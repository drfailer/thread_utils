#ifndef ASYC_WORKER
#define ASYC_WORKER
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdint.h>
#include "common.hpp"

struct TU_AsycWorker {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    TU_WorkerExecData exec_data;
    bool work_done, can_terminate;
};

void tu_aw_init(TU_AsycWorker *aw);
void tu_aw_fini(TU_AsycWorker *aw);
void tu_aw_exec(TU_AsycWorker *aw, tu_worker_exec_func_t exec_func, void *data, tu_i64 index);
void tu_aw_wait(TU_AsycWorker *aw);

#endif
