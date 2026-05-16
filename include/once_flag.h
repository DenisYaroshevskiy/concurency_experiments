// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on


// This is copied from https://github.com/facebook/folly/blob/f188cd62672ac239623096f0556f53cacadfff9f/folly/synchronization/CallOnce.h#L1

#pragma once

#include <cstdint>
#include <functional>

#include "atomic_wrappers.h"
#include "utils.h"

namespace tools {



// Compact once_flag: 3-state machine, no mutex.
// kDone=0 so test_once() is a single test instruction.
// kInit=1: not yet started. kActive=2: callable running.
// Waiters block on state_.wait(kActive) and are woken via notify_all().
class once_flag {
 public:
  constexpr once_flag() noexcept = default;
  once_flag(const once_flag&) = delete;
  once_flag& operator=(const once_flag&) = delete;

  bool test_once() const noexcept {
    return state_.load(memory_order_acquire) == kDone;
  }

 private:
  static constexpr uint8_t kDone = 0;
  static constexpr uint8_t kInit = 1;
  static constexpr uint8_t kActive = 2;

  template <typename F>
  void call_once_impl(F&& f) {
    while (true) {
      uint8_t expected = kInit;
      if (state_.compare_exchange_weak(
              expected, kActive, memory_order_acquire)) {
        bool pass = false;
        scope_exit guard{[&] {
          state_.store(pass ? kDone : kInit, memory_order_release);
          state_.notify_all();
        }};
        f();
        pass = true;
        return;
      }
      if (expected == kDone) return;
      state_.wait(kActive);
    }
  }

  template <typename F, typename... Args>
  friend void call_once(once_flag& flag, F&& f, Args&&... args);

  tools::atomic<uint8_t> state_{kInit};
};

template <typename F, typename... Args>
void call_once(once_flag& flag, F&& f, Args&&... args) {
  if (flag.test_once()) return;
  flag.call_once_impl(
      [&] { std::invoke(std::forward<F>(f), std::forward<Args>(args)...); });
}

}  // namespace tools
