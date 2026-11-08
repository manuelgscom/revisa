#include "core/infer/llama_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/infer/cpu.hpp"
#include "core/infer/prompt.hpp"
#include "llama.h"

namespace revisa::infer {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMaxDraft = 128;  // draft tokens per verification batch
constexpr int kNgram = 3;       // sync: the last 3, then 2, emitted tokens are looked up in the input

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// llama_log_set also installs the callback in ggml, so it covers both.
// Non-null user_data => LlamaOptions::debug: logs go to stderr.
void log_callback(ggml_log_level, const char* text, void* debug) {
    if (debug != nullptr) std::fputs(text, stderr);
}

class LlamaEngine : public Engine {
public:
    explicit LlamaEngine(LlamaOptions options) : options_(std::move(options)) {}

    ~LlamaEngine() override {
        if (batch_ready_) llama_batch_free(batch_);
        if (sampler_ != nullptr) llama_sampler_free(sampler_);
        if (ctx_ != nullptr) llama_free(ctx_);
        if (model_ != nullptr) llama_model_free(model_);
    }

    LlamaEngine(const LlamaEngine&) = delete;
    LlamaEngine& operator=(const LlamaEngine&) = delete;

    EngineResult correct(std::string_view segment) override {
        const auto t_start = Clock::now();
        EngineResult r;
        if (load(r)) generate(segment, r);
        r.timings.total_ms = ms_since(t_start);
        return r;
    }

private:
    // Lazy load: model, context, sampler, <|im_end|> id and the prefix state. Attempted only once;
    // a failure is kept and returned by every later call.
    bool load(EngineResult& r) {
        if (loaded_) return true;
        if (attempted_) {
            r.error = "load_failed";
            r.error_detail = load_error_;
            return false;
        }
        attempted_ = true;
        const auto t0 = Clock::now();
        load_error_ = try_load();
        r.timings.load_ms = ms_since(t0);
        if (!load_error_.empty()) {
            r.error = "load_failed";
            r.error_detail = load_error_;
            return false;
        }
        loaded_ = true;
        return true;
    }

    // Returns an empty string on success, otherwise the error detail.
    std::string try_load() {
        llama_log_set(log_callback, options_.debug ? &options_ : nullptr);
        static std::once_flag backend_once;
        std::call_once(backend_once, [] { llama_backend_init(); });

        // An explicit, empty device list (no layers anywhere but the CPU) and no op offload, so
        // no GPU, not even an integrated one, touches the model.
        static ggml_backend_dev_t no_devices[] = {nullptr};
        llama_model_params mp = llama_model_default_params();
        mp.devices = no_devices;
        mp.n_gpu_layers = 0;
        mp.load_mode = LLAMA_LOAD_MODE_MMAP;
        model_ = llama_model_load_from_file(options_.model_path.c_str(), mp);
        if (model_ == nullptr) return "cannot load the model: " + options_.model_path;

        const int threads = options_.threads > 0 ? options_.threads : default_threads();
        llama_context_params cp = llama_context_default_params();
        cp.n_ctx = kCtx;
        cp.n_batch = kBatch;
        // A larger n_ubatch reserves a much bigger compute buffer without speeding up the CPU
        // prefill; longer batches are split into micro-batches inside llama_decode.
        cp.n_ubatch = kUbatch;
        cp.n_seq_max = 1;
        cp.n_threads = threads;
        cp.n_threads_batch = threads;
        cp.op_offload = false;
        ctx_ = llama_init_from_model(model_, cp);
        if (ctx_ == nullptr) return "cannot create the context: " + options_.model_path;
        vocab_ = llama_model_get_vocab(model_);
        mem_ = llama_get_memory(ctx_);

        const std::vector<llama_token> end = tokenize("<|im_end|>");
        if (end.size() != 1) return "the model has no <|im_end|> token";
        im_end_ = end[0];

        sampler_ = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
        n_vocab_ = llama_vocab_n_tokens(vocab_);
        batch_ = llama_batch_init(kMaxDraft + 1, 0, 1);
        batch_ready_ = true;

        // Prefix state, kept in memory: every call restores it and decodes only the rest.
        const std::vector<llama_token> prefix = tokenize(prompt_prefix());
        if (prefix.empty()) return "the prompt prefix could not be tokenized";
        llama_memory_clear(mem_, true);
        if (!decode(prefix)) return "llama_decode failed on the prompt prefix";
        const std::size_t n_state = llama_state_seq_get_size(ctx_, 0);
        prefix_state_.resize(n_state);
        if (n_state == 0 || llama_state_seq_get_data(ctx_, prefix_state_.data(), n_state, 0) != n_state) {
            return "cannot save the prefix state";
        }
        n_prefix_ = prefix.size();
        return {};
    }

