// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "atomic_expensive_wait_cheap_notify_simple.h"

#include "aewcn_rl_tests.h"

int main() {
  return run_all_aewcn_tests<tools::atomic_expensive_wait_cheap_notify_simple<int>>()
      ? 0 : 1;
}
