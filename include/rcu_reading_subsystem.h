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
    counter_t cur = counter.load(tools::memory_order_relaxed);

    if (cur) [[unlikely]] {
      ++nested_readers_;
      return;
    }

    counter.store(g, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }
  void exit() {
    if (nested_readers_) [[unlikely]] {
      --nested_readers_;
      return;
    }

    tools::asymmetric_thread_fence_light();
    counter.store(0, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();

    if (waiting.load(tools::memory_order_relaxed)) [[unlikely]] {
      counter.notify_one();
      waiting_.store(false, tools::memory_order_relaxed)
    }
  }

  // At most one waiter
  void wait(counter_t desired) {
    counter_t cur = counter_.load(std::memory_order_relaxed);
    if (cur == 0 || cur >= desired) {
      return;
    }
    waiting_.store(true, tools::memory_order_relaxed);
    tools::asymmetric_fence_heavy();
    counter_.wait(cur, std::memory_order_relaxed);
    wait(desired);
  }

  rcu_reading_subsystem& subsystem() const { return *subsystem_; }

 private:
  friend class rcu_reading_subsystem;

  // There is a false sharing in theory here but
  // this is not the case where we expect it to be relevant.
  tools::atomic<counter_t> counter_{0};
  std::uint32_t nested_readers_;
  std::atomic<bool> waiting_;

  rcu_reading_subsystem* subsystem_;
};

inline void rcu_reading_subsystem::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{reader_tls_vec_m};

  counter_t desired = generation_.load(tools::memory_order_relaxed) + 1;
  generation_.store(desired, tools::memory_order_relaxed);

  std::vector<tls*> waiting;
  waiting.reserve(reader_tls_vec.size());
  std::copy_if(reader_tls_vec.begin(), reader_tls_vec.end(),
               std::back_inserter(waiting),
               [desired](const tls* x) {
                 counter_t c = x->counter.load(tools::memory_order_relaxed);
                 return 0 < c && c < desired;
               });

  for (auto* tls : waiting) {
    tls->wait(desired);
  }

  tools::asymmetric_thread_fence_heavy();
}

}  // namespace tools
