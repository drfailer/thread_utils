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

template <typename T, size_t SIZE = 1024>
struct TU_FiniteOverflowQueue {
    TU_LockFreeQueue<T> overflow_queue;
    TU_FiniteLockFreeQueue<T, SIZE> finite_queue;
    TU_AtomicFlag overflow_flag = false;
    void push(T value);
    bool pop(T *result);

    TU_FiniteOverflowQueue() = default;
    TU_FiniteOverflowQueue(TU_FiniteOverflowQueue const &) = delete;
    TU_FiniteOverflowQueue(TU_FiniteOverflowQueue &&other)
        : overflow_queue(std::move(other.overflow_queue)),
          finite_queue(std::move(other.finite_queue)),
          overflow_flag(other.overflow_flag.load()) {}
};

template <typename T, size_t SIZE>
void TU_FiniteOverflowQueue<T, SIZE>::push(T value) {
    // we keep trying to add to the finite queue in case the flag is note up to date
    if (this->finite_queue.push(value)) [[likely]] {
        return;
    }
    // overflow, update the flag
    this->overflow_flag.store(true, std::memory_order_release);
    this->overflow_queue.push(value);
}

template <typename T, size_t SIZE>
bool TU_FiniteOverflowQueue<T, SIZE>::pop(T *result) {
    if (this->overflow_flag.load(std::memory_order_acquire)) [[unlikely]] {
        if (this->overflow_queue.pop(result)) {
            return true;
        }
        // the overflow queue is empty so we change the flag
        this->overflow_flag.store(false, std::memory_order_release);
    }
    return this->finite_queue.pop(result);
}

#endif
