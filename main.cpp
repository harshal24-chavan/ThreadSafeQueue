#include "./ChaseLevDeq/CL-WS-Threadpool.hpp"
#include "./ChaseLevDeq/latency-benchmark.hpp"

// #include "./MutexBasedQueues/Latency-Benchmark.hpp"

#include <cassert>

int main() {
  WorkStealingScheduler<2, 1024> scheduler;

  global_contexts.resize(TOTAL_TASKS);
  for (int i = 0; i < TOTAL_TASKS; ++i) {
    global_contexts[i].sched = &scheduler;
    global_contexts[i].task_index = i;
  }

  std::cout << "[*] Spawning " << TOTAL_TASKS << " tasks...\n";

  // Start clock for total throughput
  uint64_t total_start = rdtsc_start();

  // Spawn root task
  global_contexts[0].spawn_tsc = total_start;
  scheduler.start(cpu_heavy_task, &global_contexts[0]);

  // Spin-wait lock-free
  while (tasks_remaining.load(std::memory_order_acquire) > 0) {
    _mm_pause();
  }

  uint64_t total_end = rdtsc_end();

  scheduler.stop();

  // Print Total Cycle Throughput
  std::cout << "[+] Total Execution: " << (total_end - total_start)
            << " CPU Cycles.\n";

  // Print the Histogram (passing TOTAL_TASKS - 1 because we skip index 0)
  print_latency_histogram(&global_latencies[1], TOTAL_TASKS - 1, 3.06);

  // std::cout << "MutexBasedQueues: latency" << std::endl;
  // MutexBasedLatencyBenchmark();

  return 0;
}
