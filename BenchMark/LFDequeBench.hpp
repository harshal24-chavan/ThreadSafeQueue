#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <thread>
#include <vector>

#include "../LockFreeDeque.hpp"

using namespace std;

struct LatencyStats {
  string name;
  vector<int64_t> samples;

  void print() {
    if (samples.empty())
      return;
    sort(samples.begin(), samples.end());

    size_t n = samples.size();
    double sum = accumulate(samples.begin(), samples.end(), 0.0);

    cout << "\n========================================" << endl;
    cout << " STATS FOR: " << name << endl;
    cout << "========================================" << endl;
    cout << "Total Ops: " << n << endl;
    cout << "Avg:       " << fixed << setprecision(2) << (sum / n) << " ns"
         << endl;
    cout << "Min:       " << samples[0] << " ns" << endl;
    cout << "P50:       " << samples[n * 0.5] << " ns" << endl;
    cout << "P90:       " << samples[n * 0.9] << " ns" << endl;
    cout << "P99:       " << samples[n * 0.99] << " ns" << endl;
    cout << "P99.9:     " << samples[n * 0.999] << " ns" << endl;
    cout << "Max:       " << samples[n - 1] << " ns" << endl;

    // Simple ASCII Histogram
    cout << "\nDistribution Map (ns):" << endl;
    map<int, int> buckets;
    for (auto s : samples) {
      int bucket = 1;
      while (bucket <= s)
        bucket *= 2; // Logarithmic buckets
      buckets[bucket]++;
    }

    for (auto const &[range, count] : buckets) {
      string bar(min(50, (int)(50.0 * count / n * 10)), '*');
      cout << setw(10) << range << " | " << bar << " (" << count << ")" << endl;
    }
  }
};

int benchmark() {
  const int NUM_TASKS = 5'000'000;
  const int NUM_THIEVES = 3;
  LFDeque<int> deque(1 << 20); // 1M capacity

  // Pre-allocate dummy data so we don't benchmark 'new'
  vector<int *> tasks(NUM_TASKS);
  for (int i = 0; i < NUM_TASKS; ++i)
    tasks[i] = new int(i);

  atomic<int> completed{0};
  atomic<bool> start_flag{false};
  atomic<bool> done{false};

  LatencyStats producer_stats{"Producer (Push/Pop)"};
  producer_stats.samples.reserve(NUM_TASKS);

  vector<LatencyStats> thief_results(NUM_THIEVES);
  vector<thread> thieves;

  for (int i = 0; i < NUM_THIEVES; ++i) {
    thief_results[i].name = "Thief_" + to_string(i);
    thief_results[i].samples.reserve(NUM_TASKS); // Over-allocate to be safe

    thieves.emplace_back([&, i]() {
      while (!start_flag)
        this_thread::yield();

      while (!done || completed < NUM_TASKS) {
        auto t1 = chrono::steady_clock::now();
        int *task = deque.steal();
        auto t2 = chrono::steady_clock::now();

        if (task) {
          thief_results[i].samples.push_back(
              chrono::duration_cast<chrono::nanoseconds>(t2 - t1).count());
          completed.fetch_add(1, memory_order_relaxed);
        } else if (done && completed >= NUM_TASKS) {
          break;
        }
      }
    });
  }

  cout << "Starting Benchmark..." << endl;
  auto global_start = chrono::steady_clock::now();
  start_flag = true;

  // Producer Loop
  for (int i = 0; i < NUM_TASKS; ++i) {
    // Measure Push
    auto t1 = chrono::steady_clock::now();
    bool pushed = deque.push(tasks[i]);
    auto t2 = chrono::steady_clock::now();

    if (pushed) {
      producer_stats.samples.push_back(
          chrono::duration_cast<chrono::nanoseconds>(t2 - t1).count());
    }

    // Simulate local work by popping every other item
    if (i % 2 == 0) {
      auto t3 = chrono::steady_clock::now();
      int *self = deque.pop();
      auto t4 = chrono::steady_clock::now();
      if (self) {
        producer_stats.samples.push_back(
            chrono::duration_cast<chrono::nanoseconds>(t4 - t3).count());
        completed.fetch_add(1, memory_order_relaxed);
      }
    }
  }

  done = true;
  for (auto &t : thieves)
    t.join();
  auto global_end = chrono::steady_clock::now();

  // Results Printing
  double total_time =
      chrono::duration_cast<chrono::milliseconds>(global_end - global_start)
          .count() /
      1000.0;
  cout << "\nTOTAL TIME: " << total_time << " seconds" << endl;
  cout << "THROUGHPUT: " << (NUM_TASKS / total_time) / 1e6 << " M tasks/sec"
       << endl;

  producer_stats.print();
  for (auto &tr : thief_results)
    tr.print();

  // Cleanup
  for (auto t : tasks)
    delete t;

  return 0;
}
