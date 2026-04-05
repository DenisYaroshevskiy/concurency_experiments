// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>

#include <array>
#include <concepts>

namespace tools {

/*
 * A communication between two threads: owner and stealer.
 * * Owner always has access to an object.
 * * Stealer might get access to the object, if the owner isn't using it.
 *   When accessing it replaces the object with a clear back up object, so that
 *   the owner can continue.
 *
 * NOTE: the stealer is reponsible for cleaning up the object that'll be reused
 *
 * active_ encodes three states:
 *   &objs_[0] / &objs_[1]  — idle, owner's current buffer
 *   nullptr                 — owner holds the lock
 *   objs_.end()             — stealer is sleeping (blocking_stealer_access)
 *
 * In our case T is typically a container of tasks.
 * Owner can always access a container of tasks.
 * The stealer might try to grab the tasks.
 */

template <typename T>
class owner_stealer {
 public:
  owner_stealer() = default;
  owner_stealer(const owner_stealer&) = delete;
  owner_stealer(owner_stealer&&) = delete;
  owner_stealer& operator=(const owner_stealer&) = delete;
  owner_stealer& operator=(owner_stealer&&) = delete;

  template <std::invocable<T&> F>
  void owner_access(F&& f);

  template <std::invocable<T&> F>
  bool try_stealer_access(F&& f);

  template <std::invocable<T&> F>
  void blocking_stealer_access(F&& f);

 private:
  std::array<tools::var<T>, 2> objs_;
  tools::atomic<tools::var<T>*> active_{&objs_[0]};

  tools::var<T>* sleeping_sentinel() { return objs_.data() + 2; }

  void notify_stealer();
  void wait_for_owner();
};

template <typename T>
template <std::invocable<T&> F>
void owner_stealer<T>::owner_access(F&& f) {
  auto* ptr = active_.exchange(nullptr, tools::memory_order_acquire);
  std::forward<F>(f)(ptr->write());

  if (active_.exchange(ptr, tools::memory_order_release) != nullptr)
      [[unlikely]] {
    notify_stealer();
  }
}

template <typename T>
void owner_stealer<T>::notify_stealer() {
  active_.notify_one();
}

template <typename T>
template <std::invocable<T&> F>
bool owner_stealer<T>::try_stealer_access(F&& f) {
  auto* cur = active_.load(tools::memory_order_relaxed);
  if (!cur) return false;
  auto* backup = (cur == &objs_[0]) ? &objs_[1] : &objs_[0];
  if (!active_.compare_exchange_strong(cur, backup, tools::memory_order_acq_rel,
                                       tools::memory_order_relaxed)) {
    return false;
  }
  std::forward<F>(f)(cur->write());
  return true;
}

template <typename T>
template <std::invocable<T&> F>
void owner_stealer<T>::blocking_stealer_access(F&& f) {
  while (!try_stealer_access(std::forward<F>(f))) {
    wait_for_owner();
  }
}

template <typename T>
void owner_stealer<T>::wait_for_owner() {
  tools::var<T>* expected = nullptr;
  if (active_.compare_exchange_strong(expected, sleeping_sentinel(),
                                      tools::memory_order_relaxed,
                                      tools::memory_order_relaxed)) {
    active_.wait(sleeping_sentinel());
  }
}

}  // namespace tools
