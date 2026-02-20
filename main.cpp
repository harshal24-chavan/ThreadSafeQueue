#include "TSQueue.h"
#include "ThreadPool.h"
#include "ThreadPoolDeq.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

// producer inserting things in the queue
void producer(std::shared_ptr<TSQueue<int>> q, int id) {

  using namespace std::chrono_literals;
  for (int i = 1; i <= 100; i++) {
    q->insert(i * id);
    std::this_thread::sleep_for(100ms);
  }
}

// consumer reading things from queue
std::mutex consumerMtx;
void consumer(std::shared_ptr<TSQueue<int>> q, int id) {
  // int n = 1e6 + 7;
  while (1) {
    int val = -1;
    q->pop(val);
    // we need lock because cout is not thread safe
    // the output print will be jumbled without a lock
    // std::lock_guard<std::mutex> lg(consumerMtx);
    // std::cout << "Consumer: " << id << " | consumed: " << val << std::endl;
    // n--;
  }
}

void run_unit_tests();

void run_benchmark(int num_producers, int num_consumers,
                   int items_per_producer) {
  TSQueue<int> benchQ;
  int total_items = num_producers * items_per_producer;

  auto start = std::chrono::high_resolution_clock::now();

  // 1. Start Consumers (They will wait for data)
  std::vector<std::thread> consumers;
  for (int i = 0; i < num_consumers; ++i) {
    consumers.emplace_back(
        [&benchQ, items_per_producer, num_producers, num_consumers]() {
          // Each consumer takes a fair share
          int to_pop = (num_producers * items_per_producer) / num_consumers;
          for (int j = 0; j < to_pop; ++j) {
            int val;
            benchQ.pop(val);
          }
        });
  }

  // 2. Start Producers
  std::vector<std::thread> producers;
  for (int i = 0; i < num_producers; ++i) {
    producers.emplace_back([&benchQ, items_per_producer]() {
      for (int j = 0; j < items_per_producer; ++j) {
        benchQ.insert(j);
      }
    });
  }

  for (auto &t : producers)
    t.join();
  for (auto &t : consumers)
    t.join();

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  double throughput = total_items / elapsed.count();

  std::cout << "--- Benchmark Results ---" << std::endl;
  std::cout << "Threads: " << (num_producers + num_consumers) << std::endl;
  std::cout << "Total Items: " << total_items << std::endl;
  std::cout << "Time: " << elapsed.count() << "s" << std::endl;
  std::cout << "Throughput: " << throughput << " items/sec" << std::endl;
}

void my_task_wrapper(void *arg) {
  auto *counter = static_cast<std::atomic<int> *>(arg);

  // Perform dummy work
  double result = 0;
  for (int i = 0; i < 1000; ++i) {
    result += (i * i);
  }

  (*counter)--;
}

void benchmark_thread_pool(int num_threads, int total_tasks) {
  ThreadPoolDeq<Task> pool;
  std::atomic<int> tasks_remaining(total_tasks);

  Task t;
  t.func = my_task_wrapper;
  t.arg = &tasks_remaining;

  auto start = std::chrono::high_resolution_clock::now();

  // using namespace std::chrono_literals;
  // pool.addTask([&tasks_remaining](){
  //     std::this_thread::sleep_for(1s);
  //     });

  for (int i = 0; i < total_tasks; ++i) {
    pool.submitTask(t);
  }

  // "Busy wait" until all tasks are done
  while (tasks_remaining > 0) {
    std::this_thread::yield();
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  std::cout << "--- ThreadPool Benchmark ---" << std::endl;
  std::cout << "Threads: " << std::thread::hardware_concurrency()
            << " | Tasks: " << total_tasks << std::endl;
  std::cout << "Time: " << elapsed.count() << "s" << std::endl;
  std::cout << "Throughput: " << total_tasks / elapsed.count() << " tasks/sec"
            << std::endl;
}

int main() {

  benchmark_thread_pool(6, 10000000);

  // run_benchmark(8, 8, 10000);

  // ThreadPool<std::function<void()>> pool;
  //
  // pool.addTask([]() { std::cout << "Task 1 running on thread " <<
  // std::this_thread::get_id() << std::endl; }); pool.addTask([]() { std::cout
  // << "Task 2 running on thread " << std::this_thread::get_id() << std::endl;
  // }); pool.addTask([]() { std::cout << "Task 3 running on thread " <<
  // std::this_thread::get_id() << std::endl; });
  //

  //
  // run_unit_tests();
  //
  // std::cout << "starting simulation" << std::endl;
  // // making shared ptr because we need to share it between our multiple
  // threads std::shared_ptr<TSQueue<int>> q = std::make_shared<TSQueue<int>>();
  //
  // // to manage threads we are using a vector
  // std::vector<std::thread> threads;
  //
  // for (int i = 1; i <= 100; i++) {
  //   // thread by default copies the values,
  //   // hence using std::ref to send by reference
  //   std::thread thProducer(producer, std::ref(q), i * 3);
  //   // we want actual threads to go in the vector hence std::move
  //   threads.push_back(std::move(thProducer));
  // }
  //
  // for (int i = 1; i <= 10; i++) {
  //   std::thread thConsumer(consumer, std::ref(q), i * 7);
  //   threads.push_back(std::move(thConsumer));
  // }
  //
  // // joining all threads
  // for (auto &th : threads) {
  //   th.join();
  // }
  //
  // std::cout << "TSQueue size: " << q->size() << std::endl;

  return 0;
}
