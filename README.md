# C++ Thread Pool with Thread  Safe Queue

## A header-only, modern C++ thread pool implementation focusing on low-latency task distribution and high throughput.


### 🚀 Evolution of Performance

The following benchmarks were conducted on a **4-core machine** using **100,000 tasks** with a payload of 1,000 iterations of double-precision math.
Implementation	Throughput (tasks/sec)	Latency per Task	
#### Key Characteristics
Raw TSQueue	**1,500,000	~0.66 μs**	Raw push/pop without worker overhead.

V1: Single-Queue Pool	**400,000	~2.50 μs**	High contention on a single mutex.

V2: Multi-Queue Pool	**650,000+	~1.53 μs**	Round-robin distribution; reduced contention.

---
### 🛠️ Key Features

    Round-Robin Distribution: Tasks are distributed across per-thread queues using an atomic CAS (Compare-And-Swap) loop to minimize contention.

    Lock-Free Indexing: Utilizes std::atomic and compare_exchange_strong for thread-safe task routing without mutex overhead.

    Work-Stealing Architecture: (In Progress/Implemented) idle threads "steal" tasks from neighbor queues to ensure 100% CPU utilization across all cores.

    RAII Managed: Automatic lifecycle management using std::thread (or std::jthread for C++20).

---
## 📈 Benchmarking Insight

#### The "Mutex Bottleneck"
**In V1**, all worker threads and the producer thread fought for a single lock on one queue. This created a "bottleneck" where threads spent more time waiting for the lock than doing actual work.

#### The "Multi-Queue" Solution

**In V2**, we transitioned to an array of queues. By assigning a dedicated queue to each worker, we effectively split the contention by the number of hardware threads. The producer uses a lock-free "claimer" logic:
```C++
// Atomic Round-Robin Routing
int expected = pos.load();
int desired;
do {
    desired = (expected + 1) % maxThreads;
} while (!pos.compare_exchange_strong(expected, desired));
queueList[expected].push(std::move(task));
```
---
### ⚙️ How to Build

C++17 or higher.
```Bash
mkdir build && cd build && cmake .. && make
```
### ⚙️ How to run
(from root directory of the project)
```Bash
cd build && ./ts_project
```

---
### 📝 Future Roadmap

    [ ] Implement Work-Stealing logic to handle non-uniform workloads.

    [ ] Integrate C++20 std::stop_token for cleaner shutdowns.

    [ ] Add support for std::future to allow tasks to return values.
