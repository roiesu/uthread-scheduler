#include "uthreads.h"
#include <stdio.h>
#include <unistd.h>

void thread_func1() {
    printf("[TID 1] Start\n");
    printf("[TID 1] Blocking self\n");
    uthread_block(1);
    printf("[TID 1] Resumed after unblock\n");
    printf("[TID 1] Sleeping for 2 quantums\n");
    uthread_sleep_quantums(2);
    printf("[TID 1] Woke up from sleep\n");
    printf("[TID 1] Exiting\n");
    uthread_exit(1);
}

void thread_func2() {
    printf("[TID 2] Start\n");
    printf("[TID 2] Unblocking TID 1\n");
    uthread_unblock(1);
    printf("[TID 2] Exiting\n");
    uthread_exit(2);
}

int main() {
    if (uthread_system_init(100000) == -1) {
        fprintf(stderr, "Failed to initialize uthreads system\n");
        return 1;
    }

    int tid1 = uthread_create(thread_func1);
    int tid2 = uthread_create(thread_func2);

    if (tid1 == -1 || tid2 == -1) {
        fprintf(stderr, "Failed to create threads\n");
        return 1;
    }

    printf("[Main] Created threads %d and %d\n", tid1, tid2);

    // keep the main thread running to allow SIGVTALRM to be triggered
    while (1) {
        for (volatile int i = 0; i < 1000000; ++i); // busy loop to consume CPU
    }

    return 0;
}
