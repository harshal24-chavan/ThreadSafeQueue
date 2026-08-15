#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

/* used for thread pining*/
#include <pthread.h>
#include <sched.h>

#include "ChaseLevDeq.hpp"

typedef void (*TaskFunc)(std::size_t worker_id, void *arg);
struct Task {
  TaskFunc func;
  void *arg;
};

template <size_t WorkerCount, size_t DequeCapacity = 1024>
class WorkStealingScheduler {
private:
  struct alignas(64) Worker {
    // The lock-free deque
    ChaseLevDeq<Task, DequeCapacity> deque;

    // Fast PRNG state for victim selection
    std::uint64_t rng_state;

    std::vector<Task> task_storage; // Physical memory for tasks
    std::vector<Task *> free_list;  // Stack of unused task pointers

    std::thread thread;
  };

  std::array<Worker, WorkerCount> workers;
  std::atomic<bool> running{false};

  // random number generator
  static std::uint64_t splitmix64(std::uint64_t &state) noexcept {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  void workerLoop(std::size_t my_id) {
    Worker &me = workers[my_id];

    while (running.load(std::memory_order_acquire)) {
      Task *task = nullptr;

      // local queue
      if (me.deque.popBottom(task)) {
        executeTask(my_id, task);
        continue;
      }

      // steal
      bool stole = false;
      for (std::size_t attempt = 0; attempt < WorkerCount; ++attempt) {
        // Random victim selection spreads out contention
        std::size_t victim = splitmix64(me.rng_state) % WorkerCount;
        if (victim == my_id)
          continue;

        if (workers[victim].deque.steal(task)) {
          executeTask(my_id, task);
          stole = true;
          break;
        }
      }
      if (stole)
        continue;

#if defined(__x86_64__) || defined(_M_X64)
      __builtin_ia32_pause();
#else
      std::this_thread::yield();
#endif
    }
  }

  void executeTask(std::size_t my_id, Task *task) noexcept {
    task->func(my_id, task->arg);
    // Memory recycling: Task is pushed into THIS worker's free list,
    // even if it was originally spawned by a different worker.
    workers[my_id].free_list.push_back(task);
  }

public:
  WorkStealingScheduler() noexcept {
    for (std::size_t i = 0; i < WorkerCount; ++i) {
      workers[i].rng_state = 0x9E3779B97F4A7C15ULL * (i + 1);

      // Pre-allocate tasks
      workers[i].task_storage.resize(DequeCapacity);
      workers[i].free_list.reserve(DequeCapacity);
      for (auto &t : workers[i].task_storage) {
        workers[i].free_list.push_back(&t);
      }
    }
  }

  ~WorkStealingScheduler() { stop(); }

  void spawn(std::size_t my_id, void (*func)(std::size_t, void *),
             void *arg) noexcept {
    Worker &me = workers[my_id];

    if (me.free_list.empty()) {
      assert(false && "Task pool exhausted!");
      return;
    }
    Task *task = me.free_list.back();
    me.free_list.pop_back();

    task->func = func;
    task->arg = arg;

    me.deque.pushBottom(task);
  }

  void start(void (*initial_func)(std::size_t, void *), void *initial_arg) {
    if (running.exchange(true, std::memory_order_acq_rel))
      return;

    spawn(0, initial_func, initial_arg);

    for (std::size_t i = 0; i < WorkerCount; ++i) {
      workers[i].thread = std::thread([this, i] { workerLoop(i); });

      // Create a CPU set mask
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);

      // Pin Worker `i` to Hardware Logical Core `i`
      CPU_SET(i, &cpuset);

      // Apply the affinity to the native thread handle
      int rc = pthread_setaffinity_np(workers[i].thread.native_handle(),
                                      sizeof(cpu_set_t), &cpuset);
      if (rc != 0) {
        assert(false && "issue with thread affinity");
      }
    }
  }

  void stop() noexcept {
    if (!running.exchange(false, std::memory_order_acq_rel))
      return;
    for (auto &w : workers) {
      if (w.thread.joinable())
        w.thread.join();
    }
  }
};
