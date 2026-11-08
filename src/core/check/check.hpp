#pragma once
// The whole path of a text: segment -> NFC -> protect -> model -> diff -> classify
// -> guards -> disabled types. Shared by `revisa check`, and later by the server and the LSP.

#include <cstddef>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/guard/config.hpp"
#include "core/infer/engine.hpp"

namespace revisa::check {

struct Edit {
    std::size_t start = 0, end = 0;            // UTF-16 units over the original text, end exclusive
    std::size_t byte_start = 0, byte_end = 0;  // the same range in bytes
    std::string from;                          // original text of the range
    std::string to;
    catalog::Type type = catalog::Type::Otro;
    std::optional<std::string> explanation;    // nullopt for OTRO
};

struct Options {
    std::set<catalog::Type> disabled;  // guard 6
    // false: none of the six guards (guard::invalid_output and guard::apply, disabled types
    // included); the edits are still classified. The model's view of the benchmark.
    bool guards = true;
    guard::Thresholds thresholds;  // guards 2 and 5 (the product's defaults unless calibrating)
};

// One call to the engine (`revisa check --jsonl --timings`).
struct SegmentTiming {
    double ms = 0;         // Engine total for the segment (model load excluded)
    bool changed = false;  // the model's output differs from the segment (before the guards)
    int tokens = 0;        // segment tokens
};

struct Result {
    std::string corrected;      // the text with every kept edit applied
    std::vector<Edit> edits;    // sorted, not overlapping
    int segments_skipped = 0;   // too long, protected, special tokens, cap reached or rejected
    bool changed = false;       // corrected != text
    std::vector<SegmentTiming> timings;  // one per segment sent to the engine, in order
    std::string error;          // engine error that aborts (load_failed, decode_failed…); empty = ok
    std::string error_detail;
};

// true if some segment of text would be sent to the model.
bool needs_model(std::string_view text);

// Why check_text skips a segment before the model.
enum class Skip { None, Long, Protected, Special };

// One segment as check_text sees it: the same segmenter and the same skip rules.
struct ModelSegment {
    std::size_t start = 0, end = 0;  // bytes in the text, [start, end), trimmed
    std::string text;                // NFC of the trimmed segment: the user message of the model
    Skip skip = Skip::None;
};

// The segments of text, in order (`revisa segment --jsonl` and the dataset renderer).
std::vector<ModelSegment> model_segments(std::string_view text);

// "", "long", "protected" or "special".
const char* skip_name(Skip s);

Result check_text(std::string_view text, infer::Engine& engine, const Options& options = {});

// text with the given edits applied, by byte_start/byte_end and `to`: any subset of the edits of a
// Result over the same text (the tray app applies the ones the user accepts). The edits must be
// sorted and not overlapping; one that starts before the end of the previous one, or falls outside
// text, is ignored. No edits = text; all of a Result's edits = its corrected text.
std::string apply(std::string_view text, std::span<const Edit> edits);

// Diff + classification of a whole pair, with no segmentation, normalization or guards: the
// canonical alignment of `revisa diff` and of the benchmark's gold_edits.
std::vector<Edit> diff_pair(std::string_view original, std::string_view corrected);

}  // namespace revisa::check
