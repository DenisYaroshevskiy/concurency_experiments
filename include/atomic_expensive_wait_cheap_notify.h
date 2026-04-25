// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <utils.h>

namespace tools {

class atomic_expensive_wait_cheap_notify : nomove {
 public:
  template <typename T>
  void wait(const tools::atomic<T>* obj, T old) {
    waiting_.store(true, memory_order_relaxed);
    tools::asymmetric_thread_fence_heavy();
    if (obj->load(memory_order_relaxed) != old) {
      waiting_.store(false, memory_order_relaxed);
      return;
    }
    waiting_.wait(true, memory_order_relaxed);
  }

  template <typename T>
  void notify_one(tools::atomic<T>*) {
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
