#ifndef ASYC_WORKER
#define ASYC_WORKER
#include <thread>
#include <mutex>
#include <condition_variable>

using async_worker_exec_t = void (*)(void*);

struct AsycWorker {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    void *data;
    async_worker_exec_t exec;
    bool work_done, can_terminate;
};

void aw_init(AsycWorker *aw);
void aw_fini(AsycWorker *aw);
void aw_exec(AsycWorker *aw, async_worker_exec_t exec, void *data);
void aw_wait(AsycWorker *aw);

#endif
