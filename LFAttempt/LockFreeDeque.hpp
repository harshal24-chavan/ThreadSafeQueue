
#pragma once
#include <atomic>
#include <cstdint>
#include <iostream>
#include <memory>

template <typename T> struct Buffer {
  std::unique_ptr<std::atomic<T *>[]> buf;
  uint64_t mask;

  Buffer(uint64_t capacity) {
    if ((capacity & (capacity - 1)) != 0) {
      throw std::invalid_argument("Capacity must be a power of 2");
    }

    buf = std::make_unique<std::atomic<T *>[]>(capacity);

    mask = capacity - 1;
  }
};

template <typename T> class LFDeque {
private:
  alignas(std::hardware_destructive_interference_size) std::atomic<int64_t> top;    // used by the thief(pop)
  alignas(std::hardware_destructive_interference_size) std::atomic<int64_t> bottom; // used by both owner (pop) and
                                            // producer (push)

  uint64_t capacity;
  Buffer<T> storage;

public:
  LFDeque(uint64_t cap) : top(0), bottom(0), capacity(cap), storage(cap) {}

  bool push(T *val) noexcept { // used by producer alone
    auto btm = bottom.load(
        std::memory_order_relaxed); // we (producer) are using this hence relax

    uint64_t tp =
        top.load(std::memory_order_acquire); // someone else is using

    if ((btm - tp) >= static_cast<uint64_t>(capacity)) {
      return false;
    }

    storage.buf[btm & storage.mask].store(val, std::memory_order_relaxed);

    // bottom.fetch_add(1, std::memory_order_release); // this is slower cause
    // first acquire then update then release

    bottom.store(btm + 1,
                 std::memory_order_release); // faster -> update then release

    return true;
  }

  T *pop() noexcept {
    uint64_t btm = bottom.load(std::memory_order_relaxed);
    btm--;
    bottom.store(btm, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    uint64_t tp = top.load(std::memory_order_acquire);

    if (btm < tp) {
      // queue is empty
      bottom.store(tp, std::memory_order_release);
      return nullptr;
    }

    T *val = storage.buf[btm & storage.mask].load(std::memory_order_relaxed);
    if (btm > tp) {
      // many elements are present
      return val;
    }

    // last element is present
    if (!top.compare_exchange_strong(tp, tp + 1, std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      val = nullptr;
    }
    btm = tp + 1;
    bottom.store(btm, std::memory_order_release);
    return val;
  }

  T *steal() noexcept {
    uint64_t tp = top.load(std::memory_order_acquire);
    uint64_t btm = bottom.load(std::memory_order_acquire);

    if (btm <= tp) {
      // empty
      return nullptr;
    }
    T *val = storage.buf[tp & storage.mask].load(std::memory_order_relaxed);

    if (!top.compare_exchange_strong(tp, tp + 1, std::memory_order_seq_cst,
                                     std::memory_order_relaxed)) {
      return nullptr;
    }
    return val;
  }
};
