/*
 * This file implements the lock free queue described by Maged M. Michael and
 * Michael L. Scott in "Simple, Fast, and Practical Non-Blocking and Blocking
 * Concurrent Queue Algorithms".
 * paper link: https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_LOCK_FREE_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_LOCK_FREE_QUEUE
#include "../common.hpp"
#include <cassert>

template <typename T>
struct TU_LockFreeQueueNode;

// The algorithm use tagged pointers to make the list ABA safe. The count is
// a version number that makes the CAS equality test fail in ABA cases.
//
// Note(linker): use `-latomic` to enable 16 bit atomics.
// Note(x86_64): compile with `-mcx16` to allow the compiler to generate the
//               `cmpxchg16b` instruction.
template <typename T>
struct alignas(16) TU_LockFreeQueueNodePtr {
    TU_LockFreeQueueNode<T> *ptr = nullptr;
    size_t count = 0;
};

template <typename T>
bool operator==(TU_LockFreeQueueNodePtr<T> const &lhs, TU_LockFreeQueueNodePtr<T> const &rhs) {
    return lhs.ptr == rhs.ptr && lhs.count == rhs.count;
}

template <typename T>
struct TU_LockFreeQueueNode {
    T data;
    TU_Atomic<TU_LockFreeQueueNodePtr<T>> next;
};

template <typename T>
struct TU_LockFreeQueue {
    alignas(64) TU_Atomic<TU_LockFreeQueueNodePtr<T>> head_;
    alignas(64) TU_Atomic<TU_LockFreeQueueNodePtr<T>> tail_;
    TU_LockFreeQueue();
    ~TU_LockFreeQueue();
    void push(T value);
    bool pop(T *result);
};


template <typename T>
TU_LockFreeQueue<T>::TU_LockFreeQueue() {
    auto node = new TU_LockFreeQueueNode<T>();
    this->head_.store({node, 0});
    this->tail_.store({node, 0});
    if (!this->head_.is_lock_free()) {
        printf("[TU_ERRO]: 16 bits atomics are not available on this platform (default to internal mutex).\n");
    }
}

template <typename T>
TU_LockFreeQueue<T>::~TU_LockFreeQueue() {
    for (TU_LockFreeQueueNodePtr<T> node = this->head_.load(); node.ptr != nullptr;) {
        TU_LockFreeQueueNodePtr<T> next = node.ptr->next;
        delete node.ptr;
        node = next;
    }
}

template <typename T>
void TU_LockFreeQueue<T>::push(T value) {
    TU_LockFreeQueueNodePtr<T> tail, next;
    auto node = new TU_LockFreeQueueNode<T>();
    node->data = std::move(value);
    node->next.store({nullptr, 0});

    for (;;) {
        tail = this->tail_.load();
        next = tail.ptr->next.load();

        if (tail == this->tail_.load()) {
            if (next.ptr == nullptr) {
                if (tail.ptr->next.compare_exchange_weak(next, {node, next.count + 1})) {
                    // 1: enthis is done
                    break;
                }
            } else {
                // 2: the tail is lagging, so we try to update it
                this->tail_.compare_exchange_weak(tail, {next.ptr, tail.count + 1});
            }
        }
    }
    // 3: try to update the tail (another thread will do the update in 2 if this fails)
    this->tail_.compare_exchange_weak(tail, {node, tail.count + 1});
}

template <typename T>
bool TU_LockFreeQueue<T>::pop(T *result) {
    TU_LockFreeQueueNodePtr<T> head, tail, next;

    for (;;) {
        head = this->head_.load();
        tail = this->tail_.load();
        next = head.ptr->next.load();

        if (head == this->head_.load()) {
            if (head == tail) { // either this empty or tail lags
                if (next.ptr == nullptr) { // the this is empty
                    return false;
                }
                // the tail is lagging so we try to advance it
                this->tail_.compare_exchange_weak(tail, {next.ptr, tail.count + 1});
            } else {
                // read the value before exchange
                *result = next.ptr->data;
                if (this->head_.compare_exchange_weak(head, {next.ptr, head.count + 1})) {
                    // we were able to advance the head therefore our result
                    // contains the right head value, otherwise, the head we
                    // read has been moved by another thread so we need to
                    // retry
                    break;
                }
            }
        }
    }
    delete head.ptr;
    return true;
}

#endif
