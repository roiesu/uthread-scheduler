# UThread Scheduler

This project is a user-level thread scheduler written in C. It mimics the basic behavior of an operating system scheduler by managing multiple threads entirely in user space. Instead of using kernel threads, it performs manual context switching and uses timer-based signals to implement preemptive scheduling with fixed time slices (quantums).

---

## Features

- 🧵 **Lightweight User Threads**  
  Threads are fully managed in user space, with no dependency on kernel-level threading.

- ⏱️ **Round-Robin Scheduler**  
  Ensures fair CPU time distribution by rotating through threads in fixed time slices.

- ⚡ **Preemption Support**  
  Threads can be preempted automatically when their time quantum expires, when they block themselves, or when they terminate.

- 🚫 **Blocking and Unblocking**  
  Threads can voluntarily block themselves and later be resumed by other threads.

- 😴 **Sleeping Threads**  
  Threads can enter a sleep state for a specified number of quantums before becoming ready again.

- 🧩 **C API**  
  Provides simple and intuitive functions for initializing the system, creating threads, and controlling their state.

---

## 📁 Project Structure

```plaintext
.
├── include/
│   └── uthreads.h         # Public API
├── src/
│   └── uthreads.c         # Scheduler and core logic
├── tests/
│   ├── main.c             # Sample/test program
├── Makefile               # Build script
├── .gitignore             # Ignored files
└── README.md              
```

---

## 🔧 Building

You can compile the project using `make`:

```bash
make
```

To clean up object files and the binary:

```bash
make clean
```

---

## ▶️ Running

After compilation, run the test program:

```bash
./test_threads
```

The test creates two threads:
1. Thread 1 blocks itself, then gets unblocked and sleeps for 2 quantums before exiting.
2. Thread 2 unblocks thread 1 and exits.

The main thread (TID 0) keeps running until all other threads have terminated.


