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

// FIXME: the node pool makes the push a lot slower for some reason.
// #define TU_LFQ_NODE_POOL

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
    alignas(CACHE_LINE) TU_Atomic<TU_LockFreeQueueNodePtr<T>> head_;
    alignas(CACHE_LINE) TU_Atomic<TU_LockFreeQueueNodePtr<T>> tail_;
    #ifdef TU_LFQ_NODE_POOL
    alignas(CACHE_LINE) TU_Atomic<TU_LockFreeQueueNodePtr<T>> free_;
    #endif // TU_LFQ_NODE_POOL
    TU_LockFreeQueue();
    TU_LockFreeQueue(TU_LockFreeQueue<T> const &other) = delete;
    TU_LockFreeQueue(TU_LockFreeQueue<T> &&other);
    ~TU_LockFreeQueue();
    void push(T value);
    bool pop(T *result);
    private: TU_LockFreeQueueNode<T> *allocate_node();
    private: void release_node(TU_LockFreeQueueNode<T> *node);
};


template <typename T>
TU_LockFreeQueue<T>::TU_LockFreeQueue(TU_LockFreeQueue<T> &&other) {
    // NOTE: we consider that other is only accessed by one thread.
    this->head_.store(other.head_);
    other.head_.store({});
    this->tail_.store(other.tail_);
    other.tail_.store({});
}

template <typename T>
TU_LockFreeQueue<T>::TU_LockFreeQueue() {
    static_assert(alignof(TU_LockFreeQueueNodePtr<T>) == 16, "NodePtr must be 16-byte aligned");
    static_assert(sizeof(TU_LockFreeQueueNodePtr<T>) == 16, "NodePtr must be 16 bytes");
    static_assert(sizeof(TU_Atomic<TU_LockFreeQueueNodePtr<T>>) == 16, "wrong atomic size");
    auto node = allocate_node();
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
        node.ptr = nullptr;
        node = next;
    }
    #ifdef TU_LFQ_NODE_POOL
    for (TU_LockFreeQueueNodePtr<T> node = this->free_.load(); node.ptr != nullptr;) {
        TU_LockFreeQueueNodePtr<T> next = node.ptr->next;
        delete node.ptr;
        node.ptr = nullptr;
        node = next;
    }
    #endif // TU_LFQ_NODE_POOL
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
    release_node(head.ptr);
    return true;
}

#ifdef TU_LFQ_NODE_POOL
template <typename T>
TU_LockFreeQueueNode<T> *TU_LockFreeQueue<T>::allocate_node() {
    TU_LockFreeQueueNodePtr<T> free;
    TU_LockFreeQueueNodePtr<T> next;

    for (;;) {
        free = this->free_.load();
        if (free.ptr == nullptr) {
            // the pool is empty
            return new TU_LockFreeQueueNode<T>();
        }
        next = free.ptr->next.load();
        if (this->free_.compare_exchange_weak(free, {next.ptr, next.count + 1})) {
            return free.ptr;
        }
        // the head has been stolen by another thread, so we retry
    }
}

template <typename T>
void TU_LockFreeQueue<T>::release_node(TU_LockFreeQueueNode<T> *node) {
    assert(node != nullptr);
    TU_LockFreeQueueNodePtr<T> free;

    // TODO: add a counter and a max pool size so we don't keep increasing the
    //       pool size infinitely

    for (;;) {
        free = this->free_.load();
        node->next.store({free.ptr, free.count + 1});
        if (this->free_.compare_exchange_weak(free, {node, free.count + 1})) {
            break;
        }
    }
}
#else
template <typename T>
TU_LockFreeQueueNode<T> *TU_LockFreeQueue<T>::allocate_node() {
    return new TU_LockFreeQueueNode<T>();
}

template <typename T>
void TU_LockFreeQueue<T>::release_node(TU_LockFreeQueueNode<T> *node) {
    delete node;
}
#endif // TU_LFQ_NODE_POOL

#endif
