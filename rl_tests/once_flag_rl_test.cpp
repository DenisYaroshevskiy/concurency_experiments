// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#define TOOLS_RL_TEST

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include "rl_simulate.h"

#include "once_flag.h"

struct once_flag_runs_once : rl::test_suite<once_flag_runs_once, 3> {
  tools::once_flag flag;
  rl::var<int> count{0};

  void thread(unsigned) {
    tools::call_once(flag, [&] { count($) += 1; });
    RL_ASSERT(count($) == 1);
  }
};

int main() {
  return simulate<once_flag_runs_once>() ? 0 : 1;
}
