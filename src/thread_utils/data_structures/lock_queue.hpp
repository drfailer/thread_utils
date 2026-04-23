#ifndef THREAD_UTILS_DATA_STRUCTURES_LOCK_QUEUE
#define THREAD_UTILS_DATA_STRUCTURES_LOCK_QUEUE
#include <queue>
#include "../common.hpp"

template <typename T>
struct TU_LockQueue {
    TU_Mutex mutex;
    std::queue<T> data;

    void push(T value) {
        TU_Lock lck(this->mutex);
        this->data.push(std::move(value));
    }

    bool pop(T *result) {
        TU_Lock lck(this->mutex);
        if (this->data.empty()) {
            return false;
        }
        *result = this->data.front();
        this->data.pop();
        return true;
    }
};

#endif
