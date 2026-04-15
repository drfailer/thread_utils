#include "async_worker.hpp"

static void aw_run(AsycWorker *aw);

void aw_init(AsycWorker *aw) {
    aw->thread = std::thread(aw_run, aw);
    aw->work_done = true;
    aw->can_terminate = false;
}

void aw_fini(AsycWorker *aw) {
    {
        std::unique_lock<std::mutex> lck(aw->mutex);
        aw->can_terminate = true;
    }
    aw->cv.notify_one();
    if (aw->thread.joinable()) {
        aw->thread.join();
    }
}

void aw_exec(AsycWorker *aw, async_worker_exec_t exec, void *data) {
    std::unique_lock<std::mutex> lck(aw->mutex);
    assert(true == aw->work_done);
    assert(false == aw->can_terminate);
    aw->data = data;
    aw->exec = exec;
    aw->work_done = false;
    lck.unlock();
    aw->cv.notify_one();
}

void aw_wait(AsycWorker *aw) {
    if (aw->work_done) {
        return;
    }
    std::unique_lock<std::mutex> lck(aw->mutex);
    aw->cv.wait(lck, [aw]{
        return aw->work_done || aw->can_terminate;
    });
}

static void aw_run(AsycWorker *aw) {
    for (;;) {
        std::unique_lock<std::mutex> lck(aw->mutex);
        aw->cv.wait(lck, [aw]{
            return !aw->work_done || aw->can_terminate;
        });
        if (aw->can_terminate) {
            break;
        }
        aw->exec(aw->data);
        aw->work_done = true;
        lck.unlock();
        aw->cv.notify_one();
    }
}
