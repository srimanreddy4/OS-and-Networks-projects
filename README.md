# Advanced OS & Networking Projects in C++

This repository contains a collection of advanced systems and network programming projects built in C++. Each project is designed to explore fundamental operating systems and networking concepts by building high-performance, real-world applications from scratch.

---

## 1. High-Performance Memory Allocator

A thread-caching memory allocator inspired by Google's `tcmalloc`, designed to be a high-performance replacement for the standard C++ `new` and `delete` operators in multi-threaded applications.

### Key Concepts & Features:
* **Three-Tier Caching:** Implements a `ThreadCache`, `TransferCache`, and `PageHeap` to minimize lock contention and maximize scalability.
* **Concurrency:** Uses thread-local storage for lock-free "fast path" allocations and mutexes to protect the central heap.
* **Virtual Memory Management:** Acquires memory from the OS in large chunks (`Spans`) using `mmap` to reduce system call overhead.
* **Resource Management:** Features a scavenging mechanism to return idle memory from thread caches back to a central pool for other threads to use.
* **Performance:** Achieved over **2.5x the throughput** of the standard `glibc` allocator in a multi-threaded benchmark.

### How to Run:
1.  Navigate to the `mem_allocator` directory.
2.  Compile the allocator and the benchmark test:
    ```bash
    g++ -std=c++17 -O2 -pthread allocator.cpp benchmark.cpp -o benchmark
    ```
3.  Run the benchmark to see the performance results:
    ```bash
    ./benchmark
    ```

---

## 2. Container from Scratch (Mini-Docker)

A lightweight container runtime that uses low-level Linux primitives to create isolated environments for running processes.

### Key Concepts & Features:
* **Process Isolation:** Uses **PID Namespaces** (`CLONE_NEWPID`) to give the container its own process tree, making the containerized process believe it is PID 1.
* **Filesystem Isolation:** Uses **Mount Namespaces** (`CLONE_NEWNS`) and `chroot` to provide the container with its own private root filesystem.
* **Resource Limiting:** Leverages **Cgroups** to enforce strict memory limits on the container, preventing it from consuming all of the host's resources.
* **Network Isolation:** Implements a private network stack using **Network Namespaces** (`CLONE_NEWNET`), a virtual ethernet (`veth`) pair, and a network bridge to connect the container to the host and the internet.

### How to Run:
1.  Navigate to the `Container` directory.
2.  Install the necessary networking tools (if not already present):
    ```bash
    sudo apt-get update && sudo apt-get install bridge-utils
    ```
3.  Compile the container runtime:
    ```bash
    g++ -std=c++17 container.cpp -o container
    ```
4.  Run a command (like `/bin/bash`) inside the new container with root privileges:
    ```bash
    sudo ./container /bin/bash
    ```

---

*The Event-Driven Web Server project will be added here upon completion.*
