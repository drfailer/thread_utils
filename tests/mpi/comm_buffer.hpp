#ifndef TESTS_MPI_BUFFER_POOL
#define TESTS_MPI_BUFFER_POOL
#include "thread_utils/thread_utils.hpp"
#include <stdio.h>
#include <source_location>
#include <vector>
#include <map>

struct TU_CommBuffer {
    char  *data;
    size_t size;
    size_t cap;
};

struct TU_CommBufferPool {
    bool track = false;
    std::vector<TU_CommBuffer> cpu_free = {};
    std::map<char *, std::source_location> cpu_used = {};

    // TODO: this is not ideal that we do sneaky mallocs here
    TU_CommBuffer alloc_cpu(size_t size, std::source_location loc = std::source_location::current()) {
        if (this->cpu_free.empty()) {
            return TU_CommBuffer{
                .data = new char[size],
                .size = size,
                .cap = size,
            };
        }
        TU_CommBuffer buffer = this->cpu_free.back();
        this->cpu_free.pop_back();
        // TODO: do we want to search a big enough buffer in the list?
        if (buffer.cap < size) {
            delete[] buffer.data;
            buffer.data = new char[size];
            buffer.cap = size;
        }
        buffer.size = size;
        if (this->track) {
            this->cpu_used[buffer.data] = loc;
        }
        return buffer;
    }

    void release_cpu(TU_CommBuffer buffer) {
        if (this->track) {
            auto it = this->cpu_used.find(buffer.data);
            if (it == this->cpu_used.end()) {
                // here we consider that we cannot fill the pool using release,
                // otherwise we would have to search the pointer in the free
                // array. We also consider that the users will not reallocate
                // the data in the buffer manually.
                printf("[TU_ERROR]: tried to release buffer twice.\n");
                return;
            }
            this->cpu_used.erase(it);
        }
        this->cpu_free.push_back(buffer);
    }

    void fill_cpu(size_t size, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            this->cpu_free.push_back(TU_CommBuffer{
                .data = new char[size],
                .size = size,
                .cap = size,
            });
        }
    }

    TU_CommBufferPool(bool track = false) : track(track) {}

    ~TU_CommBufferPool() {
        for (auto &buffer : this->cpu_free) {
            delete[] buffer.data;
        }
        for (auto &[data, loc] : this->cpu_used) {
            printf("[TU_ERROR]: buffer %p not released (allocated at %s(%d:%d)).\n",
                    data, loc.file_name(), loc.line(), loc.column());
            delete[] data;
        }
    }

    TU_CommBufferPool(TU_CommBufferPool const &) = delete;
    TU_CommBufferPool &operator=(TU_CommBufferPool const &) = delete;

    TU_CommBufferPool(TU_CommBufferPool &&other)
        : cpu_free(std::move(other.cpu_free)) {}

    TU_CommBufferPool &operator=(TU_CommBufferPool &&other) {
        this->cpu_free = std::move(other.cpu_free);
        return *this;
    }
};

#endif
