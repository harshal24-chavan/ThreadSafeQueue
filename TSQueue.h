#pragma once
#include <atomic>
#include <queue>
#include <condition_variable>
#include <mutex>


template <typename T> class TSQueue {

private:
  std::queue<T> q;
  mutable std::mutex mtx;
  // sort of used for communication between threads
  std::condition_variable cv;

  std::atomic<bool> done = false ;

public:
  TSQueue() = default;
  void insert(T &&val) {

    std::lock_guard<std::mutex> qMtx(mtx);
    q.push(std::move(val));
    // like a delivery person ringing the bell to tell there is a package
    cv.notify_one();
  }

  void insert(T &val) {

    std::lock_guard<std::mutex> qMtx(mtx);
    q.push(val);
    cv.notify_one();
  }

  bool pop(T &val) {

    std::unique_lock<std::mutex> ulk(mtx);
    // we will wait till there is a package
    cv.wait(ulk, [this] { return q.size() > 0 || done; });

    // this is to tell that we are done processing tasks you can leave now
    if(q.size() == 0 && done) return false;

    // this will never be null as we are waiting for any items 
    // to be inserted into our queue
    //
    // this says that there is sstill data to process
    // once we get the package we do not have to notify anyone
    val = std::move(q.front());
    q.pop();

    return true;
  }

  int size() const {
    std::lock_guard<std::mutex> qMtx(mtx);
    return q.size();
  }

  void close(){
    done = true;
    cv.notify_all();
  }
};



