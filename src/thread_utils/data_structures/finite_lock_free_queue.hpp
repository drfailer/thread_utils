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

    // try to increment the write cursor and write the value
    for (;;) {
        size_t h = this->head.load(std::memory_order_acquire);

        if ((w - h) >= SIZE) {
            return false;
        }
        if (this->write.compare_exchange_weak(w, w + 1, std::memory_order_release)) {
            this->buffer[w] = std::move(value);
            break;
        }
    }

    // now we wait until we can move the tail so the consumers can pop
    for (;;) {
        size_t t = w; // we use an additional variable to avoid changing w
        if (this->tail.compare_exchange_weak(t, w + 1, std::memory_order_release)) {
            break;
        }
        cross_platform_yield();
    }
    return true;
}

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::pop(T *result) {
    assert(result != nullptr);
    size_t h = this->head.load(std::memory_order_relaxed);

    // we try to increment the head
    for (;;) {
        size_t t = this->tail.load(std::memory_order_acquire);
        if (h >= t) {
            // the queue is empty
            return false;
        }
        if (this->head.compare_exchange_weak(h, h + 1, std::memory_order_release)) {
            break;
        }
    }
    *result = this->buffer[h];
    return true;
}


#endif
