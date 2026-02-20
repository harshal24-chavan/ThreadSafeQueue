# C++ Thread Pool with Thread  Safe Queue

## A header-only, modern C++ thread pool implementation focusing on low-latency task distribution and high throughput.


### 🚀 Performance Evolution

The pool was benchmarked using 10 million tasks on a 4-core (8-thread) machine. Each task consisted of 1,000 iterations of double-precision floating-point math.

|Milestone|Implementation |Throughput (Tasks/sec)|Key Optimization |
|---------|---------------|----------------------|-----------------|
|V1	      | Single Global Queue|	~400,000	 |Baseline|
|V2	|Multi-Queue (Round Robin)|	~600,000|	Reduced Mutex Contention|
|V3|	Work-Stealing Logic|	~558,000|	Added Load Balancing (The "Stealing Tax")|
|V4|	Cache-Aligned Deques|	~676,000|	Eliminated False Sharing|
|V5|	Raw Function Pointers|	727,236|	Type-Erasure Removal|
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
int currentPos = pos.load();
int nextPos;
do {
    nextPos = (currentPos + 1) % maxThreads;
} while (!pos.compare_exchange_strong(currentPos, nextPos));
queueList[currentPos].push(std::move(task));
```

#### Work Stealing:
**In V3** implemented work stealing by generating a random start point using **mt19937** engine, and looping through all available queues and trying a task steal (non blocking).
```cpp
// taskFound flag, index of the thread / queue, Actual Task to pass as parameter
  void TryStealing(bool &taskFound, int ind, T &task) {
    // attempting to get a task from n different threads
    int randStart = get_random();

    for (int i = 0; i < maxThreads; i++) {

      int checkInd = (randStart + i) % maxThreads;
      
      // non blocking because of unique_lock::try_lock();
      // instantly returns true if locked else returns false
      if (dequeList[checkInd].tryPopAtBack(task)) {
        taskFound = true;
        break;
      }
    }
  }
```

#### False Sharing Problem:
**In V4**, (Suggested by Gemini), CPU reads with 64 bits of memory in a sequence, so let's store 1 threads data in all contiguous 64 bit space. This way it won't have to co-ordinate with any other thread and will be much faster.
Using **alignas(64)** ensures each thread's deque sits on a private cache line, removing this hidden hardware tax.

Example:
```md
Case 1:
1. Data about thread 0 and thread 1 are stored nearby on the same 64 bit  contiguous space.
2. Thread 0 modifies the data a bit
3. Thread 1 has to again update it's cache to be on the same page as thread 0, and not use old data.

Case 2: (optimization)
1. Data about thread 0 will be stored in ram in 64th - 127th bit
2. Data about thread 1 will be stored in ram in 128th - 191st bit
3. Any changes made on individual threads will not affect the other thread, making it efficient.
```

#### std::function<void()> removal:
**In V5**, (suggested by Gemini), since we are just passing a basic task to our Queue, we do not require the wrapper of std::function as it has much more functionality and will generally be a larger object than our smaller struct. We made our struct to be of 16 bytes so that on a 64 bytes row we can store 4 tasks, making it efficient.
```cpp
// Instead of std::function<void()>
typedef void (*TaskFunc)(void *);
struct Task {
  TaskFunc func;
  void *arg;

  void operator()() const noexcept {
    if (func)
      func(arg);
  }
};
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

    [✅] Implement Work-Stealing logic to handle non-uniform workloads.

    [ ] Integrate C++20 std::stop_token for cleaner shutdowns.

    [ ] Add support for std::future to allow tasks to return values.
