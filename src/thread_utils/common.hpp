#ifndef THREAD_UTILS_COMMON
#define THREAD_UTILS_COMMON
#include <thread>
#include <mutex>
#include <semaphore>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <stdint.h>


using TU_Thread = std::thread;
using TU_Mutex = std::mutex;
using TU_Cond = std::condition_variable;
using TU_Sem = std::counting_semaphore<256>;
using TU_BinSem = std::counting_semaphore<1>;
using TU_AtomicFlag = std::atomic<bool>;
using TU_Lock = std::unique_lock<std::mutex>;

template <typename T>
using TU_Atomic = std::atomic<T>;

template <typename T>
using TU_Array = std::vector<T>;

using tu_i64 = int64_t;
using tu_u64 = uint64_t;

using TU_ExecProc = void (*)(void*, tu_i64);

struct TU_ExecData {
    TU_ExecProc exec_func;
    void *data;
    tu_i64 index;
};

#endif
