// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "atomic_expensive_wait_cheap_notify.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include "rl_simulate.h"


// notify_one with no waiter must not crash or hang.
struct notify_no_waiter_test
    : rl::test_suite<notify_no_waiter_test, 1> {
  tools::atomic<int> state{0};
  tools::atomic_expensive_wait_cheap_notify waiter;

  void thread(unsigned) { waiter.notify_one(&state); }
};

struct one_wait_test
    : rl::test_suite<one_wait_test, 2> {
  tools::atomic<int> state{0};
  tools::atomic_expensive_wait_cheap_notify waiter;

  void thread(unsigned idx) {
    if (idx == 0) {
      state.store(1, tools::memory_order_relaxed);
      state.store(0, tools::memory_order_relaxed);
      waiter.notify_one(&state);
    } else {
      int old = state.load(tools::memory_order_relaxed);
      if (old) {
        waiter.wait(&state, old);
      }
    }
  }
};

struct one_wait_actual_waiting_test
    : rl::test_suite<one_wait_actual_waiting_test, 2> {
  tools::atomic<int> state{1};
  tools::atomic_expensive_wait_cheap_notify waiter;

  void thread(unsigned idx) {
    if (idx == 0) {
      state.store(0, tools::memory_order_relaxed);
      waiter.notify_one(&state);
    } else {
      int old = state.load(tools::memory_order_relaxed);
      if (old) {
        waiter.wait(&state, old);
      }
      RL_ASSERT(state.load(tools::memory_order_relaxed) == 0);
    }
  }
};


// two criitcal sections, one wait
struct abab_test : rl::test_suite<abab_test, 2> {
  tools::atomic<int> state{1};
  tools::atomic_expensive_wait_cheap_notify waiter;

  void thread(unsigned idx) {
    if (idx == 0) {
      state.store(1, tools::memory_order_relaxed);
      state.store(0, tools::memory_order_relaxed);
      waiter.notify_one(&state);
      state.store(1, tools::memory_order_relaxed);
      state.store(0, tools::memory_order_relaxed);
      waiter.notify_one(&state);
    } else {
      int old = state.load(tools::memory_order_relaxed);
      if (old) {
        waiter.wait(&state, old);
      }
    }
  }
};

int main() {
  return (simulate_exhaustive<notify_no_waiter_test>()
       && simulate_exhaustive<one_wait_test>()
       && simulate_exhaustive<one_wait_actual_waiting_test>()
       && simulate<abab_test>()) ? 0 : 1;
}
