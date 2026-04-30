/*
 * This queue lock free queue is implemented over a limited ring buffer (the
 * queue can overflow).
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_FINITE_LOCK_FREE_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_FINITE_LOCK_FREE_QUEUE
#include "../common.hpp"

// push at the end and pop at the start:
//
// queue:
// [---############-----]
//     ^           ^
//     start       end
//
// empty queue:
// [--------------------]
//                 ^
//                 start/end

template <typename T, size_t SIZE = 1024>
struct TU_FiniteLockFreeQueue {
    alignas(64) TU_Atomic<size_t> start = 0;
    alignas(64) TU_Atomic<size_t> end = 0;
    TU_RingBuffer<T, SIZE> buffer;
    bool push(T value);
    bool pop(T *result);
    TU_FiniteLockFreeQueue() = default;
    TU_FiniteLockFreeQueue(TU_FiniteLockFreeQueue const &) = delete;
    TU_FiniteLockFreeQueue(TU_FiniteLockFreeQueue &&other)
        : start(other.start.load()), end(other.end.load()), buffer(std::move(other.buffer)) {}
};

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::push(T value) {
    size_t e = this->end.fetch_add(1, std::memory_order_acquire);

    if ((e - this->start.load()) >= SIZE) {
        // the queue is full, we cannot push
        this->end.fetch_sub(1, std::memory_order_acquire);
        return false;
    }
    this->buffer[e] = std::move(value);
    return true;
}

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::pop(T *result) {
    assert(result != nullptr);
    size_t s = this->start.fetch_add(1, std::memory_order_acquire);

    if (s >= this->end.load()) {
        this->start.fetch_sub(1, std::memory_order_acquire);
        return false;
    }
    *result = std::move(this->buffer[s]);
    return true;
}


#endif
