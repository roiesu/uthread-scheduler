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

static thread_t threads[UTHREAD_MAX_THREADS];
static int ready_queue[UTHREAD_MAX_THREADS];
static int ready_queue_size = 0;
static int current_tid = 0;
static int g_quantum_usecs = 0;

static void enqueue_ready(int tid) {
    if (ready_queue_size < UTHREAD_MAX_THREADS) {
        ready_queue[ready_queue_size++] = tid;
    }
}

void timer_handler(int sig);

static void thread_start() {
    int tid = current_tid;
    if (threads[tid].entry_func) {
        threads[tid].entry_func();
    }
    uthread_exit(tid);
}

int uthread_system_init(int quantum_usecs) {
    if (quantum_usecs <= 0) {
        return -1;
    }

    g_quantum_usecs = quantum_usecs;

    memset(threads, 0, sizeof(threads));
    memset(ready_queue, -1, sizeof(ready_queue));
    ready_queue_size = 0;

    threads[0].id = 0;
    threads[0].state = RUNNING;
    threads[0].quantums_left_to_sleep = 0;
    threads[0].is_active = 1;
    current_tid = 0;

    if (sigsetjmp(threads[0].context, 1) != 0) {
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
    timer.it_value.tv_sec = g_quantum_usecs / 1000000;
    timer.it_value.tv_usec = g_quantum_usecs % 1000000;
    timer.it_interval = timer.it_value;

    if (setitimer(ITIMER_VIRTUAL, &timer, NULL) < 0) {
        perror("setitimer failed");
        return -1;
    }

    return 0;
}

int uthread_create(uthread_entry entry_func) {
    if (!entry_func) {
        return -1;
    }

    int tid = -1;
    for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
        if (!threads[i].is_active) {
            tid = i;
            break;
        }
    }

    if (tid == -1) {
        return -1;
    }

    threads[tid].id = tid;
    threads[tid].state = READY;
    threads[tid].quantums_left_to_sleep = 0;
    threads[tid].is_active = 1;
    threads[tid].entry_func = entry_func;

    addr_t sp = (addr_t)threads[tid].stack + UTHREAD_STACK_BYTES - sizeof(addr_t);
    addr_t pc = (addr_t)thread_start;

    if (sigsetjmp(threads[tid].context, 1) != 0) {
        return -1;
    }

    threads[tid].context[0].__jmpbuf[JMPBUF_SP] = translate_addr(sp);
    threads[tid].context[0].__jmpbuf[JMPBUF_PC] = translate_addr(pc);
    sigemptyset(&threads[tid].context[0].__saved_mask);

    enqueue_ready(tid);
    return tid;
}

static void schedule_next() {
    int prev_tid = current_tid;

    for (int i = 0; i < UTHREAD_MAX_THREADS; ++i) {
        if (threads[i].state == SLEEPING && threads[i].quantums_left_to_sleep > 0) {
            threads[i].quantums_left_to_sleep--;
            if (threads[i].quantums_left_to_sleep == 0) {
                threads[i].state = READY;
                enqueue_ready(i);
            }
        }
    }

    if (sigsetjmp(threads[prev_tid].context, 1) != 0) {
        return;
    }

    if (ready_queue_size == 0) {
        int other_alive = 0;
        for (int i = 1; i < UTHREAD_MAX_THREADS; ++i) {
            if (threads[i].is_active) {
                other_alive = 1;
                break;
            }
        }
        if (!other_alive) {
            printf("[System] All other threads terminated. Exiting.\n");
            exit(0);
        }
        return;
    }

    int next_tid = ready_queue[0];
    for (int i = 1; i < ready_queue_size; ++i) {
        ready_queue[i - 1] = ready_queue[i];
    }
    ready_queue_size--;

    if (threads[prev_tid].state == RUNNING) {
        threads[prev_tid].state = READY;
    }
    if (threads[prev_tid].state == READY) {
        enqueue_ready(prev_tid);
    }

    threads[next_tid].state = RUNNING;
    current_tid = next_tid;

    printf("Switching from TID %d to TID %d\n", prev_tid, next_tid);
    siglongjmp(threads[next_tid].context, 1);
}

void timer_handler(int sig) {
    if (sig == SIGVTALRM) {
        printf(">>> Timer tick received (SIGVTALRM)\n");
        schedule_next();
    }
}

int uthread_exit(int tid) {
    if (tid < 0 || tid >= UTHREAD_MAX_THREADS || !threads[tid].is_active) {
        return -1;
    }

    if (tid == 0) {
        exit(0);
    }

    if (tid == current_tid) {
        threads[tid].is_active = 0;
        threads[tid].state = -1;
        schedule_next();
    }

    threads[tid].is_active = 0;
    threads[tid].state = -1;

    for (int i = 0; i < ready_queue_size; ++i) {
        if (ready_queue[i] == tid) {
            for (int j = i; j < ready_queue_size - 1; ++j) {
                ready_queue[j] = ready_queue[j + 1];
            }
            ready_queue_size--;
            break;
        }
    }

    return 0;
}

int uthread_block(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS) return -1;
    if (!threads[tid].is_active) return -1;
    if (threads[tid].state == BLOCKED) return -1;

    threads[tid].state = BLOCKED;

    for (int i = 0; i < ready_queue_size; ++i) {
        if (ready_queue[i] == tid) {
            for (int j = i; j < ready_queue_size - 1; ++j) {
                ready_queue[j] = ready_queue[j + 1];
            }
            ready_queue_size--;
            break;
        }
    }

    if (tid == current_tid) {
        schedule_next();
    }

    return 0;
}

int uthread_unblock(int tid) {
    if (tid <= 0 || tid >= UTHREAD_MAX_THREADS) return -1;
    if (!threads[tid].is_active) return -1;
    if (threads[tid].state != BLOCKED) return -1;

    threads[tid].state = READY;
    enqueue_ready(tid);
    return 0;
}

int uthread_sleep_quantums(int num_quantums) {
    if (num_quantums <= 0 || current_tid == 0) {
        return -1;
    }

    threads[current_tid].state = SLEEPING;
    threads[current_tid].quantums_left_to_sleep = num_quantums;

    schedule_next();

    return 0;
}
