// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "rcu_tls_reclaimer.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

// Basic pipeline: owner pushes 2 tasks, background propagates twice.
// In some interleavings the second push drains a ready task (owner-drain path);
// in all cases after() collects whatever remains.
struct reclaimer_push_propagate : rl::test_suite<reclaimer_push_propagate, 2> {
  rcu_tls_reclaimer rec;
  rl::atomic<const rl::var<int>*> shared{nullptr};

  void before() { shared.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread(unsigned idx) {
    if (idx == 0) {
      auto* old = shared.exchange(new rl::var<int>(2), rl::memory_order_acq_rel);
      rec.push([old] { delete old; }, 1);
      rec.push([] {}, 2);  // second push may drain ready tasks
    } else {
      rec.propagate();
      rec.propagate();
    }
  }

  void after() {
    auto tasks = rec.get_all_tasks();
    for (auto& t : tasks) t();
    delete shared.load(rl::memory_order_acquire);
  }
};

// Steal path: owner pushes, background propagates twice then steals.
struct reclaimer_steal : rl::test_suite<reclaimer_steal, 2> {
  rcu_tls_reclaimer rec;
  rl::atomic<const rl::var<int>*> shared{nullptr};

  void before() { shared.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread(unsigned idx) {
    if (idx == 0) {
      auto* old = shared.exchange(new rl::var<int>(2), rl::memory_order_acq_rel);
      rec.push([old] { delete old; }, 1);
    } else {
      rec.propagate();
      rec.propagate();
      auto tasks = rec.try_steal_ready();
      for (auto& t : tasks) t();
    }
  }

  void after() {
    auto tasks = rec.get_all_tasks();
    for (auto& t : tasks) t();
    delete shared.load(rl::memory_order_acquire);
  }
};

// Barrier path: owner pushes, another thread calls get_all_tasks concurrently.
struct reclaimer_get_all : rl::test_suite<reclaimer_get_all, 2> {
  rcu_tls_reclaimer rec;
  rl::atomic<const rl::var<int>*> shared{nullptr};

  void before() { shared.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread(unsigned idx) {
    if (idx == 0) {
      auto* old = shared.exchange(new rl::var<int>(2), rl::memory_order_acq_rel);
      rec.push([old] { delete old; }, 1);
    } else {
      auto tasks = rec.get_all_tasks();
      for (auto& t : tasks) t();
    }
  }

  void after() {
    auto tasks = rec.get_all_tasks();
    for (auto& t : tasks) t();
    delete shared.load(rl::memory_order_acquire);
  }
};

// Regression: third propagate with empty syncing_ must still return the oldest
// time of tasks already sitting in ready_, not max.
struct reclaimer_propagate_returns_oldest_ready
    : rl::test_suite<reclaimer_propagate_returns_oldest_ready, 1> {
  rcu_tls_reclaimer rec;

  void thread(unsigned /*idx*/) {
    rec.push([] {}, 42);

    // Cycle 1: waiting→syncing; nothing in ready yet.
    auto t1 = rec.propagate();
    RL_ASSERT(t1 == std::numeric_limits<rcu_tls_reclaimer::time_proxy>::max());

    // Cycle 2: syncing→ready; ready now holds the task.
    auto t2 = rec.propagate();
    RL_ASSERT(t2 == 42);

    // Cycle 3: syncing is empty again, but ready still has the task.
    auto t3 = rec.propagate();
    RL_ASSERT(t3 == 42);
  }

  void after() {
    auto tasks = rec.try_steal_ready();
    for (auto& t : tasks) t();
  }
};

int main() {
  rl::simulate<reclaimer_push_propagate>();
  rl::simulate<reclaimer_steal>();
  rl::simulate<reclaimer_get_all>();
  rl::simulate<reclaimer_propagate_returns_oldest_ready>();
}
