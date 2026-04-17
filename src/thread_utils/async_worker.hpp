#ifndef ASYC_WORKER
#define ASYC_WORKER
#include "common.hpp"

struct TU_AsycWorker {
    TU_Thread thread;
    TU_Mutex mutex;
    TU_Cond cv;
    TU_ExecData exec_data;
    volatile bool work_done, can_terminate;
};

void tu_aw_init(TU_AsycWorker *aw);
void tu_aw_fini(TU_AsycWorker *aw);
void tu_aw_exec(TU_AsycWorker *aw, tu_exec_func_t exec_func, void *data, TU_i64 index);
void tu_aw_wait(TU_AsycWorker *aw);

#endif
