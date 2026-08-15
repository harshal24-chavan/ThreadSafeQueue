#pragma once

#include "LockFreeDeque.hpp"
#include <atomic>
#include <thread>

struct alignas(64) Task {
  void (*execute)(void *context);
  void *context;

  void operator()() {
    if (execute)
      execute(context);
  }
};

template <std::size_t WorkerCount, std::size_t DequeCapacity = 4096>
class WorkStealingScheduler {

  static_assert(WorkerCount > 0);
  static_assert(std::has_single_bit(DequeCapacity));

public:
  // Same basic task representation as your old scheduler.
  using TaskFunc = void (*)(void *);

  struct Task {
    TaskFunc func;
    void *arg;

    void operator()() const noexcept {
      if (func) {
        func(arg);
      }
    }
  };

private:
  // --------------------------------------------------------
  // Worker
  // --------------------------------------------------------

  struct Worker {

    // IMPORTANT:
    //
    // Only this worker may push/pop from this deque.
    // Other workers can only steal().
    //
    ChaseLevDeq<Task, DequeCapacity> deque;

    std::thread thread;

    // Worker-local PRNG state.
    std::uint64_t rng_state;
  };

  std::array<Worker, WorkerCount> workers;

  std::atomic<bool> running{false};

public:
  WorkStealingScheduler() noexcept {

    for (std::size_t i = 0; i < WorkerCount; ++i) {

      workers[i].rng_state = 0x9E3779B97F4A7C15ULL * (i + 1);
    }
  }

  ~WorkStealingScheduler() { stop(); }

  WorkStealingScheduler(const WorkStealingScheduler &) = delete;

  WorkStealingScheduler &operator=(const WorkStealingScheduler &) = delete;

  // ========================================================
  // START
  //
  // Initial tasks MUST be inserted before workers start.
  //
  // This is what lets us avoid a global submission queue.
  // ========================================================

  void start(std::vector<Task *> initial_tasks) {

    bool expected = false;

    if (!running.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
      return;
    }

    // ----------------------------------------------------
    // Initial distribution.
    //
    // Workers haven't started yet, therefore there is
    // no owner/producer race here.
    // ----------------------------------------------------

    for (std::size_t i = 0; i < initial_tasks.size(); ++i) {

      const std::size_t worker_id = i % WorkerCount;

      Task *task = initial_tasks[i];

      const bool pushed = workers[worker_id].deque.pushBottom(task);

      // For this simple fixed-capacity scheduler,
      // don't silently lose work.
      assert(pushed);
    }

    // ----------------------------------------------------
    // Now workers start.
    //
    // From this point forward each worker owns exactly
    // one deque.
    // ----------------------------------------------------

    for (std::size_t i = 0; i < WorkerCount; ++i) {

      workers[i].thread = std::thread([this, i] { workerLoop(i); });
    }
  }

  // ========================================================
  // STOP
  // ========================================================

  void stop() noexcept {

    bool expected = true;

    if (!running.compare_exchange_strong(expected, false,
                                         std::memory_order_acq_rel)) {
      return;
    }

    for (auto &worker : workers) {

      if (worker.thread.joinable()) {
        worker.thread.join();
      }
    }
  }

  // ========================================================
  // SPAWN
  //
  // This is the important API.
  //
  // A worker executing a task calls:
  //
  //     spawn(worker_id, task)
  //
  // and puts the task into ITS OWN deque.
  //
  // No global queue.
  // ========================================================

  bool spawn(std::size_t worker_id, Task *task) noexcept {

    assert(worker_id < WorkerCount);

    return workers[worker_id].deque.pushBottom(task);
  }

private:
  // ========================================================
  // SplitMix64
  //
  // Worker-local random victim selection.
  //
  // No mutex.
  // ========================================================

  static std::uint64_t splitmix64(std::uint64_t &state) noexcept {

    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;

    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;

    return z ^ (z >> 31);
  }

  // ========================================================
  // STEAL
  //
  // Randomly selects victims.
  //
  // Each worker tries at most WorkerCount victims.
  // ========================================================

  Task *stealWork(std::size_t thief_id) noexcept {

    auto &thief = workers[thief_id];

    for (std::size_t attempt = 0; attempt < WorkerCount; ++attempt) {

      const std::uint64_t random = splitmix64(thief.rng_state);

      const std::size_t victim = random % WorkerCount;

      if (victim == thief_id) {
        continue;
      }

      Task *task = nullptr;

      if (workers[victim].deque.steal(task)) {

        return task;
      }
    }

    return nullptr;
  }

  // ========================================================
  // WORKER LOOP
  // ========================================================

  void workerLoop(std::size_t worker_id) {

    auto &worker = workers[worker_id];

    while (running.load(std::memory_order_acquire)) {

      Task *task = nullptr;

      // ------------------------------------------------
      // Phase 1:
      //
      // Work on our own queue.
      //
      // popBottom() is owner-only.
      // ------------------------------------------------

      if (worker.deque.popBottom(task)) {

        execute(task);

        continue;
      }

      // ------------------------------------------------
      // Phase 2:
      //
      // We have no local work.
      //
      // Try stealing from another worker.
      // ------------------------------------------------

      task = stealWork(worker_id);

      if (task != nullptr) {

        execute(task);

        continue;
      }

      // ------------------------------------------------
      // Phase 3:
      //
      // Nothing found.
      //
      // For your benchmark, spin/yield rather than
      // introducing condition variables into the
      // scheduler hot path.
      // ------------------------------------------------

      std::this_thread::yield();
    }

    // ----------------------------------------------------
    // Shutdown drain.
    //
    // Try to finish local work before leaving.
    // ----------------------------------------------------

    while (true) {

      Task *task = nullptr;

      if (worker.deque.popBottom(task)) {

        execute(task);
        continue;
      }

      task = stealWork(worker_id);

      if (task == nullptr) {
        break;
      }

      execute(task);
    }
  }

  // ========================================================
  // EXECUTE
  // ========================================================

  static void execute(Task *task) noexcept {

    if (!task) {
      return;
    }

    (*task);

    // Scheduler owns Task lifetime.
    delete task;
  }
};