    // add_special = false (no BOS), parse_special = true.
    std::vector<llama_token> tokenize(std::string_view text) const {
        const auto len = static_cast<std::int32_t>(text.size());
        std::vector<llama_token> tokens(text.size() + 8);
        std::int32_t n = llama_tokenize(vocab_, text.data(), len, tokens.data(),
                                        static_cast<std::int32_t>(tokens.size()), false, true);
        if (n < 0) {
            tokens.resize(static_cast<std::size_t>(-n));
            n = llama_tokenize(vocab_, text.data(), len, tokens.data(), static_cast<std::int32_t>(tokens.size()),
                               false, true);
        }
        tokens.resize(n < 0 ? 0 : static_cast<std::size_t>(n));
        return tokens;
    }

    std::string piece(llama_token token) const {
        char buf[256];
        const std::int32_t n = llama_token_to_piece(vocab_, token, buf, sizeof(buf), 0, false);
        if (n >= 0) return std::string(buf, static_cast<std::size_t>(n));
        std::string big(static_cast<std::size_t>(-n), '\0');
        const std::int32_t m = llama_token_to_piece(vocab_, token, big.data(), -n, 0, false);
        big.resize(m < 0 ? 0 : static_cast<std::size_t>(m));
        return big;
    }

    // Decodes the tokens right after what is already in memory (n <= kBatch, checked by the
    // caller through kCtx).
    bool decode(const std::vector<llama_token>& tokens) {
        std::vector<llama_token> copy = tokens;  // llama_batch_get_one takes a non-const pointer
        return llama_decode(ctx_, llama_batch_get_one(copy.data(), static_cast<std::int32_t>(copy.size()))) == 0;
    }

    static void fail(EngineResult& r, const char* code, std::string detail) {
        r.text.reset();
        r.error = code;
        r.error_detail = std::move(detail);
    }

    // Emitted tokens (without the stop token) and how the generation ended.
    struct Generation {
        std::vector<llama_token> tokens;
        bool stopped = false;      // <|im_end|> or another end-of-generation token
        std::string decode_error;  // non-empty: a decode or state failure
    };

    void generate(std::string_view segment, EngineResult& r) {
        const auto t_prefill = Clock::now();
        const std::string_view trimmed = trim_like_python(segment);
        const std::vector<llama_token> input = tokenize(trimmed);
        const int n = static_cast<int>(input.size());
        const int cap = max_new_tokens(n);
        const std::vector<llama_token> rest = tokenize(prompt_rest(trimmed));
        const int n_prompt = static_cast<int>(n_prefix_ + rest.size());
        r.timings.segment_tokens = n;
        r.timings.prompt_tokens = n_prompt;
        if (rest.empty()) return fail(r, "decode_failed", "the prompt could not be tokenized");
        if (n_prompt + cap > kCtx) {
            return fail(r, "too_long",
                        "the prompt has " + std::to_string(n_prompt) + " tokens; with " + std::to_string(cap) +
                            " for the answer it exceeds " + std::to_string(kCtx));
        }

        llama_memory_clear(mem_, true);
        if (llama_state_seq_set_data(ctx_, prefix_state_.data(), prefix_state_.size(), 0) == 0 ||
            llama_memory_seq_pos_max(mem_, 0) != static_cast<llama_pos>(n_prefix_) - 1) {
            llama_memory_clear(mem_, true);
            return fail(r, "decode_failed", "cannot restore the prefix state");
        }
        if (!decode(rest)) return fail(r, "decode_failed", "llama_decode failed on the prompt");
        r.timings.prefill_ms = ms_since(t_prefill);

        const auto t_gen = Clock::now();
        const Generation g = options_.speculative ? speculative(input, n_prompt, cap) : greedy(cap);
        r.timings.generate_ms = ms_since(t_gen);
        r.timings.gen_tokens = static_cast<int>(g.tokens.size());
        if (!g.decode_error.empty()) return fail(r, "decode_failed", g.decode_error);
        if (!g.stopped) {
            return fail(r, "cap_reached", "no <|im_end|> within " + std::to_string(cap) + " generated tokens");
        }
        std::string out;
        for (const llama_token t : g.tokens) out += piece(t);
        r.text = std::move(out);
    }

