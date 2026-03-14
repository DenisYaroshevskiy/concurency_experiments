// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)

#include <atomic_wrappers.h>

/*
 * The tls task reclaimer.
 * On retire, intead of doing synchronize immediately,
 * Threads `push` tasks into their collectors.
 * When the current collector is at the limit of their capacity,
 * threads trigger `rcu_synchronize` and `propaagate` all collector.
 *
 * Propagation.
 * collectors have tasks in 3 stages:
 *  waiting_for_sync, syncing, ready.
 *
 *  waiting_for_sync - tasks that might have just been added, so they can't really
 *                     be executed yet.
 *  syncing - tasks that we knew about during our last syncronize. It might be that
 *            they have been added after the last synch started, but we saw them.
 *            So - after the current sync is done, they are definetly ready.
 *  ready - tasks that definetly passed their clean up time.
 *
 * When pushing a new task, it will execute all of the `ready_for_clean_up` tasks.
 *
 * `push` accepts an std::size_t to track time of the reclamation - (a generation counter).
 * `propagate` returns that counter for the oldest ready task.
 *  if that ready task is too old, the thread that currently does the reclamations, might
 *  decide to steal the ready tasks, assuming that the current thread is not coming back
 *  in a foreseable future.
 *
 * For barrier operation we also have a `get_all_tasks` that guranteed to take all tasks
 * scheduled, regardless of their readiness.
*/
struct rcu_tls_reclaimer {
 using task = std::move_only_task<void()>;
 using time_proxy = std::size_t;

 // called by the thread who's reclaimer this is.
 // returns current number of tasks `waiting_for_sync`.
 // executes all tasks that are ready for it.
 std::size_t push(task, time_proxy);

 // called by a different thread, when it's doing the sync to advance
 // `waiting_for_sync` -> `syncing` -> `ready`
 time_proxy propagate();

 // called by a different thread, that assumed this thread was not going to
 // do it's own clean up any time soon.
 std::vector<task> try_steal_ready();

 // called by a thread doing barrier, to get everything, regardless
 // of it's stage.
 std::vector<task> get_all_tasks();

 private:
   struct stage_data {
    std::vector<task> tasks;
    time_proxy oldest_task;
   };

   tools::atomic<stage_data*> waiting_for_sync_;
   tools::atomic<stage_data*> syncing_;
   tools::atomic<stage_data*> ready_;
};
