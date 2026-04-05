// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <atomic_wrappers.h>
#include <owner_stealer.h>

#include <functional>

namespace tools {

/*
 * rcu tls collection of tasks to reclaim.
 *
 * Owner adds tasks to the list, using owner_reclaim.
 * He also passes the current generation of the rcu. Anything that is 2 or more
 * generations ago gets executed (generation is increasing in the beginning of a
 * sync by 1).
 *
 * owner_reclaim returns the number of the unreclaimed tasks. If it's too much,
 * the user might trigger sync / and call clean_ready_tasks (part of
 * owner_reclaim).
 *
 * Once some number of generations, we will try to clean the stale tasks
 * (a thread called reclaim a few times but never cleared).
 * That uses `oldest_unreclaimed` to check if the thread didn't clean up for a
 * while and try_steal_tasks to get the tasks.
 *
 * rcu_barrier sometimes uses steal_tasks_blocking.
 *
 * try_steal_tasks and steal_tasks_blocking don't care for generation, since
 * they are doing a sync anyways.
 */

class rcu_tls_reclaimer {
 public:
  using counter_t = std::uint64_t;
  using task = std::move_only_function<void()>;

  // returns the number of unreclaimed tasks
  std::size_t owner_reclaim(counter_t current_gen, task t);
  std::size_t clean_ready_tasks(counter_t current_gen);

  counter_t oldest_unreclaimed() const;

  bool try_steal_tasks(std::vector<task>& here);
  bool steal_tasks_blocking(std::vector<task>& here);

 private:
  counter_t oldest_unreclaimed_ = std::numeric_limits<counter_t>::max();
  owner_stealer < std::vector<std::pair<counter_t, task>> todo_list_;
};

}  // namespace tools
