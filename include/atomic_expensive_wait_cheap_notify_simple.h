// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <utils.h>

namespace tools {

class atomic_expensive_wait_cheap_notify_simple : nomove {
 public:
  template <typename T>
  void wait(const tools::atomic<T>* obj, T old) {
    waiting_.store(true, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_heavy();
    obj->wait(old, tools::memory_order_relaxed);
    waiting_.store(false, tools::memory_order_relaxed);
  }

  template <typename T>
  void notify_one(tools::atomic<T>* obj) {
    tools::asymmetric_thread_fence_light();
    if (waiting_.load(tools::memory_order_relaxed)) {
      obj->notify_one();
    }
  }

 private:
  tools::atomic<bool> waiting_{false};
};

}  // namespace tools
