// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#ifndef AEWCN_RL_TESTS_H
#define AEWCN_RL_TESTS_H

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include "rl_simulate.h"

template <typename Waiter>
struct notify_no_waiter_test : rl::test_suite<notify_no_waiter_test<Waiter>, 1> {
  tools::atomic<int> state{0};
  Waiter waiter;

  void thread(unsigned) { waiter.notify_one(&state); }
};

template <typename Waiter>
struct one_wait_test : rl::test_suite<one_wait_test<Waiter>, 2> {
  tools::atomic<int> state{0};
  Waiter waiter;

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

template <typename Waiter>
struct one_wait_actual_waiting_test
    : rl::test_suite<one_wait_actual_waiting_test<Waiter>, 2> {
  tools::atomic<int> state{1};
  Waiter waiter;

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

template <typename Waiter>
struct abab_test : rl::test_suite<abab_test<Waiter>, 2> {
  tools::atomic<int> state{1};
  Waiter waiter;

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

template <typename Waiter>
bool run_all_aewcn_tests() {
  return simulate_exhaustive<notify_no_waiter_test<Waiter>>()
      && simulate_exhaustive<one_wait_test<Waiter>>()
      && simulate_exhaustive<one_wait_actual_waiting_test<Waiter>>()
      && simulate<abab_test<Waiter>>();
}

#endif  // AEWCN_RL_TESTS_H
