#ifndef THREAD_UTILS_DATA_STRUCTURES_RING_BUFFER
#define THREAD_UTILS_DATA_STRUCTURES_RING_BUFFER

template <typename T, size_t SIZE=1024>
struct TU_RingBuffer {
    static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be a power of 2.");
    static constexpr size_t MASK = SIZE - 1;
    T buffer[SIZE];
    T &operator[](size_t idx) { return buffer[idx & MASK]; }
};

#endif
