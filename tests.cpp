#include "TSQueue.h"

#include <iostream>
#include <cassert>
#include <iostream>
#include <memory>

void run_unit_tests() {
  std::cout << "Running Unit Tests..." << std::endl;

  // Test 1: Move Semantics with unique_ptr
  {
    TSQueue<std::unique_ptr<int>> moveQ;
    auto p = std::make_unique<int>(100);

    moveQ.insert(std::move(p)); 
    assert(p == nullptr); // Ownership should be transferred!

    std::unique_ptr<int> result;
    moveQ.pop(result);
    assert(*result == 100);
    std::cout << "[PASS] Move Semantics Test" << std::endl;
  }

  // Test 2: Basic FIFO Logic
  {
    TSQueue<int> q;
    q.insert(10);
    q.insert(20);

    int v1, v2;
    q.pop(v1);
    q.pop(v2);
    assert(v1 == 10 && v2 == 20);
    std::cout << "[PASS] FIFO Logic Test" << std::endl;
  }

  std::cout << "All Unit Tests Passed!" << std::endl;
}
