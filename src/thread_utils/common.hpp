#ifndef THREAD_UTILS_COMMON
#define THREAD_UTILS_COMMON
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdint.h>


using TU_Thread = std::thread;
using TU_Mutex = std::mutex;
using TU_Cond = std::condition_variable;
using TU_Lock = std::unique_lock<std::mutex>;

using TU_i64 = int64_t;
using TU_u64 = uint64_t;

using tu_exec_func_t = void (*)(void*, TU_i64);

struct TU_ExecData {
    tu_exec_func_t exec_func;
    void *data;
    TU_i64 index;
};

#endif
