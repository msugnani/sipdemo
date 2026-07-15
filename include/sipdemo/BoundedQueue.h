#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace sipdemo {

// A fixed-capacity, thread-safe queue for a single-producer/single-consumer (or
// MPMC) handoff between the RTP receive thread and the playout thread.
//
// Provides both blocking and non-blocking variants:
//   - push()/pop()      block on a full/empty queue (backpressure).
//   - tryPush()/tryPop() never block.
// close() wakes all waiters so threads can shut down cleanly; after close(),
// pop() drains remaining items then returns nullopt.
template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t capacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Block until space is available (or the queue is closed).
    // Returns false if the queue was closed before the item could be enqueued.
    bool push(T item) {
        std::unique_lock<std::mutex> lk(mu_);
        notFull_.wait(lk, [&] { return q_.size() < capacity_ || closed_; });
        if (closed_) return false;
        q_.push(std::move(item));
        lk.unlock();
        notEmpty_.notify_one();
        return true;
    }

    // Enqueue without blocking. Returns false if full or closed.
    bool tryPush(T item) {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_ || q_.size() >= capacity_) return false;
        q_.push(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    // Block until an item is available. Returns nullopt once the queue is
    // closed and drained.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lk(mu_);
        notEmpty_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;  // closed + drained
        T item = std::move(q_.front());
        q_.pop();
        lk.unlock();
        notFull_.notify_one();
        return item;
    }

    // Dequeue without blocking. Returns nullopt if empty.
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lk(mu_);
        if (q_.empty()) return std::nullopt;
        T item = std::move(q_.front());
        q_.pop();
        notFull_.notify_one();
        return item;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    bool closed() const {
        std::lock_guard<std::mutex> lk(mu_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

    size_t capacity() const { return capacity_; }

private:
    mutable std::mutex mu_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::queue<T> q_;
    const size_t capacity_;
    bool closed_ = false;
};

}  // namespace sipdemo