    bool is_stop(llama_token t) const { return t == im_end_ || llama_vocab_is_eog(vocab_, t); }

    // Plain greedy decoding, one token per llama_decode (--no-spec): the reference output.
    Generation greedy(int cap) {
        Generation g;
        llama_sampler_reset(sampler_);
        while (static_cast<int>(g.tokens.size()) < cap) {
            llama_token token = llama_sampler_sample(sampler_, ctx_, -1);
            if (is_stop(token)) {
                g.stopped = true;
                break;
            }
            g.tokens.push_back(token);
            if (static_cast<int>(g.tokens.size()) >= cap) break;
            if (llama_decode(ctx_, llama_batch_get_one(&token, 1)) != 0) {
                g.decode_error = "llama_decode failed in the generation";
                break;
            }
        }
        return g;
    }

    // Input-guided speculative decoding: the draft is the input segment itself.
    // Each step decodes [last emitted, d1..dK] in one batch, accepts the longest prefix of the
    // draft that matches the argmax, and emits the first differing argmax too. Same stopping rules
    // and cap as greedy(). input: the tokens of the trimmed segment; n_past: tokens in memory.
    Generation speculative(const std::vector<llama_token>& input, int n_past_tokens, int cap) {
        Generation g;
        std::vector<llama_token>& out = g.tokens;  // out.back() is not in memory yet
        bool done = false;
        const auto emit = [&](llama_token t) {
            if (done) return;
            if (is_stop(t)) {
                g.stopped = true;
                done = true;
                return;
            }
            out.push_back(t);
            if (static_cast<int>(out.size()) >= cap) done = true;
        };

        llama_pos n_past = n_past_tokens;
        int sync_end = 0;
        std::vector<llama_token> batch;
        emit(argmax(-1));
        while (!done) {
            // Room: the cap (+1 for <|im_end|>) and the context (positions n_past..n_past+K).
            const int room = std::min(cap - static_cast<int>(out.size()) + 1, kCtx - 1 - static_cast<int>(n_past));
            const std::vector<llama_token> draft = make_draft(input, out, sync_end, std::min(kMaxDraft, room));
            const int k = static_cast<int>(draft.size());
            batch.assign(1, out.back());
            batch.insert(batch.end(), draft.begin(), draft.end());

            if (k > 0 && !save_partial_state()) {
                g.decode_error = "cannot save the recurrent state";
                break;
            }
            if (!decode_at(batch.data(), k + 1, n_past, k > 0)) {
                g.decode_error = "llama_decode failed in the generation";
                break;
            }
            if (k == 0) {  // no sync point: one plain greedy step
                n_past += 1;
                emit(argmax(-1));
                continue;
            }

            int j = 0;
            llama_token next = argmax(0);
            while (j < k && next == draft[static_cast<std::size_t>(j)]) next = argmax(++j);
            sync_end += j;
            for (int i = 0; i < j; ++i) emit(draft[static_cast<std::size_t>(i)]);
            emit(next);

            // [last, d1..dj] stay in memory; next (the correction or bonus token) is pending.
            if (j < k && !done && !rollback(batch.data(), j + 1, n_past)) {
                g.decode_error = "cannot roll back the rejected draft";
                break;
            }
            n_past += j + 1;
        }
        return g;
    }

