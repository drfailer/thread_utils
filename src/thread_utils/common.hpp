#ifndef THREAD_UTILS_COMMON
#define THREAD_UTILS_COMMON
#include <stdint.h>

using tu_i64 = int64_t;

using tu_exec_func_t = void (*)(void*, tu_i64);

struct TU_ExecData {
    tu_exec_func_t exec_func;
    void *data;
    tu_i64 index;
};

#endif
