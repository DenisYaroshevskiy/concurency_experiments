// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <utils.h>

namespace tools {

// One reader -
// Two readers - one after the other
// herd7

class condition_waiter : nomove {
 public:
  template <typename Pred>
  void wait_if_not(Pred pred) {
    waiting_.store(true, memory_order_relaxed);
    tools::asymmetric_thread_fence_heavy();
    // load waiting as true
    // -> missed notification
    // new reader - pred => false
    if (pred()) {
      waiting_.store(false, memory_order_relaxed);
      return;
    }
    // reader exits, waiting_ is false, no notification

    // asymmetric_thread_fence_heavy 
   
    // waiting is going to wait
    waiting_.wait(true, memory_order_relaxed);
  }

  void notify_if_waiting() {
    tools::asymmetric_thread_fence_light();
    if (waiting_.load(memory_order_relaxed)) {
      waiting_.store(false, memory_order_relaxed);
      waiting_.notify_one();
    }
  }

 private:
  tools::atomic<bool> waiting_{false};
};

}  // namespace tools
