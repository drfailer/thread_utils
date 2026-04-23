/*
 * This file implements the lock free queue described by Maged M. Michael and
 * Michael L. Scott in "Simple, Fast, and Practical Non-Blocking and Blocking
 * Concurrent Queue Algorithms".
 * paper link: https://www.cs.rochester.edu/u/scott/papers/1996_PODC_queues.pdf
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_LOCK_FREE_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_LOCK_FREE_QUEUE
#include <memory>
#include "../common.hpp"

template <typename T>
struct TU_LockFreeQueueNode;

template <typename T>
using TU_LockFreeQueueNodePtr = std::shared_ptr<TU_LockFreeQueueNode<T>>;

template <typename T>
struct TU_LockFreeQueueNode {
    T data;
    TU_Atomic<TU_LockFreeQueueNodePtr<T>> next;
};

template <typename T>
struct TU_LockFreeQueue {
    TU_Atomic<TU_LockFreeQueueNodePtr<T>> head;
    TU_Atomic<TU_LockFreeQueueNodePtr<T>> tail;
};

template <typename T>
void tu_lfq_init(TU_LockFreeQueue<T> *queue) {
    // the head and the tail always point to a node (we use a dummy node for
    // the empty list)
    auto node = std::make_shared<TU_LockFreeQueueNode<T>>();
    queue->head.store(node);
    queue->tail.store(node);
}

template <typename T>
void tu_lfq_fini(TU_LockFreeQueue<T> *) {
    // TODO: the shared_ptr version doesn't require free
}

template <typename T>
void tu_lfq_push(TU_LockFreeQueue<T> *queue, T value) {
    TU_LockFreeQueueNodePtr<T> tail, next;
    auto node = std::make_shared<TU_LockFreeQueueNode<T>>();
    node->data = std::move(value);
    node->next = nullptr;

    for (;;) {
        tail = queue->tail.load();
        next = tail->next.load();

        if (tail == queue->tail.load()) {
            if (next == nullptr) {
                if (tail->next.compare_exchange_weak(next, node)) {
                    // 1: enqueue is done
                    break;
                }
            } else {
                // 2: the tail is lagging, so we try to update it
                queue->tail.compare_exchange_weak(tail, next);
            }
        }
    }
    // 3: try to update the tail (another thread will do the update in 2 if this fails)
    queue->tail.compare_exchange_weak(tail, node);
}

template <typename T>
bool tu_lfq_pop(TU_LockFreeQueue<T> *queue, T *result) {
    for (;;) {
        TU_LockFreeQueueNodePtr<T> head = queue->head.load();
        TU_LockFreeQueueNodePtr<T> tail = queue->tail.load();
        TU_LockFreeQueueNodePtr<T> next = head->next.load();

        if (head == queue->head.load()) {
            if (head == tail) { // either queue empty or tail lags
                if (next == nullptr) { // the queue is empty
                    return false;
                }
                // the tail is lagging so we try to advance it
                queue->tail.compare_exchange_weak(tail, next);
            } else {
                // read the value before exchange
                *result = next->data;
                if (queue->head.compare_exchange_weak(head, next)) {
                    // we were able to advance the head therefore our result
                    // contains the right head value, otherwise, the head we
                    // read has been moved by another thread so we need to
                    // retry
                    break;
                }
            }
        }
    }
    // TODO: free head
    return true;
}

#endif
