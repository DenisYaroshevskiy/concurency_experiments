// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at
// https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <owner_stealer.h>

#include <functional>
#include <limits>
#include <optional>
#include <vector>

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

  std::size_t owner_reclaim(counter_t current_gen, task t);
  std::size_t clean_ready_tasks(counter_t current_gen);

  std::optional<counter_t> oldest_unreclaimed() const;

  void drain(std::vector<task>& here);

  bool try_steal_tasks(std::vector<task>& here);
  void steal_tasks_blocking(std::vector<task>& here);

 private:
  using task_entry = std::pair<counter_t, task>;

  void do_clean(std::vector<task_entry>& v, counter_t current_gen);

  static constexpr counter_t kNoTasks = std::numeric_limits<counter_t>::max();
  tools::atomic<counter_t> oldest_unreclaimed_{kNoTasks};
  tools::owner_stealer<std::vector<task_entry>> todo_list_;
};

inline void rcu_tls_reclaimer::do_clean(std::vector<task_entry>& v,
                                        counter_t current_gen) {
  auto it = v.begin();
  while (it != v.end() && it->first + 2 <= current_gen) {
    it->second();
    ++it;
  }
  v.erase(v.begin(), it);
  oldest_unreclaimed_.store(v.empty() ? kNoTasks : v.front().first,
                            tools::memory_order_relaxed);
}

inline std::size_t rcu_tls_reclaimer::owner_reclaim(counter_t current_gen,
                                                     task t) {
  std::size_t remaining = 0;
  todo_list_.owner_access([&](auto& v) {
    v.push_back({current_gen, std::move(t)});
    do_clean(v, current_gen);
    remaining = v.size();
  });
  return remaining;
}

inline std::size_t rcu_tls_reclaimer::clean_ready_tasks(
    counter_t current_gen) {
  std::size_t remaining = 0;
  todo_list_.owner_access([&](auto& v) {
    do_clean(v, current_gen);
    remaining = v.size();
  });
  return remaining;
}

inline std::optional<rcu_tls_reclaimer::counter_t>
rcu_tls_reclaimer::oldest_unreclaimed() const {
  auto v = oldest_unreclaimed_.load(tools::memory_order_relaxed);
  if (v == kNoTasks) return std::nullopt;
  return v;
}

inline void rcu_tls_reclaimer::drain(std::vector<task>& here) {
  todo_list_.owner_access([&](auto& v) {
    for (auto& [gen, t] : v) here.push_back(std::move(t));
    v.clear();
    oldest_unreclaimed_.store(kNoTasks, tools::memory_order_relaxed);
  });
}

inline bool rcu_tls_reclaimer::try_steal_tasks(std::vector<task>& here) {
  return todo_list_.try_stealer_access([&](auto& v) {
    for (auto& [gen, t] : v) here.push_back(std::move(t));
    v.clear();
  });
}

inline void rcu_tls_reclaimer::steal_tasks_blocking(std::vector<task>& here) {
  todo_list_.blocking_stealer_access([&](auto& v) {
    for (auto& [gen, t] : v) here.push_back(std::move(t));
    v.clear();
  });
}

}  // namespace tools
