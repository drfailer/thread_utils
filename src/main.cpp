#include <iostream>
#include <stdio.h>
#include <thread>
#include <chrono>
#include "thread_utils/thread_utils.hpp"
#include "defer.hpp"

void test_execute(void*, i64) {
    using namespace std::literals::chrono_literals;
    printf("start work...\n");
    std::this_thread::sleep_for(5s);
    printf("end work\n");
}

void test_async_worker() {
    AsycWorker worker;
    aw_init(&worker);
    defer(aw_fini(&worker));

    printf("begin test worker.\n");
    printf("run exec\n");
    aw_exec(&worker, test_execute, nullptr, 0);
    {
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(3s);
    }
    printf("wait for the worker...\n");
    aw_wait(&worker);
    printf("end test worker.\n");
}

int main(int , char **) {
    test_async_worker();
    return 0;
}
