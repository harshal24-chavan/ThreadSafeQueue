#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

template <typename T> class TSDeque {
private:
  std::deque<T> deq;
  mutable std::mutex mtx;

  std::condition_variable cv;

  std::atomic_flag workDone = ATOMIC_FLAG_INIT;

public:
  TSDeque() = default;

  void insertAtFront(T &&val) {
    // we are inserting at front becuase we want to steal the latest task ,which
    // is fresh in cpu memory
    std::lock_guard<std::mutex> lck(mtx);
    deq.push_front(std::move(val));

    cv.notify_one();
  }

  void insertAtFront(T &val) {
    std::lock_guard<std::mutex> lck(mtx);
    deq.push_front(val);

    cv.notify_one();
  }

  bool popAtFront(T &val) {
    // this is for our current thread to work on
    // as the task is fresh in cache
    // it is much faster

    std::unique_lock<std::mutex> ulk(mtx);

    cv.wait(ulk, [this]() { return deq.size() > 0 || workDone.test(); });

    if (deq.size() == 0 && workDone.test()) {
      return false;
    }

    val = std::move(deq.front());
    deq.pop_front();

    return true;
  }

  bool popAtBack(T &val) {
    // this is for the theif thread
    // as this task is probably removed from current threads cache
    // we want to steal this task and give it to another thread

    std::unique_lock<std::mutex> ulk(mtx, std::defer_lock);
    if (!ulk.try_lock()) {
      // theif / stealer did not acquire the lock
      return false;
    }

    if (deq.size() == 0) {
      return false;
    }

    // lock acquired by stealer / theif
    val = std::move(deq.back());
    deq.pop_back();

    return true;
  }

  int size() const {
    std::lock_guard<std::mutex> lck(mtx);
    return deq.size();
  }
  void close() {
    // this sets the workDone value to true and return prev val;
    // we don't have to do anything with the prev val hence just used temp
    bool temp = workDone.test_and_set();

    // why do I have to notify here?
    cv.notify_all();
  }
};
