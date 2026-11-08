#pragma once
// The inference queue: one thread runs the jobs one at a time, in order. A new job
// with the same non-empty key as a job that has not started yet cancels that one: the user kept
// typing, so its text is stale. A job that already started always finishes.

#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "core/check/check.hpp"

namespace revisa::queue {

class Queue {
public:
    using Job = std::function<check::Result()>;
    // nullopt: cancelled by a newer job with the same key (or by the queue's destruction).
    using Future = std::future<std::optional<check::Result>>;

    Queue();
    ~Queue();  // cancels the pending jobs and waits for the running one
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // key: "" = never cancelled (and never cancels anything).
    Future submit(std::string key, Job job);

    // Blocks until every job submitted so far has finished or been cancelled.
    void drain();

private:
    struct Pending {
        std::string key;
        Job job;
        std::promise<std::optional<check::Result>> promise;
    };

    void loop();

    std::mutex mutex_;
    std::condition_variable wake_;  // a new job or stop
    std::condition_variable idle_;  // the queue became empty and nothing runs
    std::deque<Pending> pending_;
    bool running_ = false;
    bool stop_ = false;
    std::thread thread_;  // last: started once everything else exists
};

}  // namespace revisa::queue
