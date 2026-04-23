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
struct TU_LockFreeQueue {
    struct Node;
    using NodePtr = std::shared_ptr<Node>;
    struct Node {
        T data;
        TU_Atomic<NodePtr> next;
    };

    TU_Atomic<NodePtr> head;
    TU_Atomic<NodePtr> tail;

    TU_LockFreeQueue() {
        auto node = std::make_shared<Node>();
        this->head.store(node);
        this->tail.store(node);
    }

    ~TU_LockFreeQueue() {
        // TODO: the shared_ptr version doesn't require free
    }

    void push(T value) {
        NodePtr tail, next;
        auto node = std::make_shared<Node>();
        node->data = std::move(value);
        node->next = nullptr;

        for (;;) {
            tail = this->tail.load();
            next = tail->next.load();

            if (tail == this->tail.load()) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_weak(next, node)) {
                        // 1: enthis is done
                        break;
                    }
                } else {
                    // 2: the tail is lagging, so we try to update it
                    this->tail.compare_exchange_weak(tail, next);
                }
            }
        }
        // 3: try to update the tail (another thread will do the update in 2 if this fails)
        this->tail.compare_exchange_weak(tail, node);
    }

    bool pop(T *result) {
        for (;;) {
            NodePtr head = this->head.load();
            NodePtr tail = this->tail.load();
            NodePtr next = head->next.load();

            if (head == this->head.load()) {
                if (head == tail) { // either this empty or tail lags
                    if (next == nullptr) { // the this is empty
                        return false;
                    }
                    // the tail is lagging so we try to advance it
                    this->tail.compare_exchange_weak(tail, next);
                } else {
                    // read the value before exchange
                    *result = next->data;
                    if (this->head.compare_exchange_weak(head, next)) {
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
};

#endif
