#define _GNU_SOURCE
#include "uthreads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/time.h>

#ifdef __x86_64__
typedef unsigned long addr_t;
#define JMPBUF_SP 6
#define JMPBUF_PC 7
#else
typedef unsigned int addr_t;
#define JMPBUF_SP 4
#define JMPBUF_PC 5
#endif

typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    SLEEPING
} thread_state;

typedef struct {
    int id;
    thread_state state;
    sigjmp_buf context;
    char stack[UTHREAD_STACK_BYTES];
    int quantums_left_to_sleep;
    int is_active;
    uthread_entry entry_func;
} thread_t;

typedef struct {
    int queue[UTHREAD_MAX_THREADS];
    int size;
} ready_queue_t;

typedef struct {
    thread_t threads[UTHREAD_MAX_THREADS];
    ready_queue_t ready_queue;
    int current_tid;
    int quantum_usecs;
} scheduler_state_t;

static scheduler_state_t sched;

static addr_t translate_addr(addr_t addr) {
    addr_t ret;
#ifdef __x86_64__
    __asm__ volatile("xor    %%fs:0x30,%0\n"
                     "rol    $0x11,%0\n"
                     : "=g" (ret)
                     : "0" (addr));
#else
    __asm__ volatile("xor    %%gs:0x18,%0\n"
                     "rol    $0x9,%0\n"
                     : "=g" (ret)
                     : "0" (addr));
#endif
    return ret;
}

static void enqueue_ready(int tid) {
    if (sched.ready_queue.size < UTHREAD_MAX_THREADS) {
        sched.ready_queue.queue[sched.ready_queue.size++] = tid;
    }
}

void timer_handler(int sig);

static void thread_start() {
    int tid = sched.current_tid;
    if (sched.threads[tid].entry_func) {
        sched.threads[tid].entry_func();
    }
    uthread_exit(tid);
}

static void schedule_next() {
    int prev_tid = sched.current_tid;
    int found_active = 0;

    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (sched.threads[i].state == SLEEPING && sched.threads[i].quantums_left_to_sleep > 0) {
            sched.threads[i].quantums_left_to_sleep--;
            if (sched.threads[i].quantums_left_to_sleep == 0) {
                sched.threads[i].state = READY;
                enqueue_ready(i);
            }
        }
        if (sched.threads[i].is_active) {
            found_active = 1;
        }
    }

    if (sched.ready_queue.size == 0 && !found_active) {
        printf("[System] All other threads terminated. Exiting.\n");
        exit(0);
    }

    if (sched.ready_queue.size == 0) {
        return;
    }

    if (sigsetjmp(sched.threads[prev_tid].context, 1) != 0) {
        return;
    }

    int next_tid = sched.ready_queue.queue[0];
    for (int i = 1; i < sched.ready_queue.size; ++i) {
        sched.ready_queue.queue[i - 1] = sched.ready_queue.queue[i];
    }
    sched.ready_queue.size--;

    if (sched.threads[prev_tid].state == RUNNING) {
        sched.threads[prev_tid].state = READY;
    }
    if (sched.threads[prev_tid].state == READY) {
        enqueue_ready(prev_tid);
    }

    sched.threads[next_tid].state = RUNNING;
    sched.current_tid = next_tid;

    printf("Switching from TID %d to TID %d\n", prev_tid, next_tid);
    siglongjmp(sched.threads[next_tid].context, 1);
}

void timer_handler(int sig) {
    if (sig == SIGVTALRM) {
        printf(">>> Timer tick received (SIGVTALRM)\n");
        schedule_next();
    }
}

int uthread_system_init(int quantum_usecs) {
    if (quantum_usecs <= 0) {
        fprintf(stderr, "[Error] Invalid quantum duration.\n");
        return -1;
    }

    memset(&sched, 0, sizeof(sched));
    sched.quantum_usecs = quantum_usecs;

    sched.threads[0].id = 0;
    sched.threads[0].state = RUNNING;
    sched.threads[0].is_active = 1;
    sched.current_tid = 0;

    if (sigsetjmp(sched.threads[0].context, 1) != 0) {
        return 0;
    }

    struct sigaction sa;
    sa.sa_handler = &timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGVTALRM, &sa, NULL) < 0) {
        perror("sigaction failed");
        return -1;
    }

    struct itimerval timer;
    timer.it_value.tv_sec = sched.quantum_usecs / 1000000;
    timer.it_value.tv_usec = sched.quantum_usecs % 1000000;
    timer.it_interval = timer.it_value;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        perror("setitimer failed");
        return -1;
    }

    return 0;
}

