// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <rcu_reading_subsystem.h>
#include <rcu_tls_reclaimer.h>
#include <utils.h>

#include <functional>
#include <memory>

/*
 * Generation-based RCU with per-thread self-cleaning reclaimers.
 *
 * reader_tls and reclaim_tls are independent.
 * Most threads only need reader_tls.
 *
 * retire() stamps each task with the current generation.  A task at gen G is
 * safe to execute once the generation has advanced to G+2 (two full quiescent
 * cycles have elapsed).
 *
 * garbage_collect() is the active reclaim path:
 *   1. If the domain hasn't checked for stale tasks in stale_gen_threshold
 *      generations, steal tasks whose oldest_unreclaimed is sufficiently old
 *      from other threads' reclaimers (try_steal_tasks, non-blocking).
 *   2. synchronize() — advances the generation counter and waits for readers.
 *   3. Execute any stolen stale tasks (they are safe post-synchronize).
 *
 * barrier() is the blocking drain path used on shutdown or explicit flush.
 *
 * There is no background thread; garbage_collect() is called by a user thread
 * that accumulates retire_threshold unreclaimed tasks, or explicitly.
 *
 * Dead reclaim_tls entries (use_count == 1 in reclaim_mailbox_vec) are evicted
 * and drained by barrier().
 */

namespace v3 {

struct rcu_domain {
  using counter_t = tools::rcu_reading_subsystem::counter_t;
  using clean_up_task = std::move_only_function<void()>;

  struct config {
    std::size_t retire_threshold = 10;
    counter_t stale_gen_threshold = 10;
  };

  using reader_tls = tools::rcu_reading_subsystem::tls;
  struct reclaim_tls;

  rcu_domain() = default;
  explicit rcu_domain(config cfg) : config_(cfg) {}

  ~rcu_domain() { barrier(); }

  config config_;
  tools::rcu_reading_subsystem reading;

  void synchronize() { reading.synchronize(); }
  void garbage_collect();
  void barrier();

 private:
  struct reclaim_mailbox;

  tools::mutex reclaim_mailbox_vec_m;
  std::vector<tools::shared_ptr<reclaim_mailbox>> reclaim_mailbox_vec;
  tools::atomic<counter_t> last_stale_gen{0};

  void collect_stale_tasks(std::vector<clean_up_task>& out, counter_t current_gen);
};

struct rcu_domain::reclaim_mailbox {
  tools::rcu_tls_reclaimer reclaimer_;
};

struct rcu_domain::reclaim_tls : tools::nomove {
  tools::shared_ptr<reclaim_mailbox> mailbox_;
  rcu_domain* domain_;

  explicit reclaim_tls(rcu_domain& d) : domain_(&d) {
    mailbox_ = tools::make_shared<reclaim_mailbox>();
    tools::lock_guard _{d.reclaim_mailbox_vec_m};
    d.reclaim_mailbox_vec.push_back(mailbox_);
  }

  template <typename T, typename D = std::default_delete<T>>
  void retire(T* x, D d = {}) {
    counter_t gen = domain_->reading.generation();
    auto cnt = mailbox_->reclaimer_.owner_reclaim(
        gen, clean_up_task([x, d = std::move(d)]() mutable { d(x); }));
    if (cnt >= domain_->config_.retire_threshold) {
      domain_->garbage_collect();
    }
  }
};

inline void rcu_domain::collect_stale_tasks(std::vector<clean_up_task>& out,
                                            counter_t current_gen) {
  tools::lock_guard _{reclaim_mailbox_vec_m};
  for (auto& m : reclaim_mailbox_vec) {
    auto oldest = m->reclaimer_.oldest_unreclaimed();
    if (oldest && *oldest + config_.stale_gen_threshold <= current_gen) {
      m->reclaimer_.try_steal_tasks(out);
    }
  }
}

inline void rcu_domain::garbage_collect() {
  counter_t current_gen = reading.generation();

  std::vector<clean_up_task> stale_tasks;
  counter_t last_stale = last_stale_gen.load(tools::memory_order_relaxed);

  if (current_gen >= last_stale + config_.stale_gen_threshold) {
    if (last_stale_gen.compare_exchange_strong(
            last_stale, current_gen, tools::memory_order_relaxed,
            tools::memory_order_relaxed)) {
      collect_stale_tasks(stale_tasks, current_gen);
    }
  }

  synchronize();

  for (auto& t : stale_tasks) t();
}

inline void rcu_domain::barrier() {
  std::vector<clean_up_task> tasks;

  {
    tools::lock_guard _{reclaim_mailbox_vec_m};
    auto move_tasks = [&](std::vector<clean_up_task>& v) {
      tasks.insert(tasks.end(), std::make_move_iterator(v.begin()),
                   std::make_move_iterator(v.end()));
      v.clear();
    };

    std::vector<reclaim_mailbox*> busy;
    std::erase_if(reclaim_mailbox_vec, [&](const auto& m) {
      bool dead = m.use_count() == 1;
      if (!m->reclaimer_.try_steal_tasks(tasks)) {
        busy.emplace_back(m.get());
        return false;
      }
      return dead;
    });
    for (auto* b : busy) {
      b->reclaimer_.steal_tasks_blocking(tasks);
    }
  }

  synchronize();

  for (auto& t : tasks) t();
}

}  // namespace v3
