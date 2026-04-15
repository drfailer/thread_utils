#ifndef THREAD_UTILS_COMMON
#define THREAD_UTILS_COMMON
#include <stdint.h>

using i64 = int64_t;

using worker_exec_func_t = void (*)(void*, i64);

struct WorkerExecData {
    worker_exec_func_t exec_func;
    void *data;
    i64 index;
};

#endif
