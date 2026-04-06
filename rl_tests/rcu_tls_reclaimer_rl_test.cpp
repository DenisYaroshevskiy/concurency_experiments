// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "rcu_tls_reclaimer.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

// Task added at gen G becomes ready only when current_gen >= G+2.
struct reclaimer_owner_cleans : rl::test_suite<reclaimer_owner_cleans, 1> {
  tools::rcu_tls_reclaimer r;
  rl::var<int> executed{0};

  void thread(unsigned) {
    RL_ASSERT(!r.oldest_unreclaimed().has_value());

    auto cnt = r.owner_reclaim(1, [this] { executed($) = 1; });
    RL_ASSERT(cnt == 1);
    RL_ASSERT(executed($) == 0);
    RL_ASSERT(r.oldest_unreclaimed() == 1);

    cnt = r.clean_ready_tasks(2);
    RL_ASSERT(cnt == 1);  // gen 1+2=3 > 2: not yet
    RL_ASSERT(executed($) == 0);
    RL_ASSERT(r.oldest_unreclaimed() == 1);

    cnt = r.clean_ready_tasks(3);
    RL_ASSERT(cnt == 0);  // gen 1+2=3 <= 3: ready
    RL_ASSERT(executed($) == 1);
    RL_ASSERT(!r.oldest_unreclaimed().has_value());
  }
};

// A stale task is cleaned inline when the next owner_reclaim arrives.
struct reclaimer_inline_clean : rl::test_suite<reclaimer_inline_clean, 1> {
  tools::rcu_tls_reclaimer r;
  rl::var<int> t1_done{0}, t2_done{0};

  void thread(unsigned) {
    r.owner_reclaim(1, [this] { t1_done($) = 1; });
    auto cnt = r.owner_reclaim(3, [this] { t2_done($) = 1; });
    RL_ASSERT(t1_done($) == 1);  // gen 1+2=3 <= 3: cleaned inline
    RL_ASSERT(t2_done($) == 0);  // gen 3+2=5 > 3: not yet
    RL_ASSERT(cnt == 1);
    RL_ASSERT(r.oldest_unreclaimed() == 3);
  }
};

// Three tasks at different gens; only the two earliest are ready at gen=4.
struct reclaimer_partial_clean : rl::test_suite<reclaimer_partial_clean, 1> {
  tools::rcu_tls_reclaimer r;
  rl::var<int> cleaned{0};

  void thread(unsigned) {
    r.owner_reclaim(1, [this] { cleaned($) += 1; });
    r.owner_reclaim(2, [this] { cleaned($) += 1; });
    r.owner_reclaim(5, [this] { cleaned($) += 1; });

    auto cnt = r.clean_ready_tasks(4);
    RL_ASSERT(cnt == 1);         // gen 5 remains
    RL_ASSERT(cleaned($) == 2);  // gen 1 and 2 cleaned
    RL_ASSERT(r.oldest_unreclaimed() == 5);
  }
};

// Owner adds a task too young to self-clean; stealer loops until it gets it.
struct reclaimer_try_steal : rl::test_suite<reclaimer_try_steal, 2> {
  tools::rcu_tls_reclaimer r;
  rl::atomic<int> executed{0};

  void thread(unsigned idx) {
    if (idx == 0) {
      r.owner_reclaim(1,
                      [this] { executed.store(1, rl::memory_order_relaxed); });
    } else {
      std::vector<tools::rcu_tls_reclaimer::task> tasks;
      while (tasks.empty()) {
        r.try_steal_tasks(tasks);
        if (tasks.empty()) rl::yield(1, $);
      }
      for (auto& t : tasks) t();
    }
  }

  void after() { RL_ASSERT(executed.load(rl::memory_order_relaxed) == 1); }
};

// oldest_unreclaimed is set inside owner_access (during do_clean), so the
// stealer waiting on it guarantees the task is in the buffer before stealing.
// This mirrors the owner_stealer_blocking pattern: signal inside the lock,
// stealer waits for it, then blocking-steals and asserts directly.
struct reclaimer_steal_blocking : rl::test_suite<reclaimer_steal_blocking, 2> {
  tools::rcu_tls_reclaimer r;
  rl::atomic<int> executed{0};
  tools::atomic<bool> reclaim_started = false;

  void thread(unsigned idx) {
    if (idx == 0) {
      r.owner_reclaim(1, [this] {
        executed.store(1, rl::memory_order_relaxed);
      });
      reclaim_started.store(true, rl::memory_order_relaxed);
    } else {
      while (!reclaim_started.load(rl::memory_order_relaxed)) rl::yield(1, $);
      std::vector<tools::rcu_tls_reclaimer::task> tasks;
      r.steal_tasks_blocking(tasks);
      for (auto& t : tasks) t();
      RL_ASSERT(executed.load(rl::memory_order_relaxed) == 1);
    }
  }
};

int main() {
  rl::simulate<reclaimer_owner_cleans>();
  rl::simulate<reclaimer_inline_clean>();
  rl::simulate<reclaimer_partial_clean>();
  rl::simulate<reclaimer_try_steal>();
  rl::simulate<reclaimer_steal_blocking>();
}
