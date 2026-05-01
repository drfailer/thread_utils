/*
 * This is a really small cache used by the workers, and it is not supposed to
 * be shared between threads. Its role is to allow workers to save some operations
 * and reduce context switching. When the worker update its cache, the cache acts
 * as a queue, but when the worker process the elements it acts as a stack so
 * that the last element added is processed first.
 *
 * NOTE: To avoid unbalanced workload between the workers and maintain this
 * cache efficient, the size of the cache should be small (1 to 8 items, but it
 * depends on who much data flows inside the graph, and how complicated the
 * tasks are).
 *
 * NOTE: state operations should not be pushed to the cache.
 */

#ifndef THREAD_UTILS_DATA_STRUCTURES_CACHE_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_CACHE_QUEUE

template <typename T>
struct TU_CacheQueue {
    size_t size = 0;
    size_t head = 0;
    size_t tail = 0;
    size_t mask = 0;
    T *data = nullptr;
    TU_CacheQueue(size_t size);
    TU_CacheQueue(TU_CacheQueue<T> const &other) = delete;
    TU_CacheQueue(TU_CacheQueue<T> &&other);
    ~TU_CacheQueue();
    bool cache(T new_value, T *poped_value);
    bool pop(T *result);
};

template <typename T>
TU_CacheQueue<T>::TU_CacheQueue(size_t size)
    : size(size), mask(size - 1), data(new T[size]) {
    assert(((size & (size - 1)) == 0) && "cache size should be a power of 2");
}

template <typename T>
TU_CacheQueue<T>::TU_CacheQueue(TU_CacheQueue<T> &&other)
    : size(other.size), head(other.head), tail(other.tail), mask(other.mask), data(other.data) {
    other.data = nullptr;
}

template <typename T>
TU_CacheQueue<T>::~TU_CacheQueue() {
    delete[] data;
}

template <typename T>
bool TU_CacheQueue<T>::cache(T new_value, T *poped_value) {
    bool poped = false;
    if ((this->tail - this->head) >= this->size) {
        *poped_value = this->data[this->head & mask];
        this->head += 1;
        poped = true;
    }
    this->data[this->tail & mask] = std::move(new_value);
    this->tail += 1;
    return poped;
}

template <typename T>
bool TU_CacheQueue<T>::pop(T *result) {
    if (this->tail <= this->head) {
        return false;
    }
    this->tail -= 1;
    *result = this->data[this->tail & mask];
    return true;
}

#endif
