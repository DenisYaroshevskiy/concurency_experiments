// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <utils.h>

namespace tools {

template <typename T>
class atomic_expensive_wait_cheap_notify_simple : nomove {
 public:
  explicit atomic_expensive_wait_cheap_notify_simple(tools::atomic<T>* obj)
      : obj_(obj) {}

  void wait(T old) {
    waiting_.store(true, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_heavy();
    obj_->wait(old, tools::memory_order_relaxed);
    waiting_.store(false, tools::memory_order_relaxed);
  }

  void notify_one() {
    tools::asymmetric_thread_fence_light();
    if (waiting_.load(tools::memory_order_relaxed)) {
      obj_->notify_one();
    }
  }

 private:
  tools::atomic<T>* obj_;
  tools::atomic<bool> waiting_{false};
};

}  // namespace tools
