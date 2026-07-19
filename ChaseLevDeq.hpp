#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <new>

template <typename T, size_t capacity> class ChaseLevDeq {
private:
  std::array<T, capacity> deq;
  size_t mask = capacity - 1;

  /* used by theives for steal items
   * top is never decremented
   */
  alignas(std::hardware_constructive_interference_size)
      std::atomic<uint64_t> top{0};

  /* owned by owner to push and pop items*/
  alignas(std::hardware_constructive_interference_size)
      std::atomic<uint64_t> bottom{0};

public:
  ChaseLevDeq() { static_assert(std::has_single_bit(capacity)); };

  bool pushBottom(T &&obj) { // R value reference
    /* since bottom  is owned by the owner it can be relaxed*/
    auto b = bottom.load(std::memory_order_relaxed);

    /* t is shared between both owner and thief hence acquire*/
    auto t = top.load(std::memory_order_acquire);

    if ((b - t) >= capacity) {
      return false;
    }

    deq[b & mask] = std::move(obj);

    bottom.store(b + 1, std::memory_order_release);
    return true;
  }

  bool steal(T &obj) { // L value reference
    /* top is shared between owner and thief hence acquire*/
    auto t = top.load(std::memory_order_acquire);

    /* owner owns bottom hence thief has to acquire*/
    auto b = bottom.load(std::memory_order_acquire);

    if (int64_t(b - t) <= 0) {
      return false;
    }

    if (!top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst)) {
      return false;
    }

    obj = std::move(deq[t & mask]);
    return true;
  }

  bool popBottom(T &obj) { // L value reference

    auto b = bottom.load(std::memory_order_relaxed);
    b = b - 1;
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
      obj = std::move(deq[b & mask]);
      return true;
    }

    bool res = false;
    /* seq_cst required cause both owner and thief are accessing and trying to
     * modify at this stage, tho I am a bit confused in this section*/
    if (top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst)) {
      // success (we got the object)
      obj = std::move(deq[b & mask]);
      res = true;
    }

    bottom.store(t + 1, std::memory_order_release);
    return res;
  }
};
