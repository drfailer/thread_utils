#include <iostream>
#include <stdio.h>
#include <thread>
#include <chrono>
#include "thread_utils/thread_utils.hpp"
#include "thread_utils/data_structures/lock_free_queue.hpp"
#include "defer.hpp"
#include "timer.hpp"

void test_execute(void*, TU_i64) {
    using namespace std::literals::chrono_literals;
    printf("start work...\n");
    std::this_thread::sleep_for(5s);
    printf("end work\n");
}

void test_async_worker() {
    TU_AsyncWorker worker;
    tu_aw_init(&worker);
    defer(tu_aw_fini(&worker));

    timer_start(test_async_worker);
    printf("begin test worker.\n");
    printf("run exec\n");
    tu_aw_exec(&worker, test_execute, nullptr, 0);
    {
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(3s);
    }
    printf("wait for the worker...\n");
    tu_aw_wait(&worker);
    printf("end test worker.\n");
    timer_end(test_async_worker);

    timer_report(test_async_worker)
    printf("\n");
}

void test_thread_pool() {
    constexpr size_t POOL_SIZE = 40;
    TU_ThreadPool pool;
    tu_tp_init(&pool, POOL_SIZE);
    defer(tu_tp_fini(&pool));
    TU_OperationHandle op;

    timer_start(test_thread_pool);
    printf("begin test thread pool.\n\n");

    printf("run exec single job\n");
    timer_start(exec_single_job);
    tu_tp_exec(&pool, test_execute, nullptr, 0, &op);
    {
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(3s);
    }
    printf("wait for single job...\n");
    tu_tp_op_wait(&op);
    timer_end(exec_single_job);
    printf("end single job\n");

    timer_report(exec_single_job);

    printf("\n");

    constexpr size_t JOB_COUNT = 3*POOL_SIZE;
    TU_ExecData jobs[JOB_COUNT];
    for (size_t i = 0; i < JOB_COUNT; ++i) {
        jobs[i].exec_func = test_execute;
    }
    printf("run launch with %ld jobs...\n", JOB_COUNT);
    timer_start(exec_n_jobs);
    tu_tp_lauch(&pool, jobs, JOB_COUNT, &op);
    {
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(3s);
    }
    printf("wait for %ld job...\n", JOB_COUNT);
    tu_tp_op_wait(&op);
    timer_end(exec_n_jobs);

    printf("end %ld jobs...\n", JOB_COUNT);

    timer_report(exec_n_jobs);

    printf("\nend test thread pool.\n");
    timer_end(test_thread_pool);

    timer_report(test_thread_pool)
    printf("\n");

    tu_duration_print("enqueue time", static_cast<TU_Duration>(pool.enqueue_dur));
    tu_duration_print("dequeue time", static_cast<TU_Duration>(pool.dequeue_dur));
}

void test_lock_free_queue() {
    TU_LockFreeQueue<int> queue;

    printf("single thread push/pop in lfq:\n");
    for (int i = 0; i < 10; ++i) {
        queue.push(i);
    }
    for (int i = 0; i < 10; ++i) {
        int value;
        if (!queue.pop(&value)) {
            printf("failed to pop at %d\n", i);
        }
        if (value != i) {
            printf("poped %d expected %d\n", value, i);
        }
    }
}

int main(int , char **) {
    // test_async_worker();
    test_lock_free_queue();
    test_thread_pool();
    return 0;
}
