#include "core/queue/queue.hpp"

#include <exception>
#include <utility>

namespace revisa::queue {

Queue::Queue() : thread_([this] { loop(); }) {}

Queue::~Queue() {
    std::deque<Pending> cancelled;
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
        cancelled.swap(pending_);
    }
    wake_.notify_all();
    for (Pending& p : cancelled) p.promise.set_value(std::nullopt);
    thread_.join();
}

Queue::Future Queue::submit(std::string key, Job job) {
    std::promise<std::optional<check::Result>> promise;
    Future future = promise.get_future();
    std::optional<Pending> stale;
    {
        std::lock_guard lock(mutex_);
        if (stop_) {
            promise.set_value(std::nullopt);
            return future;
        }
        if (!key.empty()) {
            for (auto it = pending_.begin(); it != pending_.end(); ++it) {
                if (it->key == key) {
                    stale.emplace(std::move(*it));
                    pending_.erase(it);
                    break;  // at most one pending job per key
                }
            }
        }
        pending_.push_back({std::move(key), std::move(job), std::move(promise)});
    }
    if (stale) stale->promise.set_value(std::nullopt);
    wake_.notify_one();
    return future;
}

void Queue::drain() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return pending_.empty() && !running_; });
}

void Queue::loop() {
    for (;;) {
        Pending p;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !pending_.empty(); });
            if (stop_) {
                running_ = false;
                idle_.notify_all();
                return;
            }
            p = std::move(pending_.front());
            pending_.pop_front();
            running_ = true;
        }
        try {
            p.promise.set_value(p.job());
        } catch (...) {
            p.promise.set_exception(std::current_exception());
        }
        {
            std::lock_guard lock(mutex_);
            running_ = false;
        }
        idle_.notify_all();
    }
}

}  // namespace revisa::queue