    // Draft: the input tokens after the sync point, plus <|im_end|> if it reaches the end of the
    // input; at most max_k tokens. Sync (prompt lookup): the last kNgram, then 2, emitted tokens
    // are searched in the input, preferring the first match at or after sync_end. While fewer
    // tokens than that were emitted, they must match the start of the input. No match => empty.
    std::vector<llama_token> make_draft(const std::vector<llama_token>& in, const std::vector<llama_token>& out,
                                        int& sync_end, int max_k) const {
        std::vector<llama_token> d;
        const int nin = static_cast<int>(in.size());
        const int no = static_cast<int>(out.size());
        int found = -1;
        for (int n = kNgram; n >= 2 && found < 0; --n) {
            if (no < n) {
                if (no <= nin && std::equal(out.begin(), out.end(), in.begin())) found = no;
                continue;
            }
            int first_after = -1, last_any = -1;
            for (int p = 0; p + n <= nin; ++p) {
                if (!std::equal(out.end() - n, out.end(), in.begin() + p)) continue;
                last_any = p + n;
                if (first_after < 0 && p + n >= sync_end) first_after = p + n;
            }
            found = first_after >= 0 ? first_after : last_any;
        }
        if (found < 0) return d;
        sync_end = found;
        for (int p = found; p < nin && static_cast<int>(d.size()) < max_k; ++p) d.push_back(in[static_cast<std::size_t>(p)]);
        if (static_cast<int>(d.size()) < max_k && found + static_cast<int>(d.size()) == nin) d.push_back(im_end_);
        return d;
    }

    // Decodes n <= kMaxDraft + 1 tokens at positions pos0.. of sequence 0. all_logits: outputs on
    // every position (draft verification), otherwise only on the last one.
    bool decode_at(const llama_token* tokens, int n, llama_pos pos0, bool all_logits) {
        if (n < 1 || n > kMaxDraft + 1) return false;
        batch_.n_tokens = n;
        for (int i = 0; i < n; ++i) {
            batch_.token[i] = tokens[i];
            batch_.pos[i] = pos0 + i;
            batch_.n_seq_id[i] = 1;
            batch_.seq_id[i][0] = 0;
            batch_.logits[i] = static_cast<std::int8_t>(all_logits || i == n - 1);
        }
        return llama_decode(ctx_, batch_) == 0;
    }

    // Argmax of the logits of output i (-1: the last one). Ties go to the lowest id, as in
    // llama.cpp's greedy sampler, so it picks exactly what greedy() would.
    llama_token argmax(int i) const {
        const float* logits = llama_get_logits_ith(ctx_, i);
        llama_token best = 0;
        for (llama_token t = 1; t < n_vocab_; ++t)
            if (logits[t] > logits[best]) best = t;
        return best;
    }

    // The Gated DeltaNet layers keep a recurrent state, not one entry per token: once a batch is
    // decoded, the rejected draft tokens are already folded into it and llama_memory_seq_rm cannot
    // take them out. So before every verification batch the recurrent part of the state is saved
    // (PARTIAL_ONLY: the attention KV cells are not copied)...
    bool save_partial_state() {
        const std::size_t size = llama_state_seq_get_size_ext(ctx_, 0, LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY);
        partial_state_.resize(size);
        return size > 0 && llama_state_seq_get_data_ext(ctx_, partial_state_.data(), size, 0,
                                                        LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY) == size;
    }

    // ...and on a rejection it is restored, the batch's KV cells (positions >= n_past) removed and
    // only the accepted tokens decoded again, so the memory holds exactly the emitted sequence.
    bool rollback(const llama_token* accepted, int n, llama_pos n_past) {
        if (llama_state_seq_set_data_ext(ctx_, partial_state_.data(), partial_state_.size(), 0,
                                         LLAMA_STATE_SEQ_FLAGS_PARTIAL_ONLY) == 0) {
            return false;
        }
        return llama_memory_seq_rm(mem_, 0, n_past, -1) && decode_at(accepted, n, n_past, false);
    }

    LlamaOptions options_;
    bool attempted_ = false;
    bool loaded_ = false;
    std::string load_error_;
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    llama_memory_t mem_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    llama_sampler* sampler_ = nullptr;
    llama_token im_end_ = -1;
    std::size_t n_prefix_ = 0;
    std::vector<std::uint8_t> prefix_state_;
    int n_vocab_ = 0;
    llama_batch batch_{};                     // speculative batches, kMaxDraft + 1 tokens
    bool batch_ready_ = false;
    std::vector<std::uint8_t> partial_state_;  // recurrent state before the current batch
};

}  // namespace

std::unique_ptr<Engine> make_llama_engine(const LlamaOptions& options) {
    return std::make_unique<LlamaEngine>(options);
}

}  // namespace revisa::infer
