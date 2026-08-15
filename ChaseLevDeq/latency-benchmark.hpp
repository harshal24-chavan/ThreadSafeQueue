#include <algorithm>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h> // For __rdtsc() and __rdtscp()
#endif

// Include your scheduler header here
// #include "WorkStealingScheduler.h"

constexpr int TREE_DEPTH = 19;
constexpr int TOTAL_TASKS = (1 << (TREE_DEPTH + 1)) - 1;

std::atomic<int> tasks_remaining{TOTAL_TASKS};

// Global array to hold raw cycle latencies without allocating on the hot path.
// We use a raw array to guarantee zero overhead.
uint64_t global_latencies[TOTAL_TASKS];

struct TaskContext {
  WorkStealingScheduler<2, 1024> *sched;
  int task_index;
  uint64_t spawn_tsc; // Cycles timestamp when this task was created
};

std::vector<TaskContext> global_contexts;

// ---------------------------------------------------------
// INLINE TSC WRAPPERS
// ---------------------------------------------------------
inline uint64_t rdtsc_start() {
  // lfence prevents earlier instructions from reordering past the timestamp
  _mm_lfence();
  return __rdtsc();
}

inline uint64_t rdtsc_end() {
  unsigned int aux;
  // rdtscp serializes inherently, ensuring all task code finishes first
  uint64_t tsc = __rdtscp(&aux);
  _mm_lfence(); // Prevent future instructions from moving backward
  return tsc;
}

// ---------------------------------------------------------
// THE TASK ROUTINE
// ---------------------------------------------------------
void cpu_heavy_task(std::size_t my_worker_id, void *arg) {
  // 1. Immediately stamp the start time.
  uint64_t start_tsc = rdtsc_end();

  TaskContext *ctx = static_cast<TaskContext *>(arg);

  // Calculate queue latency: Time from parent spawning it -> to this core
  // running it
  if (ctx->task_index != 0) { // Skip root task
    global_latencies[ctx->task_index] = start_tsc - ctx->spawn_tsc;
  }

  // 2. Simulate real work
  volatile double compute = 0.0;
  for (int i = 0; i < 500; ++i) {
    compute += i * 3.14159;
  }

  // 3. Spawn children with their spawn timestamps
  int left_idx = 2 * ctx->task_index + 1;
  int right_idx = 2 * ctx->task_index + 2;

  if (left_idx < TOTAL_TASKS) {
    global_contexts[left_idx].spawn_tsc = rdtsc_start();
    ctx->sched->spawn(my_worker_id, cpu_heavy_task, &global_contexts[left_idx]);
  }
  if (right_idx < TOTAL_TASKS) {
    global_contexts[right_idx].spawn_tsc = rdtsc_start();
    ctx->sched->spawn(my_worker_id, cpu_heavy_task,
                      &global_contexts[right_idx]);
  }

  // 4. Mark complete
  tasks_remaining.fetch_sub(1, std::memory_order_release);
}

// ---------------------------------------------------------
// PERCENTILE HISTOGRAM GENERATOR
// ---------------------------------------------------------
void print_latency_histogram(uint64_t *latencies, size_t count,
                             double cpu_ghz) {
  std::cout << "\n[*] Building Latency Histogram (Queue Wait Times)...\n";
  std::sort(latencies, latencies + count);

  auto print_percentile = [&](const char *label, double percentile) {
    size_t idx = static_cast<size_t>((percentile / 100.0) * (count - 1));
    ;
    if (idx >= count)
      idx = count - 1;

    uint64_t cycles = latencies[idx];
    double ns = cycles / cpu_ghz;

    std::cout << std::left << std::setw(8) << label << ": " << std::setw(10)
              << cycles << " cycles (" << std::fixed << std::setprecision(2)
              << ns / 1000.0 << " us)\n";
  };

  std::cout << "--------------------------------------------------\n";
  print_percentile("50th %", 50.0);
  print_percentile("90th %", 90.0);
  print_percentile("99th %", 99.0);
  print_percentile("99.9th %", 99.9);
  print_percentile("99.99th%", 99.99);
  print_percentile("Max", 100.0);
  std::cout << "--------------------------------------------------\n";
}
