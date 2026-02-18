#pragma once

#include "TSQueue.h"
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

template <typename T> class ThreadPool {
private:
  std::unique_ptr<TSQueue<T>[]> queueList;
  std::vector<std::thread> workers;

  int maxThreads{0};

  // for round robin load balancing of tasks
  std::atomic<int> pos{0};

  void worker_loop(int ind) {
    T task;
    while (queueList[ind].pop(task)) {
      task();
    }
  }

public:
  ThreadPool() {
    maxThreads = std::thread::hardware_concurrency();

    queueList = std::make_unique<TSQueue<T>[]>(maxThreads);

    for (int i = 0; i < maxThreads; i++) {
      workers.emplace_back(&ThreadPool::worker_loop, this, i);
    }
  }

  void addTask(std::function<void()> task) {
    if (task == nullptr)
      return;

    // for this search and understand CAS ( compare and exchange strong ) theory
    int currentPos = pos.load();
    int nextPos;
    do {
      nextPos = (currentPos + 1) % maxThreads;
    } while (!pos.compare_exchange_strong(currentPos, nextPos));

    queueList[currentPos].insert(std::move(task));
  }

  ~ThreadPool() {
    for (int i = 0; i < maxThreads; i++) {
      // closing every queue
      queueList[i].close();
    }

    for (auto &th : workers) {
      if (th.joinable()) {
        th.join();
      }
    }
  }
};
