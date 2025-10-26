# C++ Systems and Networking Primitives

**[August 2025]** | Advanced Operating Systems & Networking Projects

This repository showcases three deep-dive projects into operating systems and network programming, built from scratch in C++ using low-level Linux primitives. Each project demonstrates mastery of systems programming concepts including concurrency, virtual memory management, process isolation, and high-performance I/O.

---

## 📋 Table of Contents
1. [High-Performance Memory Allocator](#1-high-performance-memory-allocator)
2. [Lightweight Container Runtime (Mini-Docker)](#2-lightweight-container-runtime-mini-docker)
3. [Event-Driven Web Server](#3-event-driven-web-server)

---

## 1. High-Performance Memory Allocator

A production-grade, thread-caching memory allocator inspired by Google's **tcmalloc**, designed to replace C++'s standard `new`/`delete` operators in multi-threaded applications.

### 🎯 Project Goal
Build a memory allocator that significantly outperforms the standard `glibc` allocator by minimizing lock contention and reducing expensive system calls in multi-threaded workloads.

### 🏗️ Architecture

The allocator implements a **three-tier caching hierarchy** to optimize for different allocation patterns:

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Thread                        │
└────────────────────────┬────────────────────────────────────┘
                         │
                ┌────────▼────────┐
                │  ThreadCache    │ ◄─── Lock-free "fast path"
                │  (TLS)          │      for small allocations
                └────────┬────────┘
                         │
                ┌────────▼────────┐
                │ TransferCache   │ ◄─── Batch transfers between
                │  (Mutex)        │      thread and central heap
                └────────┬────────┘
                         │
                ┌────────▼────────┐
                │   PageHeap      │ ◄─── Manages memory from OS
                │  (mmap/munmap)  │      in large chunks (spans)
                └─────────────────┘
```

#### Key Components:

**1. ThreadCache (Per-Thread, Lock-Free)**
- Maintains per-size-class free lists (8B, 16B, 32B, ..., 1KB)
- Uses thread-local storage (`thread_local`) for zero-contention access
- Fast path allocation: simple pointer pop from free list
- Implements **scavenging**: returns idle memory to TransferCache when threshold exceeded

**2. TransferCache (Central, Mutex-Protected)**
- Acts as a middle layer between threads and the page heap
- Performs **batch transfers** (default: 32 objects) to amortize lock overhead
- Reduces contention by allowing threads to acquire/release memory in bulk
- Each size class has its own transfer cache to minimize lock conflicts

**3. PageHeap (Global, Mutex-Protected)**
- Manages large memory regions called **Spans** (multiple 4KB pages)
- Acquires memory from OS using `mmap()` in large chunks
- Maintains a `page_map` for O(1) span lookup during deallocation
- Handles large allocations (>1KB) directly without fragmentation

### 🔑 Key Concepts Demonstrated

#### Virtual Memory Management
- Direct interaction with OS via `mmap()`/`munmap()` system calls
- Memory acquired in page-aligned chunks (4KB pages)
- Reduces kernel transitions: one `mmap()` can serve hundreds of allocations

#### Concurrency & Synchronization
- **Fine-grained locking**: Separate mutexes per size class in TransferCache
- **Thread-local storage**: Eliminates cache coherence overhead between cores

#### Memory Fragmentation Control
- **Size classes** eliminate external fragmentation for small objects
- **Span-based allocation** groups objects by size for better cache locality
- **Scavenging mechanism** prevents unbounded thread cache growth

#### Performance Optimization Techniques
- **Batch transfers** reduce mutex acquisition frequency
- **Pre-allocated span metadata** avoids recursive allocation during bootstrap
- **Edge-triggered polling** style: check once, return fast

### 📊 Performance Results

Benchmark: 4 threads, 1 million allocations/deallocations per thread

| Allocator | Throughput | Speedup |
|-----------|-----------|---------|
| Standard `glibc` malloc | 1.0x (baseline) | - |
| **Custom tcmalloc-style** | **2.5x** | 150% faster |




```

### 📁 Project Structure
```
mem_allocator/
├── allocator.h         # Interface and data structures
├── allocator.cpp       # Core implementation
└── benchmark.cpp       # Multi-threaded performance test
```


---

## 2. Lightweight Container Runtime (Mini-Docker)

A container runtime built from scratch using low-level Linux kernel primitives, demonstrating the core concepts behind Docker, Kubernetes, and other containerization platforms.

### 🎯 Project Goal
Understand containerization at the OS level by implementing process, filesystem, and network isolation without relying on high-level container frameworks.

### 🏗️ Architecture

```
┌───────────────────────────────────────────────────────────┐
│                      Host System                           │
│  ┌─────────────────────────────────────────────────────┐  │
│  │           Containerized Process Tree                 │  │
│  │  ┌──────────────────────────────────────────────┐   │  │
│  │  │  PID 1: /bin/bash (inside container)         │   │  │
│  │  │  - Isolated PID namespace                    │   │  │
│  │  │  - Private root filesystem via chroot        │   │  │
│  │  │  - Virtual network interface (veth1)         │   │  │
│  │  │  - Memory limited to 100MB (cgroup)          │   │  │
│  │  └──────────────────────────────────────────────┘   │  │
│  └─────────────────────────────────────────────────────┘  │
│                                                             │
│  Network Bridge (bridge0): 10.0.0.1/24                     │
│         │                                                   │
│    veth0 ├─────── veth1 (in container: 10.0.0.2/24)       │
│                                                             │
│  Cgroup Hierarchy: /sys/fs/cgroup/memory/my_container/     │
└───────────────────────────────────────────────────────────┘
```

### 🔑 Key Concepts & Implementation

#### 1. Process Isolation with PID Namespaces
**Concept**: Give each container its own view of the process tree.

```cpp
// Key system call
clone(child_func, stack_top, CLONE_NEWPID | SIGCHLD, args);
```

**What it does**:
- Containerized process becomes **PID 1** inside its namespace
- Cannot see or signal host processes
- Replicates the isolation model of VMs at the process level

*

#### 2. Filesystem Isolation with Mount Namespaces & chroot
**Concept**: Provide a private root filesystem visible only to the container.

```cpp
// Create private mount namespace
clone(..., CLONE_NEWNS, ...);

// Change root directory
chroot("/tmp/my_root");
chdir("/");
```

**What it does**:
- Container sees `/tmp/my_root` as `/`
- Cannot access host filesystem outside the new root
- Each container can have different OS userland (e.g., Ubuntu vs Alpine)

**Setup**:
```bash
# Copy essential binaries and their dependencies
cp /bin/bash /tmp/my_root/bin/
ldd /bin/bash | awk '{print $3}' | xargs -I '{}' cp '{}' /tmp/my_root/lib/
```

#### 3. Resource Limiting with Cgroups
**Concept**: Enforce hard limits on memory, CPU, and I/O.

```cpp
// Create cgroup and set memory limit
mkdir("/sys/fs/cgroup/memory/my_container");
echo "104857600" > "/sys/fs/cgroup/memory/my_container/memory.limit_in_bytes";
echo child_pid > "/sys/fs/cgroup/memory/my_container/cgroup.procs";
```

**What it does**:
- Limits container to **100MB of RAM**
- Kernel sends `SIGKILL` (OOM) if limit exceeded
- Prevents "noisy neighbor" resource exhaustion

**Demo**:
```bash
# Inside container - try to allocate 150MB
$ python3 -c "x = 'a' * (150 * 1024 * 1024)"
Killed  # OOM by cgroup!
```

#### 4. Network Isolation with Network Namespaces
**Concept**: Give each container a private network stack.

```cpp
// Create network namespace
clone(..., CLONE_NEWNET, ...);

// Setup virtual ethernet pair
ip link add veth0 type veth peer name veth1
ip link set veth1 netns <container_pid>

// Create bridge and NAT
brctl addbr bridge0
ip addr add 10.0.0.1/24 dev bridge0
iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -j MASQUERADE
```

**Network topology**:
```
Internet
    ↕
Host eth0 (NAT via iptables)
    ↕
bridge0 (10.0.0.1) ←→ veth0 ↔ veth1 (10.0.0.2, in container)
```

**What it does**:
- Container gets its own IP address (10.0.0.2)
- Can communicate with host and internet via NAT
- Isolated from other containers' network traffic



### 📁 Project Structure
```
Container/
└── container.cpp       # Complete container runtime implementation
```

### 🛠️ Technical Challenges Solved

1. **Namespace Setup Timing**: Network namespace setup requires parent coordination
   - Solution: `sleep(1)` in child to let parent configure veth pair

2. **DNS Resolution**: Container needs `/etc/resolv.conf` for name resolution
   - Solution: Copy host's DNS config into container root

3. **Library Dependencies**: Binaries fail without shared libraries
   - Solution: `ldd` parsing and recursive library copying

4. **Graceful Cleanup**: Leftover cgroups and network devices persist
   - Solution: Cleanup hooks in parent after child exits



## 3. Event-Driven Web Server

A high-performance HTTP/1.1 web server built from scratch using **non-blocking I/O** and the **epoll** event notification API, capable of handling thousands of concurrent connections on a single thread.

### 🎯 Project Goal
Build a production-ready web server that demonstrates advanced I/O multiplexing techniques and HTTP protocol implementation, achieving C10K problem performance targets.

### 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   Client Connections (1000s)                 │
└───────┬──────────┬──────────┬──────────┬──────────┬─────────┘
        │          │          │          │          │
   ┌────▼────┐┌───▼────┐┌───▼────┐┌───▼────┐┌───▼────┐
   │ Socket  ││ Socket ││ Socket ││ Socket ││ Socket │
   │ (fd=5)  ││ (fd=6) ││ (fd=7) ││ (fd=8) ││ (fd=9) │
   └────┬────┘└───┬────┘└───┬────┘└───┬────┘└───┬────┘
        └──────────┴──────────┴──────────┴────────┘
                            │
                ┌───────────▼───────────┐
                │   epoll Instance      │
                │  (Kernel Event Loop)  │ ◄─── Single syscall
                └───────────┬───────────┘      monitors 1000s
                            │                  of sockets
                ┌───────────▼───────────┐
                │   Main Thread         │
                │   (Event Dispatcher)  │
                └───────────┬───────────┘
                            │
                ┌───────────▼───────────┐
                │  Thread-Safe Queue    │
                └───────────┬───────────┘
                            │
        ┌───────────────────┼───────────────────┐
        │                   │                   │
   ┌────▼────┐         ┌───▼────┐         ┌───▼────┐
   │ Worker  │         │ Worker │         │ Worker │
   │ Thread 1│         │ Thread2│         │ Thread3│
   └────┬────┘         └───┬────┘         └───┬────┘
        │                  │                   │
        └──────────────────┴───────────────────┘
                           │
                 HTTP Request Processing
                 File Serving from Disk
```

### 🔑 Key Concepts & Implementation

#### 1. Non-Blocking I/O with epoll (Edge-Triggered)

**The C10K Problem**: Traditional `select()`/`poll()` scale poorly beyond ~1000 connections.

**Why epoll?**
- **O(1) complexity**: Adding/removing sockets is constant time
- **Edge-triggered mode**: Notifications only on state change (reduces spurious wakeups)
- **Kernel-space event queue**: No need to copy entire fd set on every call

```cpp
// Setup epoll
int epoll_fd = epoll_create1(0);
epoll_event event;
event.events = EPOLLIN | EPOLLET;  // Read events, edge-triggered
event.data.fd = client_fd;
epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);

// Main event loop
while (true) {
    int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    for (int i = 0; i < num_events; i++) {
        // Dispatch ready sockets to worker threads
        task_queue.push(events[i].data.fd);
    }
}
```

**How it works**:
1. **Monitoring**: `epoll_wait()` blocks until any socket becomes ready
2. **Notification**: Kernel wakes up program when data arrives
3. **Dispatch**: Main thread pushes ready socket to worker thread queue
4. **Non-blocking read**: Worker calls `read()` until `EAGAIN` (no more data)

#### 2. Multi-Threaded Request Handling

**Thread Pool Pattern**:
```cpp
// Producer (main thread)
for (event : ready_events) {
    task_queue.push(event.fd);
}

// Consumer (worker threads)
while (true) {
    int fd = task_queue.pop();  // Blocks until work available
    handle_http_request(fd);    // Parse, serve file, send response
}
```

**Benefits**:
- **Horizontal scalability**: Automatically uses all CPU cores
- **Isolation**: Slow request doesn't block others
- **Graceful degradation**: Queue acts as buffer during load spikes

#### 3. HTTP/1.1 Protocol Implementation

**Request Parsing**:
```cpp
// Example request:
// GET /index.html HTTP/1.1
// Host: localhost:8080
// Connection: keep-alive

std::stringstream ss(request_line);
ss >> method >> uri >> version;

if (method == "GET") {
    std::string file_path = WEB_ROOT + uri;
    serve_file(file_path, client_fd);
}
```

**Response Generation**:
```http
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 1234
Connection: keep-alive

<!DOCTYPE html>
<html>...</html>
```


#### 4. Static File Serving

```cpp
// Open file and get size
std::ifstream file(path, std::ios::binary | std::ios::ate);
size_t file_size = file.tellg();
file.seekg(0);

// Send headers
send_headers(client_fd, "200 OK", {
    {"Content-Type", get_mime_type(path)},
    {"Content-Length", std::to_string(file_size)}
});

// Stream file in 4KB chunks
char buffer[4096];
while (file.read(buffer, sizeof(buffer))) {
    write(client_fd, buffer, file.gcount());
}
```

**Optimizations**:
- **Chunked transfer**: Prevents loading entire file into memory
- **Zero-copy I/O**: Could use `sendfile()` for even better performance
- **Content-Type detection**: Proper MIME types for browser rendering

### 📊 Performance Characteristics

**Concurrency model**:
- **Event-driven I/O**: Single thread monitors 10,000+ connections
- **Worker thread pool**: 4-8 threads (CPU count) handle request processing
- **Async logging**: Non-blocking output via producer-consumer queue

**Theoretical throughput** (on modern hardware):
- **Connections/sec**: ~50,000 (limited by `accept()` rate)
- **Requests/sec**: ~100,000 (for small static files)
- **Concurrent connections**: 10,000+ (C10K benchmark target)

**Why it's fast**:
1. **No busy-waiting**: `epoll_wait()` sleeps until events arrive
2. **Minimal context switches**: Thread pool avoids per-request thread creation
3. **CPU cache locality**: Workers process entire request without interruption

### 🚀 How to Build and Run

```bash
# Navigate to web server directory

# Compile the server
g++ -std=c++17 -O2 -pthread server.cpp -o server

# Run the server (creates public_html/ automatically)
./server

# Expected output:
# Created web root directory: ./public_html
# Launched worker thread 0
# Launched worker thread 1
# ...
# Server listening on port 8080 with 4 workers, serving files from ./public_html...

# In another terminal, test with curl:
curl http://localhost:8080/
# Output: <h1>Hello from My Server!</h1>

# Or open in browser:
firefox http://localhost:8080/
```

### 📁 Project Structure
```
Web_Server/
├── server.cpp                  # Complete HTTP server implementation
└── public_html/                # Document root (auto-created)
    ├── index.html              # Default homepage
    ├── style.css               # Stylesheet
```

### 🛠️ Technical Challenges Solved

1. **Edge-Triggered Epoll Complexity**: Must drain socket completely or miss events
   - Solution: Loop on `read()` until `EAGAIN`, checking `errno` carefully

2. **Keep-Alive Connection Management**: HTTP/1.1 allows request pipelining
   - Solution: Re-register socket with epoll after response sent

3. **Large File Handling**: Blocking `write()` causes head-of-line blocking
   - Solution: Chunked writes with `EPOLLOUT` monitoring (production version)

4. **Request Parsing Edge Cases**: Malformed requests, partial reads, buffer overflows
   - Solution: State machine parser with timeout handling

5. **Path Traversal Security**: `GET /../etc/passwd` must be blocked
   - Solution: Reject any URI containing `..` before filesystem access


## 🎓 Learning Outcomes

### Systems Programming
- Direct use of Linux system calls (`clone`, `mmap`, `epoll_wait`)
- Understanding of kernel abstractions (namespaces, cgroups, epoll)
- Virtual memory management and page-level operations

### Concurrency
- Lock-free programming with thread-local storage
- Producer-consumer patterns with condition variables
- Fine-grained locking strategies to minimize contention

### Networking
- Socket programming (TCP/IP stack interaction)
- I/O multiplexing techniques (epoll edge-triggered mode)
- HTTP protocol implementation and parsing

### Performance Engineering
- Profiling and benchmarking methodologies
- Understanding of cache effects and memory locality
- Trade-offs between latency, throughput, and scalability

---

## 📚 References & Resources

### Memory Allocator
- [TCMalloc Design Doc](https://google.github.io/tcmalloc/design.html)
- [glibc malloc internals](https://sourceware.org/glibc/wiki/MallocInternals)
- "Understanding the Linux Virtual Memory Manager" by Mel Gorman

### Containers
- [Linux Namespaces man page](https://man7.org/linux/man-pages/man7/namespaces.7.html)
- [Cgroups v2 Documentation](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html)
- "Container Security" by Liz Rice (O'Reilly)

### Web Server
- [epoll man page](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [RFC 7230: HTTP/1.1 Message Syntax](https://tools.ietf.org/html/rfc7230)
- "The C10K Problem" by Dan Kegel

---


## 📄 License

This project is for educational purposes. Feel free to use as reference for learning systems programming concepts.

---

**⭐ If you found these projects helpful, please star the repository!**
