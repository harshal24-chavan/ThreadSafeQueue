#pragma once

#include "TSQueue.h"
#include <functional>
#include <thread>
#include <vector>

template <typename T> class ThreadPool {
private:
  TSQueue<T> q;
  std::vector<std::thread> workers;

  void worker_loop() {
    T task;
    while (q.pop(task)) {
      task();
    }
  }

public:
  ThreadPool(int numberOfThreads) {
    for (int i = 0; i < numberOfThreads; i++) {
      workers.emplace_back(&ThreadPool::worker_loop, this);
    }
  }

  void addTask(std::function<void()> task) {
    if (task == nullptr)
      return;
    q.insert(std::move(task));
  }

  ~ThreadPool() {
    q.close();
    for (auto &th : workers) {
      if (th.joinable()) {
        th.join();
      }
    }
  }
};
