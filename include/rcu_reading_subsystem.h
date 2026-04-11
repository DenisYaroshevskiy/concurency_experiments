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
    counter.store(g, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }
  void exit() {
    tools::asymmetric_thread_fence_light();
    counter.store(0, tools::memory_order_relaxed);
  }

  rcu_reading_subsystem& subsystem() const {
    return *subsystem_;
  }

 private:
  friend class rcu_reading_subsystem;

  tools::atomic<counter_t> counter{0};
  rcu_reading_subsystem* subsystem_;
};

inline void rcu_reading_subsystem::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{reader_tls_vec_m};

  counter_t desired = generation_.load(tools::memory_order_relaxed) + 1;
  generation_.store(desired, tools::memory_order_relaxed);

  while (true) {
    bool wait_more =
        std::ranges::any_of(reader_tls_vec, [desired](const tls* x) {
          counter_t c = x->counter.load(tools::memory_order_relaxed);
          return 0 < c && c < desired;
        });

    if (!wait_more) break;
    tools::this_thread_yield();
  }
  tools::asymmetric_thread_fence_heavy();
}

}  // namespace tools
