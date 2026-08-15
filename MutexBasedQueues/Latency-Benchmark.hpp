#include "./ThreadPoolDeq.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <pthread.h>
#include <sched.h>
#include <x86intrin.h>
#endif

constexpr int TREE_DEPTH = 19;
constexpr int TOTAL_TASKS = (1 << (TREE_DEPTH + 1)) - 1;

constexpr int WORK_ITERATIONS = 500;

std::atomic<int> tasks_remaining{TOTAL_TASKS};

uint64_t global_latencies[TOTAL_TASKS];

class BenchmarkContext;

struct TaskContext {
  ThreadPoolDeq<Task> *pool;

  int task_index;

  uint64_t spawn_tsc;
};

std::vector<TaskContext> global_contexts;

#if defined(__x86_64__) || defined(_M_X64)

inline uint64_t rdtsc_start() noexcept {
  _mm_lfence();
  return __rdtsc();
}

inline uint64_t rdtsc_end() noexcept {
  unsigned int aux;
  uint64_t tsc = __rdtscp(&aux);
  _mm_lfence();
  return tsc;
}
#endif

void cpu_heavy_task(void *arg) {

  TaskContext *ctx = static_cast<TaskContext *>(arg);

  uint64_t start_tsc = rdtsc_end();

  // Root has no meaningful spawn latency.
  if (ctx->task_index != 0) {
    global_latencies[ctx->task_index] = start_tsc - ctx->spawn_tsc;
  }

  volatile double compute = 0.0;

  for (int i = 0; i < WORK_ITERATIONS; ++i) {
    compute += i * 3.14159;
  }

  int left_idx = 2 * ctx->task_index + 1;

  int right_idx = 2 * ctx->task_index + 2;

  if (left_idx < TOTAL_TASKS) {

    global_contexts[left_idx].spawn_tsc = rdtsc_start();

    Task child{&cpu_heavy_task, &global_contexts[left_idx]};

    ctx->pool->submitTask(child);
  }

  if (right_idx < TOTAL_TASKS) {

    global_contexts[right_idx].spawn_tsc = rdtsc_start();

    Task child{&cpu_heavy_task, &global_contexts[right_idx]};

    ctx->pool->submitTask(child);
  }

  // --------------------------------------------------------
  // Mark task complete
  // --------------------------------------------------------

  tasks_remaining.fetch_sub(1, std::memory_order_release);
}

// ============================================================
// LATENCY HISTOGRAM
// ============================================================

void print_latency_histogram(uint64_t *latencies, size_t count,
                             double tsc_ghz) {

  std::sort(latencies, latencies + count);

  std::cout << "\n[*] Building Latency Histogram "
            << "(Spawn -> Task Start)...\n";

  std::cout << "--------------------------------------------------\n";

  auto print_percentile = [&](const char *label, double percentile) {
    size_t idx = static_cast<size_t>((percentile / 100.0) * (count - 1));

    uint64_t cycles = latencies[idx];

    // GHz = cycles / nanoseconds
    double ns = cycles / tsc_ghz;

    std::cout << std::left << std::setw(8) << label << ": " << std::setw(12)
              << cycles << " cycles (" << std::fixed << std::setprecision(2)
              << ns / 1000.0 << " us)\n";
  };

  print_percentile("50th %", 50.0);
  print_percentile("90th %", 90.0);
  print_percentile("99th %", 99.0);
  print_percentile("99.9th%", 99.9);
  print_percentile("99.99th%", 99.99);

  std::cout << "Max     : " << latencies[count - 1] << " cycles (" << std::fixed
            << std::setprecision(2) << (latencies[count - 1] / tsc_ghz) / 1000.0
            << " us)\n";

  std::cout << "--------------------------------------------------\n";
}

int MutexBasedLatencyBenchmark() {

  ThreadPoolDeq<Task> scheduler;

  global_contexts.resize(TOTAL_TASKS);

  for (int i = 0; i < TOTAL_TASKS; ++i) {

    global_contexts[i].pool = &scheduler;

    global_contexts[i].task_index = i;

    global_contexts[i].spawn_tsc = 0;
  }

  tasks_remaining.store(TOTAL_TASKS, std::memory_order_relaxed);

  std::cout << "[*] Spawning " << TOTAL_TASKS << " tasks...\n";

  uint64_t total_start = rdtsc_start();

  global_contexts[0].spawn_tsc = total_start;

  Task root{&cpu_heavy_task, &global_contexts[0]};

  scheduler.submitTask(root);

  while (tasks_remaining.load(std::memory_order_acquire) > 0) {

    _mm_pause();
  }

  uint64_t total_end = rdtsc_end();

  uint64_t total_cycles = total_end - total_start;

  constexpr double TSC_GHZ = 3.06;

  double seconds = total_cycles / (TSC_GHZ * 1e9);

  double tasks_per_sec = TOTAL_TASKS / seconds;

  std::cout << "\n[+] Total Execution: " << total_cycles << " CPU Cycles.\n";

  std::cout << "[+] Estimated Execution Time: " << std::fixed
            << std::setprecision(3) << seconds << " seconds\n";

  std::cout << "[+] Throughput: " << std::fixed << std::setprecision(0)
            << tasks_per_sec << " tasks/sec\n";

  print_latency_histogram(&global_latencies[1], TOTAL_TASKS - 1, TSC_GHZ);

  return 0;
}
