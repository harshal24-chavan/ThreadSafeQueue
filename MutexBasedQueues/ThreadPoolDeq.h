#pragma once

#include "TSDeque.h"
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <vector>

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

template <typename T> class ThreadPoolDeq {
private:
  std::unique_ptr<TSDeque<T>[]> dequeList;
  std::vector<std::jthread> workers;

  std::mutex rand_mut;
  std::mt19937 mt;

  std::atomic<int> pos{0};
  int maxThreads{2};

  void worker_loop(int ind) {

    while (true) {
      T task;
      bool taskFound = false;

      // phase 1: trying non blocking in our own queue
      if (dequeList[ind].tryPopAtFront(task)) {
        taskFound = true;
      }

      // phase 2: trying non blocking by stealing from another queue
      if (!taskFound) {
        TryStealing(taskFound, ind, task);
      }

      if (taskFound) {
        task();
      } else {
        if (dequeList[ind].popAtFront(task)) {
          // now since we did not get any task directly
          // we'll try a blocking way to get task
          task();
        } else {
          // we now want to close the thread hence break
          break;
        }
      }
    }
  }

  void TryStealing(bool &taskFound, int ind, T &task) {
    // attempting to get a task from n different threads
    int randStart = get_random();

    for (int i = 0; i < maxThreads; i++) {

      int checkInd = (randStart + i) % maxThreads;

      if (dequeList[checkInd].tryPopAtBack(task)) {
        taskFound = true;
        break;
      }
    }
  }

  int get_random() {
    std::lock_guard<std::mutex> lck(rand_mut);
    std::uniform_int_distribution<int> dist(0, maxThreads - 1);

    return dist(mt);
  }

public:
  ThreadPoolDeq() : maxThreads(std::thread::hardware_concurrency()) {
    dequeList = std::make_unique<TSDeque<T>[]>(maxThreads);

    for (int i = 0; i < maxThreads; i++) {
      workers.emplace_back(&ThreadPoolDeq::worker_loop, this, i);
    }
  }

  void addTask(std::function<void()> task) {
    if (task == nullptr)
      return;

    int currentPos = pos.load();
    int nextPos;
    do {
      nextPos = (currentPos + 1) % maxThreads;
    } while (!pos.compare_exchange_strong(currentPos, nextPos));

    // dequeList[currentPos].insertAtFront(std::move(task));
    dequeList[currentPos].insertAtFront(std::move(task));
  }

  void submitTask(Task &task) {

    int i = 0;
    do {
      i = get_random();
    } while (!dequeList[i].tryInsertAtFront(task));
  }

  ~ThreadPoolDeq() {
    for (int i = 0; i < maxThreads; i++) {
      dequeList[i].close();
    }

    // now the  jthreads will close/ join themselves
  }
};
