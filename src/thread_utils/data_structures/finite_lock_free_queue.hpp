/*
 * This queue lock free queue is implemented over a finite ring buffer (the
 * queue can overflow).
 *
 * This queue is currently an implemenation of Dmitry Vyukov MPMC queue.
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_FINITE_LOCK_FREE_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_FINITE_LOCK_FREE_QUEUE
#include "../common.hpp"
#include "ring_buffer.hpp"

// push at the tail and pop at the head:
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

// Dmitry Vyukov MPMC queue

template <typename T>
struct TU_FiniteLockFreeQueueNode {
    alignas(CACHE_LINE) TU_Atomic<size_t> sequence;
    T data;
    TU_FiniteLockFreeQueueNode<T> &operator=(TU_FiniteLockFreeQueueNode<T> &&other) {
        this->sequence.store(other.sequence.load());
        this->data = other.data;
        return *this;
    }
};

template <typename T, size_t SIZE>
struct TU_FiniteLockFreeQueue {
    TU_RingBuffer<TU_FiniteLockFreeQueueNode<T>, SIZE> buffer;
    alignas(CACHE_LINE) TU_Atomic<size_t> head{0};
    alignas(CACHE_LINE) TU_Atomic<size_t> tail{0};

    bool push(T value);
    bool pop(T *result);

    TU_FiniteLockFreeQueue();
    TU_FiniteLockFreeQueue(TU_FiniteLockFreeQueue<T, SIZE> const &) = delete;
    TU_FiniteLockFreeQueue<T, SIZE> &operator=(TU_FiniteLockFreeQueue<T, SIZE> const &) = delete;
    TU_FiniteLockFreeQueue(TU_FiniteLockFreeQueue<T, SIZE> &&other)
        : buffer(std::move(other.buffer)), head(other.head.load()), tail(other.tail.load()) {}
    TU_FiniteLockFreeQueue<T, SIZE> &operator=(TU_FiniteLockFreeQueue<T, SIZE> &&other) {
        this->buffer = std::move(other.buffer);
        this->head.store(other.head.load());
        this->tail.store(other.tail.load());
        return *this;
    }
    ~TU_FiniteLockFreeQueue() = default;
};

template <typename T, size_t SIZE>
TU_FiniteLockFreeQueue<T, SIZE>::TU_FiniteLockFreeQueue() {
    for (size_t i = 0; i < SIZE; ++i) {
        buffer[i].sequence.store(i, std::memory_order_relaxed);
    }
}

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::push(T value) {
    TU_FiniteLockFreeQueueNode<T> *node;
    size_t t = this->tail.load(std::memory_order_relaxed);

    for (;;) {
        node = &this->buffer[t];
        size_t seq = node->sequence.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)t;
        if (diff == 0) {
            if (this->tail.compare_exchange_weak(t, t + 1, std::memory_order_relaxed)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            t = this->tail.load(std::memory_order_relaxed);
        }
    }
    node->data = value;
    node->sequence.store(t + 1, std::memory_order_release);
    return true;
}

template <typename T, size_t SIZE>
bool TU_FiniteLockFreeQueue<T, SIZE>::pop(T *result) {
    TU_FiniteLockFreeQueueNode<T> *node;
    size_t h = this->head.load(std::memory_order_relaxed);

    for (;;) {
        node = &this->buffer[h];
        size_t seq = node->sequence.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(h + 1);
        if (diff == 0) {
            if (this->head.compare_exchange_weak(h, h + 1, std::memory_order_relaxed)) {
                break;
            }
        } else if (diff < 0) {
            return false;
        } else {
            h = this->head.load(std::memory_order_relaxed);
        }
    }
    *result = node->data;
    node->sequence.store(h + SIZE, std::memory_order_release);
    return true;
}

#endif
