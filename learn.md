# Learning Atomic queues

## Basic implementation from Gemini

```cpp
#include <atomic>
#include <vector>
#include <optional>
#include <iostream>

template<typename T>
class WorkStealingQueue {
private:
    struct Array {
        int64_t capacity;
        int64_t mask;
        std::atomic<T>* buffer;

        Array(int64_t cap) : capacity(cap), mask(cap - 1) {
            buffer = new std::atomic<T>[capacity];
        }
        ~Array() { delete[] buffer; }
    };

    std::atomic<int64_t> top{0};    // Controlled by Thieves (FIFO)
    std::atomic<int64_t> bottom{0}; // Controlled by Owner (LIFO)
    Array* array;

public:
    WorkStealingQueue(int64_t capacity = 1024) {
        array = new Array(capacity);
    }

    // Owner only: Pushes to the "bottom"
    void push(T item) {
        int64_t b = bottom.load(std::memory_order_relaxed);
        int64_t t = top.load(std::memory_order_acquire);

        // In a real impl, if (b - t) > capacity, you'd resize here
        array->buffer[b & array->mask].store(item, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        bottom.store(b + 1, std::memory_order_relaxed);
    }

    // Owner only: Pops from the "bottom" (LIFO)
    std::optional<T> pop() {
        int64_t b = bottom.load(std::memory_order_relaxed) - 1;
        bottom.store(b, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t t = top.load(std::memory_order_relaxed);

        if (t <= b) {
            T item = array->buffer[b & array->mask].load(std::memory_order_relaxed);
            if (t == b) {
                // Last item: race against potential thief
                if (!top.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst, std::memory_order_relaxed)) {
                    item = std::nullopt;
                }
                bottom.store(b + 1, std::memory_order_relaxed);
            }
            return item;
        } else {
            bottom.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
    }

    // Thieves only: Steal from the "top" (FIFO)
    std::optional<T> steal() {
        int64_t t = top.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = bottom.load(std::memory_order_acquire);

        if (t < b) {
            T item = array->buffer[t & array->mask].load(std::memory_order_relaxed);
            if (!top.compare_exchange_strong(t, t + 1,
                std::memory_order_seq_cst, std::memory_order_relaxed)) {
                return std::nullopt; // Lost the race to another thief or the owner
            }
            return item;
        }
        return std::nullopt;
    }
};
```

## Resize

I don't trust this code, using a global atomic queue is the way to go to manage
overflow.

```cpp
void push(T item) {
    int64_t b = bottom.load(std::memory_order_relaxed);
    int64_t t = top.load(std::memory_order_acquire);
    Array* a = array.load(std::memory_order_relaxed);

    // Check if the circular buffer is full
    if (b - t >= a->capacity) {
        // Double the capacity
        Array* new_array = new Array(a->capacity * 2);

        // Copy elements from the old buffer to the new one
        for (int64_t i = t; i < b; i++) {
            new_array->buffer[i & new_array->mask].store(
                a->buffer[i & a->mask].load(std::memory_order_relaxed),
                std::memory_order_relaxed
            );
        }

        // Update the main array pointer
        // Note: 'array' must now be a std::atomic<Array*>
        array.store(new_array, std::memory_order_release);

        // Optional: Keep the old 'a' in a 'garbage' list to delete later
        // or use a smart pointer system.
        a = new_array;
    }

    a->buffer[b & a->mask].store(item, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    bottom.store(b + 1, std::memory_order_relaxed);
}

std::optional<T> steal() {
    // 1. Get the current buffer
    Array* a = array.load(std::memory_order_acquire);

    // 2. Get the indices
    int64_t t = top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t b = bottom.load(std::memory_order_acquire);

    if (t < b) {
        // 3. Read the item
        T item = a->buffer[t & a->mask].load(std::memory_order_relaxed);

        // 4. Atomic race to claim it
        if (!top.compare_exchange_strong(t, t + 1,
            std::memory_order_seq_cst, std::memory_order_relaxed)) {
            return std::nullopt;
        }
        return item;
    }
    return std::nullopt;
}
```
