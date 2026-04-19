// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <utils.h>

#include <algorithm>

namespace tools {

/*
 * The reader-tracking half of RCU.
 *
 * Maintains a per-thread counter vec and a generation counter.
 * synchronize() advances the generation and waits until all readers that
 * entered before the advance have exited.
 *
 * supports nested entering
 * supports blocking waits for the synchronize.
 */
class rcu_reading_subsystem {
 public:
  using counter_t = std::uint64_t;

  class tls;

  counter_t generation() const {
    return generation_.load(tools::memory_order_relaxed);
  }

  void synchronize();

 private:
  tools::mutex reader_tls_vec_m;
  std::vector<tls*> reader_tls_vec;
  tools::atomic<counter_t> generation_{1};
};

class rcu_reading_subsystem::tls : tools::nomove {
 public:
  explicit tls(rcu_reading_subsystem& s) : subsystem_(&s) {
    tools::lock_guard _{subsystem_->reader_tls_vec_m};
    subsystem_->reader_tls_vec.push_back(this);
  }

  ~tls() {
    tools::lock_guard _{subsystem_->reader_tls_vec_m};
    std::iter_swap(std::ranges::find(subsystem_->reader_tls_vec, this),
                   std::prev(subsystem_->reader_tls_vec.end()));
    subsystem_->reader_tls_vec.pop_back();
  }

  void enter() {
    counter_t g = subsystem_->generation_.load(tools::memory_order_relaxed);
    counter_t cur = counter_.load(tools::memory_order_relaxed);

    if (cur) [[unlikely]] {
      ++nested_readers_;
      return;
    }

    counter_.store(g, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }
  void exit() {
    if (nested_readers_) [[unlikely]] {
      --nested_readers_;
      return;
    }

    // This light fence is to communicate with rcu::sync.
    tools::asymmetric_thread_fence_light();
    counter_.store(0, tools::memory_order_relaxed);

    // This light fence is to communicate with wait.
    tools::asymmetric_thread_fence_light();

    if (waiting_.load(tools::memory_order_relaxed)) [[unlikely]] {
      counter_.notify_one();
      // folly has this clear but it gives me a bug, when we reenter with the same
      // generation - it's possible for the notify_one above to be mised.
      // waiting_.store(false, tools::memory_order_relaxed);
    }
  }

  bool is_reading(counter_t desired) const {
    counter_t cur = counter_.load(tools::memory_order_relaxed);
    return 0 < cur && cur < desired;
  }

  // At most one waiter
  void wait(counter_t desired) {
    counter_t cur = counter_.load(tools::memory_order_relaxed);
    if (cur == 0 || cur >= desired) {
      return;
    }
    waiting_.store(true, tools::memory_order_relaxed);

    // Makes sure that the reader will see the waiting signal before we sleep.
    tools::asymmetric_thread_fence_heavy();

    counter_.wait(cur, tools::memory_order_relaxed);
    // Counter is guaranteed to have changed, that's why we don't need to check,
    // that's how the wait API works.
    // However, we can't really test `is_reading(desired)` - that might be false,
    // because the reader might have re-entered the critical section with the same counter value.
    // Good news is that we don't care, the - heavy fence from sync has to syncrhonize with light
    // fence in entry, so we are OK.

    // I'm not sure I need this fence.
    // The goal here is to not clear the "waiting" signal before doing the wait.
    // However it's possible that "wait" gives us enough ordering.
    // Folly doesn't have this fence:
    // https://github.com/facebook/folly/blob/main/folly/synchronization/detail/ThreadCachedReaders.h#L184
    //
    // It's also OK to just completely remove the next two lines - then only the
    // reader clears the waiting_ signal - there is a tiny chance of an extra
    // notification.
    tools::asymmetric_thread_fence_heavy();
    waiting_.store(false, tools::memory_order_relaxed);
  }

 private:
  friend class rcu_reading_subsystem;

  // There is a false sharing in theory here but
  // this is not the case where we expect it to be relevant.
  tools::atomic<counter_t> counter_{0};
  std::uint32_t nested_readers_ = 0;
  tools::atomic<bool> waiting_{false};

  rcu_reading_subsystem* subsystem_;
};

inline void rcu_reading_subsystem::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{reader_tls_vec_m};

  counter_t desired = generation_.load(tools::memory_order_relaxed) + 1;
  generation_.store(desired, tools::memory_order_relaxed);

  std::vector<tls*> waiting;
  waiting.reserve(reader_tls_vec.size());
  std::ranges::copy_if(
      reader_tls_vec, std::back_inserter(waiting),
      [desired](const tls* x) { return x->is_reading(desired); });

  for (auto* tls : waiting) {
    tls->wait(desired);
  }

  tools::asymmetric_thread_fence_heavy();
}

}  // namespace tools
