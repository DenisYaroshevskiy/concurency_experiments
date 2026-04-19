// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <utils.h>

namespace tools {

class condition_waiter : nomove {
 public:
  template <typename Pred>
  void wait_until(Pred pred) {
    waiting_.store(true, memory_order_relaxed);
    asymmetric_thread_fence_heavy();
    if (pred()) {
      waiting_.store(false, memory_order_relaxed);
      return;
    }
    // Should there be a heavy fence here to prevent
    // the loads on wait happening before pred.
    waiting_.wait(true, memory_order_relaxed);
  }

  void notify_if_waiting() {
    // This light fence is to communicate with wait.
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
