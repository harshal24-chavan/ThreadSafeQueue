
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>

template <typename T> struct Buffer {
  std::unique_ptr<std::atomic<T *>[]> buf;
  uint64_t mask;

  Buffer(uint64_t capacity) {
    if ((capacity & (capacity - 1)) != 0) {
      std::cerr << "capacity should be power of 2.\n";
      return;
    }

    buf = std::make_unique<std::atomic<T *>[]>(capacity);

    mask = capacity - 1;
  }
};

template <typename T> class LFDeque {
private:
  alignas(64) std::atomic<uint64_t> top;    // used by the thief(pop)
  alignas(64) std::atomic<uint64_t> bottom; // used by both owner (pop) and
                                            // producer (push)
  uint64_t cachedTop{0};
  uint64_t cachedBottom{0};

  uint64_t capacity;
  Buffer<T> storage;

public:
  LFDeque(uint64_t cap) : top(0), bottom(0), capacity(cap), storage(cap) {}

  bool push(T *val) { // used by producer alone
    auto btm = bottom.load(
        std::memory_order_relaxed); // we (producer) are using this hence relax

    if ((btm - cachedTop) >= static_cast<uint64_t>(capacity)) {
      cachedTop =
          top.load(std::memory_order_acquire); // someone else is using and
                                               // we need to coordinate

      if ((btm - cachedTop) >= static_cast<uint64_t>(capacity)) {
        return false;
      }
    }

    storage.buf[btm & storage.mask].store(val, std::memory_order_relaxed);

    // bottom.fetch_add(1, std::memory_order_release); // this is slower cause
    // first acquire then update then release

    bottom.store(btm + 1,
                 std::memory_order_release); // faster -> update then release

    return true;
  }

  T *pop() {
    cachedBottom = bottom.load(std::memory_order_relaxed);
    cachedBottom--;
    bottom.store(cachedBottom, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    cachedTop = top.load(std::memory_order_acquire);

    if (cachedBottom < cachedTop) {
      // queue is empty
      bottom.store(cachedTop, std::memory_order_release);
      return nullptr;
    }

    T *val = storage.buf[cachedBottom & storage.mask].load(
        std::memory_order_relaxed);
    if (cachedBottom > cachedTop) {
      // many elements are present
      return val;
    }

    // last element is present
    if (!top.compare_exchange_strong(cachedTop, cachedTop + 1,
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      val = nullptr;
    }
    cachedBottom = cachedTop + 1;
    bottom.store(cachedBottom, std::memory_order_release);
    return val;
  }

  T *steal() {
    cachedTop = top.load(std::memory_order_acquire);
    cachedBottom = bottom.load(std::memory_order_acquire);

    if (cachedBottom <= cachedTop) {
      // empty
      return nullptr;
    }
    T *val =
        storage.buf[cachedTop & storage.mask].load(std::memory_order_relaxed);

    if (!top.compare_exchange_strong(cachedTop, cachedTop + 1,
                                     std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      return nullptr;
    }
    return val;
  }
};
