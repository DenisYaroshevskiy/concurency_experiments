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
      not_standard_situtation.fetch_add(1, tools::memory_order_relaxed);
      return;
    }

    counter.store(g, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }
  void exit() {
    counter_t unusual =
        not_standard_situtation.load(tools::memory_order_relaxed);
    if (unusual) [[unlikely]] {
      unusual_exit(unusual);
      return;
    }

    tools::asymmetric_thread_fence_light();
    counter.store(0, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();

    counter_t waiting = not_standard_situtation.load(tools::memory_order_relaxed);
    if (waiting) [[unlikely]] {
      counter.notify_one();
    }
  }

  // At most one waiter
  void wait(counter_t desired) {
    counter_t c = counter.load(tools::memory_order_relaxed);
    if (0 == c || c >= desired) {
      return;
    }
    set_waiting_bit();
    tools::asymmetric_thread_fence_heavy();
    counter.wait(c, tools::memory_order_relaxed);
    clear_waiting_bit();

    // it is possible that this wait always immediately returns
    // but I find it not obvious to prove.
    wait(desired);
  }

  rcu_reading_subsystem& subsystem() const { return *subsystem_; }

 private:
  friend class rcu_reading_subsystem;

  static_assert(sizeof(counter_t) == 8, "");
  static constexpr counter_t waiting_bit = (counter_t)1 << 63;

  void unusual_exit(counter_t unusual) {
    bool is_shared = unusual & ~waiting_bit;
    if (is_shared) {
      not_standard_situtation.fetch_sub(1, tools::memory_order_relaxed);
      return;
    }

    // If we are here - means the sync is waiting.

    // exit the critical section.
    {
      tools::asymmetric_thread_fence_light();
      counter.store(0, tools::memory_order_relaxed);
      tools::asymmetric_thread_fence_light();
    }

    // are still waiting?
    unusual = not_standard_situtation.load(tools::memory_order_relaxed);
    if (!unusual) {
      return;
    }

    clear_waiting_bit();
    counter.notify_one();
  }

  void set_waiting_bit() {
    counter_t cur = not_standard_situtation.load(tools::memory_order_relaxed);
    while (!not_standard_situtation.compare_exchange_weak(
        cur, cur | waiting_bit, tools::memory_order_relaxed));
  }

  void clear_waiting_bit() {
    counter_t cur = not_standard_situtation.load(tools::memory_order_relaxed);

    // it's possible that we don't need the spin here.
    while (!not_standard_situtation.compare_exchange_weak(
        cur, cur & ~waiting_bit, tools::memory_order_relaxed));
  }

  tools::atomic<counter_t> counter{0};

  // a counter that indicates something unsual is happening
  // bits below the top one -
  tools::atomic<counter_t> not_standard_situtation{0};

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