int uthread_create(uthread_entry entry_func) {
    if (!entry_func) {
        fprintf(stderr, "[Error] NULL entry function.\n");
        return -1;
    }

    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (!sched.threads[i].is_active) {
            tid = i;
            break;
        }
    }

    if (tid == -1) {
        fprintf(stderr, "[Error] No available TID slots.\n");
        return -1;
    }

    sched.threads[tid].id = tid;
    sched.threads[tid].state = READY;
    sched.threads[tid].quantums_left_to_sleep = 0;
    sched.threads[tid].is_active = 1;
    sched.threads[tid].entry_func = entry_func;

    addr_t sp = (addr_t)sched.threads[tid].stack + UTHREAD_STACK_BYTES - sizeof(addr_t);
    addr_t pc = (addr_t)thread_start;

    if (sigsetjmp(sched.threads[tid].context, 1) != 0) {
        fprintf(stderr, "[Error] sigsetjmp failed for TID %d.\n", tid);
        return -1;
    }

    sched.threads[tid].context[0].__jmpbuf[JMPBUF_SP] = translate_addr(sp);
    sched.threads[tid].context[0].__jmpbuf[JMPBUF_PC] = translate_addr(pc);
    sigemptyset(&sched.threads[tid].context[0].__saved_mask);

    enqueue_ready(tid);
    return tid;
}

int uthread_exit(int tid) {
    if (tid < 0 || tid >= UTHREAD_MAX_THREADS) {
        fprintf(stderr, "[Error] Invalid TID for exit.\n");
        return -1;
    }
    if (!sched.threads[tid].is_active) {
        fprintf(stderr, "[Error] Exiting inactive thread.\n");
        return -1;
    }

    if (tid == 0) {
        exit(0);
    }

    if (tid == sched.current_tid) {
        sched.threads[tid].is_active = 0;
        sched.threads[tid].state = -1;
        schedule_next();
    }

    sched.threads[tid].is_active = 0;
    sched.threads[tid].state = -1;

    for (int i = 0; i < sched.ready_queue.size; ++i) {
        if (sched.ready_queue.queue[i] == tid) {
            for (int j = i; j < sched.ready_queue.size - 1; ++j) {
                sched.ready_queue.queue[j] = sched.ready_queue.queue[j + 1];
            }
            sched.ready_queue.size--;
            break;
        }
    }

    return 0;
}

int uthread_block(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS) {
        fprintf(stderr, "[Error] Invalid TID for block.\n");
        return -1;
    }
    if (!sched.threads[tid].is_active) {
        fprintf(stderr, "[Error] Blocking inactive thread.\n");
        return -1;
    }
    if (sched.threads[tid].state == BLOCKED) {
        fprintf(stderr, "[Error] Thread already blocked.\n");
        return -1;
    }

    sched.threads[tid].state = BLOCKED;

    for (int i = 0; i < sched.ready_queue.size; ++i) {
        if (sched.ready_queue.queue[i] == tid) {
            for (int j = i; j < sched.ready_queue.size - 1; ++j) {
                sched.ready_queue.queue[j] = sched.ready_queue.queue[j + 1];
            }
            sched.ready_queue.size--;
            break;
        }
    }

    if (tid == sched.current_tid) {
        schedule_next();
    }

    return 0;
}

int uthread_unblock(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS) {
        fprintf(stderr, "[Error] Invalid TID for unblock.\n");
        return -1;
    }
    if (!sched.threads[tid].is_active) {
        fprintf(stderr, "[Error] Unblocking inactive thread.\n");
        return -1;
    }
    if (sched.threads[tid].state != BLOCKED) {
        fprintf(stderr, "[Error] Thread is not blocked.\n");
        return -1;
    }

    sched.threads[tid].state = READY;
    enqueue_ready(tid);
    return 0;
}

int uthread_sleep_quantums(int num_quantums) {
    if (num_quantums <= 0) {
        fprintf(stderr, "[Error] Invalid sleep duration.\n");
        return -1;
    }
    if (sched.current_tid == 0) {
        fprintf(stderr, "[Error] Main thread cannot sleep.\n");
        return -1;
    }

    sched.threads[sched.current_tid].state = SLEEPING;
    sched.threads[sched.current_tid].quantums_left_to_sleep = num_quantums;

    schedule_next();
    return 0;
}
