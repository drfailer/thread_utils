#ifndef THREAD_UTILS_DATA_STRUCTURES_WORK_STEAL_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_WORK_STEAL_QUEUE
#include "../common.hpp"

template <typename T, size_t SIZE=1024>
struct TU_WorkStealQueue {
    struct RingBuffer {
        static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be a power of 2.");
        static constexpr size_t MASK = SIZE - 1;
        T buffer[SIZE];
        T &operator[](size_t idx) { return buffer[idx & MASK]; }
    };

    alignas(64) TU_Atomic<size_t> top_ = 0;
    alignas(64) TU_Atomic<size_t> bottom_ = 0;
    RingBuffer buffer_;

    bool push(T value) {
        size_t b = this->bottom_.fetch_add(1, std::memory_order_relaxed);
        size_t t = this->top_.load(std::memory_order_relaxed);

        if ((b - t) >= SIZE) {
            this->bottom_.store(b - 1); // the queue was full, reset and leave
            return false;
        }
        this->buffer_[b] = std::move(value);
        return true;
    }

    bool pop(T *result) {
        bool poped = true;
        size_t b = this->bottom_.load(std::memory_order_relaxed) - 1;
        this->bottom_.store(b, std::memory_order_relaxed);
        size_t t = this->top_.load(std::memory_order_relaxed);

        if (b >= t) { // queue non empty or one element
            *result = this->buffer_[b];
            if (t == b) {
                // there is one element in the queue so we compete with a theif
                if (!this->top_.compare_exchange_strong(t, t + 1)) {
                    poped = false;
                }
                // Reset the bottom (in that case we advanced the queue instead
                // pushing the head). We reset after the test to increase pop
                // chances (thieves will look at the head value).
                this->bottom_.store(b + 1, std::memory_order_relaxed);
            }
        } else {
            // reset the bottom (the queue was emtpy)
            this->bottom_.store(b + 1, std::memory_order_relaxed);
            return false;
        }
        return poped;
    }

    bool steal(T *result) {
        size_t b = this->bottom_.load(std::memory_order_acquire);
        size_t t = this->top_.load(std::memory_order_acquire);

        if (b > t) { // the queue is not empty
            *result = this->buffer_[t];
            if (this->top_.compare_exchange_strong(t, t + 1)) {
                // we were able to move the top, so the element is valid
                return true;
            }
        }
        return false;
    }
};

#endif
