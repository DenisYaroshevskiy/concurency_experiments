// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "condition_waiter.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include "rl_simulate.h"

// State is "done" when outside [1, 2].
static bool done(int s) { return s < 1 || s > 2; }

// notify_if_waiting with no waiter must not crash or hang.
struct condition_waiter_notify_no_waiter
    : rl::test_suite<condition_waiter_notify_no_waiter, 1> {
  tools::condition_waiter waiter;

  void thread(unsigned) { waiter.notify_if_waiting(); }
};

// One writer sets state outside [1,2] and notifies; one reader waits.
struct condition_waiter_one_wait
    : rl::test_suite<condition_waiter_one_wait, 2> {
  rl::atomic<int> state{1};
  tools::condition_waiter waiter;

  void thread(unsigned idx) {
    if (idx == 0) {
      state.store(3, rl::memory_order_relaxed);
      waiter.notify_if_waiting();
    } else {
      waiter.wait_until([&] { return done(state.load(rl::memory_order_relaxed)); });
      RL_ASSERT(done(state.load(rl::memory_order_relaxed)));
    }
  }
};

// Writer bounces state 1->2->1->2->3; reader must eventually unblock.
struct condition_waiter_abab : rl::test_suite<condition_waiter_abab, 2> {
  rl::atomic<int> state{1};
  tools::condition_waiter waiter;

  void thread(unsigned idx) {
    if (idx == 0) {
      state.store(1, rl::memory_order_relaxed);
      state.store(0, rl::memory_order_relaxed);
      waiter.notify_if_waiting();
      state.store(1, rl::memory_order_relaxed);
      state.store(0, rl::memory_order_relaxed);
      waiter.notify_if_waiting();
    } else {
      waiter.wait_until([&] { return done(state.load(rl::memory_order_relaxed)); });
    }
  }
};

int main() {
  return (simulate_exhaustive<condition_waiter_notify_no_waiter>()
       && simulate_exhaustive<condition_waiter_one_wait>()
       && simulate<condition_waiter_abab>()) ? 0 : 1;
}
