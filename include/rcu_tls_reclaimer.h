// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <atomic_wrappers.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <vector>

/*
 * Per-thread task reclaimer for RCU.
 *
 * On retire, instead of synchronizing immediately, each thread pushes tasks
 * into its own reclaimer. When the accumulation exceeds a threshold, the
 * caller triggers rcu_synchronize and then calls propagate() on all reclaimers.
 *
 * Each reclaimer has a 3-stage pipeline:
 *
 *   waiting_for_sync  — tasks added since the last synchronize began.
 *   syncing           — tasks that were in waiting_for_sync when the last
 *                       synchronize started; they become ready once it completes.
 *   ready             — tasks that have passed a full synchronize cycle and
 *                       may safely be executed by the owning thread.
 *
 * Stage advancement is done by calling propagate() after each synchronize.
 * It atomically moves: syncing → ready, waiting_for_sync → syncing.
 *
 * The owning thread executes ready tasks during the next push(), preserving
 * allocator locality. If the owning thread is inactive for too long,
 * propagate() returns an old time_proxy for the oldest ready task, allowing
 * another thread to call try_steal_ready() to take over execution.
 *
 * For barrier operations, get_all_tasks() collects every task regardless of
 * stage. It is non-blocking per slot: a null slot (temporarily held by a
 * concurrent push) is treated as empty, so callers should ensure no pushes
 * are in flight when a hard guarantee is required.
 */
struct rcu_tls_reclaimer {
  using task = std::move_only_function<void()>;
  using time_proxy = std::size_t;

  rcu_tls_reclaimer();
  ~rcu_tls_reclaimer();
  rcu_tls_reclaimer(const rcu_tls_reclaimer&) = delete;
  rcu_tls_reclaimer(rcu_tls_reclaimer&&) = delete;

  // Called by the owning thread on retire.
  // Adds the task to waiting_for_sync, recording tp as its submission time.
  // Drains and executes any tasks that are already ready, on the calling
  // thread (preserving allocator locality).
  // Returns the count of tasks now in waiting_for_sync; once this exceeds
  // the caller's threshold, the caller should trigger rcu_synchronize and
  // then call propagate() on all reclaimers.
  std::size_t push(task t, time_proxy tp);

  // Called by another thread after rcu_synchronize() completes.
  // Advances the pipeline: syncing → ready, waiting_for_sync → syncing.
  // Returns the time_proxy of the oldest task now in ready so the caller
  // can decide whether to steal if the owning thread seems inactive.
  time_proxy propagate();

  // Called by another thread when it thinks current thread took too long.
  // Non-blocking. Takes the ready tasks if the slot is immediately available;
  // returns an empty vector if the slot is currently locked.
  std::vector<task> try_steal_ready();

  // Called by a barrier thread.
  // Returns every task from all stages. Non-blocking per slot: a slot
  // temporarily held by a concurrent push is treated as empty.
  std::vector<task> get_all_tasks();

 private:
  struct stage_data {
    std::vector<task> tasks;
    time_proxy oldest_task = std::numeric_limits<time_proxy>::max();
  };

  tools::atomic<stage_data*> waiting_for_sync_;
  tools::atomic<stage_data*> syncing_;
  tools::atomic<stage_data*> ready_;
};

inline rcu_tls_reclaimer::rcu_tls_reclaimer() {
  // Only waiting_for_sync starts with an allocated batch; the other stages
  // start null (nothing to sync or execute yet).
  waiting_for_sync_.store(new stage_data{}, tools::memory_order_relaxed);
  syncing_.store(nullptr, tools::memory_order_relaxed);
  ready_.store(nullptr, tools::memory_order_relaxed);
}

inline rcu_tls_reclaimer::~rcu_tls_reclaimer() {
  delete waiting_for_sync_.load(tools::memory_order_relaxed);
  delete syncing_.load(tools::memory_order_relaxed);
  delete ready_.load(tools::memory_order_relaxed);
}

inline std::size_t rcu_tls_reclaimer::push(task t, time_proxy tp) {
  std::size_t res;

  {
    auto* cur = waiting_for_sync_.exchange(nullptr, tools::memory_order_acquire);

    cur->tasks.push_back(std::move(t));
    if (cur->tasks.size() == 1) cur->oldest_task = tp;
    res = cur->tasks.size();

    waiting_for_sync_.store(cur, tools::memory_order_release);
  }

  {
    auto* r = ready_.exchange(nullptr, tools::memory_order_acquire);
    if (!r) return res;

    for (auto& ready_task : r->tasks) ready_task();
    delete r;
  }

  return res;
}

inline rcu_tls_reclaimer::time_proxy rcu_tls_reclaimer::propagate() {
  stage_data* new_syncing = waiting_for_sync_.load(tools::memory_order_relaxed);
  if (new_syncing != nullptr) {
    auto* fresh = new stage_data{};
    if (!waiting_for_sync_.compare_exchange_strong(
            new_syncing, fresh,
            tools::memory_order_acq_rel, tools::memory_order_relaxed)) {
      delete fresh;
    }
  }
  auto* new_ready   = syncing_.exchange(new_syncing, tools::memory_order_acq_rel);

  if (!new_ready) {
    auto* cur_ready = ready_.exchange(nullptr, tools::memory_order_acquire);
    if (!cur_ready) return std::numeric_limits<time_proxy>::max();
    time_proxy res = cur_ready->oldest_task;
    ready_.store(cur_ready, tools::memory_order_release);
    return res;
  }

  auto* cur_ready = ready_.exchange(nullptr, tools::memory_order_acquire);
  if (!cur_ready) {
    cur_ready = new_ready;
  } else {
    cur_ready->tasks.insert(cur_ready->tasks.end(),
                            std::make_move_iterator(new_ready->tasks.begin()),
                            std::make_move_iterator(new_ready->tasks.end()));
    cur_ready->oldest_task = std::min(cur_ready->oldest_task, new_ready->oldest_task);
    delete new_ready;
  }

  time_proxy res = cur_ready->oldest_task;
  ready_.store(cur_ready, tools::memory_order_release);
  return res;
}

inline std::vector<rcu_tls_reclaimer::task> rcu_tls_reclaimer::try_steal_ready() {
  auto* r = ready_.exchange(nullptr, tools::memory_order_acquire);
  if (!r) return {};

  auto tasks = std::move(r->tasks);
  delete r;
  return tasks;
}

inline std::vector<rcu_tls_reclaimer::task> rcu_tls_reclaimer::get_all_tasks() {
  std::vector<task> all;

  auto drain_fresh = [&all](tools::atomic<stage_data*>& a) {
    stage_data* s = a.load(tools::memory_order_relaxed);
    if (!s) return;
    auto* fresh = new stage_data{};
    if (!a.compare_exchange_strong(s, fresh,
                                   tools::memory_order_acq_rel,
                                   tools::memory_order_relaxed)) {
      delete fresh;
      return;
    }
    for (auto& t : s->tasks) all.push_back(std::move(t));
    delete s;
  };
  auto drain = [&all](tools::atomic<stage_data*>& a) {
    auto* s = a.exchange(nullptr, tools::memory_order_acquire);
    if (!s) return;
    for (auto& t : s->tasks) all.push_back(std::move(t));
    delete s;
  };

  drain_fresh(waiting_for_sync_);
  drain(syncing_);
  drain(ready_);
  return all;
}
