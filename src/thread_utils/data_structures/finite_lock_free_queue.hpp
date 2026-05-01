/*
 * This queue lock free queue is implemented over a limited ring buffer (the
 * queue can overflow).
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_FINITE_LOCK_FREE_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_FINITE_LOCK_FREE_QUEUE
#include "../common.hpp"
#include "ring_buffer.hpp"

// push at the end and pop at the start:
//
// queue:
// [---############-----]
//     ^           ^
//     head        tail
//
// empty queue:
// [--------------------]
//                 ^
//                 head/tail

template <typename T, size_t SIZE = 1024>
struct TU_FiniteLockFreeQueue {
    alignas(CACHE_LINE) TU_Atomic<size_t> head = 0;
    alignas(CACHE_LINE) TU_Atomic<size_t> tail = 0;
    alignas(CACHE_LINE) TU_Atomic<size_t> write = 0;
    TU_RingBuffer<T, SIZE> buffer;
    bool push(T value);
    bool pop(T *result);
    TU_FiniteLockFreeQueue() = default;
    TU_FiniteLockFreeQueue(TU_FiniteLockFreeQueue const &) = delete;
    TU_FiniteLockFreeQueue(TU_FiniteLockFreeQueue &&other)
        : head(other.head.load()), tail(other.tail.load()),
          write(other.write.load()), buffer(std::move(other.buffer)) {}
};

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::push(T value) {
    size_t w = this->write.load(std::memory_order_relaxed);

    // try to reserve a slot without "drifting" the counter
    do {
        size_t h = this->head.load(std::memory_order_acquire);
        if ((w - h) >= SIZE) {
            return false; // the queue is full
        }
    } while (!this->write.compare_exchange_weak(w, w + 1, std::memory_order_release));

    // write the data
    this->buffer[w] = std::move(value);

    size_t expected = w;
    while (!this->tail.compare_exchange_weak(expected, w + 1, std::memory_order_release)) {
        expected = w; // reset expected to our reserved slot
        cross_platform_yield();
    }
    return true;
}

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::pop(T *result) {
    size_t h = this->head.load(std::memory_order_relaxed);

    do {
        size_t t = this->tail.load(std::memory_order_acquire);
        if (h >= t) {
            return false; // Queue is empty
        }
    } while (!this->head.compare_exchange_weak(h, h + 1, std::memory_order_release));

    *result = this->buffer[h];
    return true;
}

#endif
