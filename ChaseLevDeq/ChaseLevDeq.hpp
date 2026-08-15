#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <new>

template <typename T, size_t capacity> class ChaseLevDeq {
private:
  std::array<std::atomic<T *>, capacity> deq;
  size_t mask = capacity - 1;

  /* used by theives for steal items
   * top is never decremented
   */
  alignas(
      std::hardware_destructive_interference_size) std::atomic<uint64_t> top{0};

  /* owned by owner to push and pop items*/
  alignas(std::hardware_destructive_interference_size)
      std::atomic<uint64_t> bottom{0};

public:
  ChaseLevDeq() {
    static_assert(capacity > 0);
    static_assert(std::has_single_bit(capacity));

    for (auto &slot : deq)
      slot.store(nullptr, std::memory_order_relaxed);
  };

  bool pushBottom(T *&obj) noexcept { // R value reference
    /* since bottom  is owned by the owner it can be relaxed*/
    auto b = bottom.load(std::memory_order_relaxed);

    /* t is shared between both owner and thief hence acquire*/
    auto t = top.load(std::memory_order_acquire);

    if ((b - t) >= capacity) {
      return false;
    }

    deq[b & mask].store(obj, std::memory_order_relaxed);

    bottom.store(b + 1, std::memory_order_release);
    return true;
  }

  bool steal(T *&obj) noexcept { // L value reference
    /* top is shared between owner and thief hence acquire*/
    auto t = top.load(std::memory_order_acquire);

    /* owner owns bottom hence thief has to acquire*/
    auto b = bottom.load(std::memory_order_acquire);

    if (int64_t(b - t) <= 0) {
      return false;
    }

    T *potential_task = deq[t & mask].load(std::memory_order_relaxed);

    if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst)) {
      return false;
    }

    obj = potential_task;
    return true;
  }

  bool popBottom(T *&obj) noexcept { // L value reference

    auto b = bottom.load(std::memory_order_relaxed);
    b = b - 1;
    /* we need a seq_cst here because there is a possibility that the element
     * present in the deque is the last element, so both theif and the owner
     * will try to access it, this can lead to data race + if the invalidate
     * messages or the store buffers werent flushed in the other cpu cores that
     * could lead to them accessing incorrect value of bottom which can lead to
     * data corruption and data race*/
    bottom.store(b, std::memory_order_seq_cst);

    auto t = top.load(std::memory_order_acquire);

    const int64_t size = (b - t);
    if (size < 0) {
      /* empty deq*/
      bottom.store(t, std::memory_order_release);
      return false;
    }

    if (size > 0) {
      /* more than 1 element present*/
      obj = deq[b & mask].load(std::memory_order_relaxed);
      return true;
    }

    bool res = false;
    /* seq_cst required cause both owner and thief are accessing and trying to
     * modify at this stage */
    if (top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst)) {
      // success (we got the object)
      obj = deq[b & mask].load(std::memory_order_relaxed);
      res = true;
    }

    /* here we do t+1 because there was just one element present
     * at t, which we took, so now there is nothing at t, and also top has moved
     * to t+1, so we will also move the bottom to t+1 to show that there is no
     * element present in the deque*/
    bottom.store(t + 1, std::memory_order_release);
    return res;
  }
};
