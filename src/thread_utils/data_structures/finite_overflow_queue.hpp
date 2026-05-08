/*
 * This implementation is a combintation of the finite lock free queue
 * (implemented over a ring buffer), and a standard lock free queue
 * (implemented using a linked list). This allow leveraging the speed of the
 * limited queue but also add a non limited queue to mitigate overflow.
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_FINITE_OVERFLOW_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_FINITE_OVERFLOW_QUEUE
#include "finite_lock_free_queue.hpp"
#include "lock_free_queue.hpp"
#include "../common.hpp"

// TODO: this type should be removed and the Finite queue should be able to grow (possible using a mutex)
template <typename T, size_t SIZE = 1024>
struct TU_FiniteOverflowQueue {
    TU_LockFreeQueue<T> overflow_queue;
    TU_FiniteLockFreeQueue<T, SIZE> finite_queue;

    void push(T value);
    bool pop(T *result);

    TU_FiniteOverflowQueue() = default;
    TU_FiniteOverflowQueue(TU_FiniteOverflowQueue const &) = delete;
    TU_FiniteOverflowQueue<T, SIZE> &operator=(TU_FiniteOverflowQueue const &) = delete;
    TU_FiniteOverflowQueue(TU_FiniteOverflowQueue &&other)
        : overflow_queue(std::move(other.overflow_queue)),
          finite_queue(std::move(other.finite_queue)) {}
    TU_FiniteOverflowQueue<T, SIZE> &operator=(TU_FiniteOverflowQueue &&other) {
        this->overflow_queue = std::move(other.overflow_queue);
        this->finite_queue = std::move(other.finite_queue);
        return *this;
    }
    ~TU_FiniteOverflowQueue() = default;
};

template <typename T, size_t SIZE>
void TU_FiniteOverflowQueue<T, SIZE>::push(T value) {
    if (this->finite_queue.push(value)) [[likely]] {
        return;
    }
    this->overflow_queue.push(value);
}

template <typename T, size_t SIZE>
bool TU_FiniteOverflowQueue<T, SIZE>::pop(T *result) {
    if (this->finite_queue.pop(result)) [[likely]] {
        return true;
    }
    return this->overflow_queue.pop(result);
}

#endif
