#include "async_worker.hpp"
#include <cassert>

static void tu_aw_run(TU_AsyncWorker *aw);

void tu_aw_init(TU_AsyncWorker *aw) {
    aw->work_done = true;
    aw->can_terminate = false;
    aw->exec_data = {};
    aw->thread = TU_Thread(tu_aw_run, aw);
}

void tu_aw_fini(TU_AsyncWorker *aw) {
    {
        TU_Lock lck(aw->mutex);
        aw->can_terminate = true;
    }
    aw->cv.notify_one();
    if (aw->thread.joinable()) {
        aw->thread.join();
    }
}

void tu_aw_exec(TU_AsyncWorker *aw, TU_ExecProc exec_func, void *data, tu_i64 index) {
    TU_Lock lck(aw->mutex);
    assert(true == aw->work_done);
    assert(false == aw->can_terminate);
    aw->exec_data = TU_ExecData{
        .exec_func = exec_func,
        .data = data,
        .index = index,
    };
    aw->work_done = false;
    lck.unlock();
    aw->cv.notify_one();
}

void tu_aw_wait(TU_AsyncWorker *aw) {
    TU_Lock lck(aw->mutex);
    if (aw->work_done) {
        return;
    }
    aw->cv.wait(lck, [aw]{
        return aw->work_done || aw->can_terminate;
    });
}

static void tu_aw_run(TU_AsyncWorker *aw) {
    for (;;) {
        TU_Lock lck(aw->mutex);
        aw->cv.wait(lck, [aw]{
            return !aw->work_done || aw->can_terminate;
        });
        if (aw->can_terminate) {
            break;
        }
        aw->exec_data.exec_func(aw->exec_data.data, aw->exec_data.index);
        aw->work_done = true;
        lck.unlock();
        aw->cv.notify_all();
    }
}
