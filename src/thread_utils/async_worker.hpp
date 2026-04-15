#ifndef ASYC_WORKER
#define ASYC_WORKER
#include <thread>
#include <mutex>
#include <condition_variable>
#include <stdint.h>
#include "common.hpp"

struct AsycWorker {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    WorkerExecData exec_data;
    bool work_done, can_terminate;
};

void aw_init(AsycWorker *aw);
void aw_fini(AsycWorker *aw);
void aw_exec(AsycWorker *aw, worker_exec_func_t exec_func, void *data, i64 index);
void aw_wait(AsycWorker *aw);

#endif
