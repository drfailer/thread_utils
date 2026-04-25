#ifndef ASYC_WORKER
#define ASYC_WORKER
#include "common.hpp"

struct TU_AsyncWorker {
    TU_Thread thread;
    TU_Mutex mutex;
    TU_Cond cv;
    TU_ExecData exec_data;
    volatile bool work_done, can_terminate;
};

void tu_aw_init(TU_AsyncWorker *aw);
void tu_aw_fini(TU_AsyncWorker *aw);
void tu_aw_exec(TU_AsyncWorker *aw, TU_ExecProc exec_func, void *data, TU_i64 index);
void tu_aw_wait(TU_AsyncWorker *aw);

#endif
