#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

// By adding alignas(64), you told the compiler: "Every single TSDeque instance
// must start at a memory address that is a multiple of 64."
//
// This effectively gives every queue its own "private lane" in the CPU cache.
//
// Thread 0 can hammer Queue[0] as fast as it wants.
//
// Thread 1 can hammer Queue[1] simultaneously.
//
// Because they are on separate cache lines, the CPU cores no longer need to
// talk to each other to "verify" the memory. They can both run at full speed
// without the hardware-level interrupts.
template <typename T> class alignas(64) TSDeque {
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

  bool tryInsertAtFront(T &&val) {
    std::unique_lock<std::mutex> ulk(mtx, std::defer_lock);

    if (!ulk.try_lock())
      return false;

    deq.push_front(std::move(val));
    cv.notify_all();

    return true;
  }
  bool tryInsertAtFront(T &val) {
    std::unique_lock<std::mutex> ulk(mtx, std::defer_lock);

    if (!ulk.try_lock())
      return false;

    deq.push_front(val);
    cv.notify_all();

    return true;
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

  bool tryPopAtFront(T &val) {
    std::unique_lock<std::mutex> ulk(mtx, std::defer_lock);

    // checking first for efficiency
    if (deq.empty()) {
      return false;
    }

    if (!ulk.try_lock()) {
      // lock not acquired then return false
      return false;
    }

    // maybe someone took the element before locking
    // hence checking again
    if (deq.empty()) {
      return false;
    }

    val = std::move(deq.front());
    deq.pop_front();

    return true;
  }

  bool tryPopAtBack(T &val) {
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
    // so that no thread keeps on listening with cv.wait(), and becomes a dead
    // lock
    cv.notify_all();
  }
};
