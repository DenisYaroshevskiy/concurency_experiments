// Copyright 2025 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)


#define TOOLS_RL_TEST
#include "atrocious_mutex.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

struct atrocious_mutex_test_var : rl::test_suite<atrocious_mutex_test_var, 2> {
  rl::var<int> var;
  tools::atrocious_mutex m;

  void before() { var($) = 0; }

  void thread(unsigned) {
    std::lock_guard _{m};
    var($) += 1;
  }

  void after() { RL_ASSERT(var($) == 2); }
};

struct atrocious_mutex_test_atomic
    : rl::test_suite<atrocious_mutex_test_atomic, 2> {
  tools::atomic<int> atomic_var;
  tools::atrocious_mutex m;

  void before() { atomic_var.store(0, rl::mo_relaxed); }

  void thread(unsigned) {
    std::lock_guard _{m};
    int val = atomic_var.load(rl::mo_relaxed);
    atomic_var.store(val + 1, rl::mo_relaxed);
  }

  void after() { RL_ASSERT(atomic_var.load(rl::mo_relaxed) == 2); }
};

int main() {
  rl::simulate<atrocious_mutex_test_var>();
  rl::simulate<atrocious_mutex_test_atomic>();
}
