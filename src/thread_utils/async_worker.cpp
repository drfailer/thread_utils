#include "async_worker.hpp"
#include <cassert>

static void tu_aw_run(TU_AsycWorker *aw);

void tu_aw_init(TU_AsycWorker *aw) {
    aw->thread = std::thread(tu_aw_run, aw);
    aw->work_done = true;
    aw->can_terminate = false;
    aw->exec_data = {};
}

void tu_aw_fini(TU_AsycWorker *aw) {
    {
        std::unique_lock<std::mutex> lck(aw->mutex);
        aw->can_terminate = true;
    }
    aw->cv.notify_one();
    if (aw->thread.joinable()) {
        aw->thread.join();
    }
}

void tu_aw_exec(TU_AsycWorker *aw, tu_worker_exec_func_t exec_func, void *data, tu_i64 index) {
    std::unique_lock<std::mutex> lck(aw->mutex);
    assert(true == aw->work_done);
    assert(false == aw->can_terminate);
    aw->exec_data = TU_WorkerExecData{
        .exec_func = exec_func,
        .data = data,
        .index = index,
    };
    aw->work_done = false;
    lck.unlock();
    aw->cv.notify_one();
}

void tu_aw_wait(TU_AsycWorker *aw) {
    if (aw->work_done) {
        return;
    }
    std::unique_lock<std::mutex> lck(aw->mutex);
    aw->cv.wait(lck, [aw]{
        return aw->work_done || aw->can_terminate;
    });
}

static void tu_aw_run(TU_AsycWorker *aw) {
    for (;;) {
        std::unique_lock<std::mutex> lck(aw->mutex);
        aw->cv.wait(lck, [aw]{
            return !aw->work_done || aw->can_terminate;
        });
        if (aw->can_terminate) {
            break;
        }
        aw->exec_data.exec_func(aw->exec_data.data, aw->exec_data.index);
        aw->work_done = true;
        lck.unlock();
        aw->cv.notify_one();
    }
}
