// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "rcu_3.h"

#include "rcu_rl_tests.h"

struct rcu_v3_small_cfg : v3::rcu_domain {
  rcu_v3_small_cfg()
      : v3::rcu_domain{v3::rcu_domain::config{
            .retire_threshold = 1,
            .stale_gen_threshold = 1,
        }} {}
};


// threshold=2, stale=1: triggers garbage_collect() which calls
// collect_stale_tasks() and steals tasks from other threads' reclaimers.
struct rcu_v3_cfg_stale : v3::rcu_domain {
  rcu_v3_cfg_stale()
      : v3::rcu_domain{v3::rcu_domain::config{
            .retire_threshold = 2,
            .stale_gen_threshold = 1,
        }} {}
};

int main() {
  return (full_test<v3::rcu_domain>()
       && full_test<rcu_v3_small_cfg>()
       && full_test<rcu_v3_cfg_stale>()) ? 0 : 1;
}
