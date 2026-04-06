// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <atomic_wrappers.h>
#include <rcu_tls_reclaimer.h>

#include <algorithm>
#include <functional>
#include <memory>

/*
 * Generation-based RCU with per-thread self-cleaning reclaimers.
 *
 * reader_tls and reclaim_tls are independent:
 *   - reader_tls is stored directly (raw pointer) in reader_tls_vec.
 *   - reclaim_tls is stored via shared_ptr in reclaim_tls_vec.
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
 *   3. clean_ready_tasks(new_gen) — flush the caller's own reclaimer.
 *   4. Execute any stolen stale tasks (they are safe post-synchronize).
 *
 * barrier() is the blocking drain path used on shutdown or explicit flush.
 *
 * There is no background thread; garbage_collect() is called by a user thread
 * that accumulates retire_threshold unreclaimed tasks, or explicitly.
 */

namespace v3 {

struct rcu_domain {
  using counter_t = std::uint64_t;
  using clean_up_task = std::move_only_function<void()>;

  struct config {
    std::size_t retire_threshold = 10;
    counter_t stale_gen_threshold = 10;
  };

  struct reader_tls;
  struct reclaim_tls;
  struct tls;

  rcu_domain() = default;
  explicit rcu_domain(config cfg) : config_(cfg) {}

  ~rcu_domain() { barrier(); }

  config config_;

  tools::mutex reader_tls_vec_m;
  std::vector<reader_tls*> reader_tls_vec;

  tools::mutex reclaim_tls_vec_m;
  std::vector<tools::shared_ptr<reclaim_tls>> reclaim_tls_vec;

  tools::atomic<counter_t> generation{1};
  tools::atomic<counter_t> last_stale_gen{0};

  void synchronize();
  void collect_stale_tasks(std::vector<clean_up_task>& out, counter_t current_gen);
  void garbage_collect();
  void barrier();
};

struct rcu_domain::reader_tls {
  tools::atomic<counter_t> counter{0};
  rcu_domain* domain_;

  explicit reader_tls(rcu_domain* domain) : domain_(domain) {
    tools::lock_guard _{domain_->reader_tls_vec_m};
    domain_->reader_tls_vec.push_back(this);
  }

  reader_tls(const reader_tls&) = delete;
  reader_tls(reader_tls&&) = delete;
  reader_tls& operator=(const reader_tls&) = delete;
  reader_tls& operator=(reader_tls&&) = delete;

  ~reader_tls() {
    tools::lock_guard _{domain_->reader_tls_vec_m};
    std::iter_swap(std::ranges::find(domain_->reader_tls_vec, this),
                   std::prev(domain_->reader_tls_vec.end()));
    domain_->reader_tls_vec.pop_back();
  }

  void enter() {
    counter_t g = domain_->generation.load(tools::memory_order_relaxed);
    counter.store(g, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }

  void exit() {
    tools::asymmetric_thread_fence_light();
    counter.store(0, tools::memory_order_relaxed);
  }
};

struct rcu_domain::reclaim_tls {
  tools::rcu_tls_reclaimer reclaimer_;

  template <typename T, typename D>
  std::size_t retire(counter_t gen, T* x, D d) {
    return reclaimer_.owner_reclaim(
        gen, clean_up_task([x, d = std::move(d)]() mutable { d(x); }));
  }

  bool try_steal_tasks(std::vector<clean_up_task>& tasks) {
    return reclaimer_.try_steal_tasks(tasks);
  }

  void steal_tasks_blocking(std::vector<clean_up_task>& tasks) {
    reclaimer_.steal_tasks_blocking(tasks);
  }

  std::optional<counter_t> oldest_unreclaimed() const {
    return reclaimer_.oldest_unreclaimed();
  }

  std::size_t clean_ready_tasks(counter_t gen) {
    return reclaimer_.clean_ready_tasks(gen);
  }
};

struct rcu_domain::tls {
  reader_tls reader_;
  tools::shared_ptr<reclaim_tls> reclaimer_;

  explicit tls(rcu_domain* domain) : reader_(domain) {
    reclaimer_ = tools::make_shared<reclaim_tls>();
    tools::lock_guard _{domain->reclaim_tls_vec_m};
    domain->reclaim_tls_vec.push_back(reclaimer_);
  }

  tls(const tls&) = delete;
  tls(tls&&) = delete;
  tls& operator=(const tls&) = delete;
  tls& operator=(tls&&) = delete;
  ~tls() = default;

  void enter() { reader_.enter(); }
  void exit() { reader_.exit(); }

  template <typename T, typename D = std::default_delete<T>>
  void retire(T* x, D d = {}) {
    rcu_domain* domain = reader_.domain_;
    counter_t gen = domain->generation.load(tools::memory_order_relaxed);
    auto cnt = reclaimer_->retire(gen, x, std::move(d));
    if (cnt >= domain->config_.retire_threshold) {
      domain->garbage_collect();
    }
  }
};

inline void rcu_domain::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{reader_tls_vec_m};

  counter_t desired = generation.load(tools::memory_order_relaxed) + 1;
  generation.store(desired, tools::memory_order_relaxed);

  while (true) {
    bool wait_more =
        std::ranges::any_of(reader_tls_vec, [desired](const reader_tls* x) {
          counter_t c = x->counter.load(tools::memory_order_relaxed);
          return 0 < c && c < desired;
        });

    if (!wait_more) break;
    tools::this_thread_yield();
  }
  tools::asymmetric_thread_fence_heavy();
}

inline void rcu_domain::collect_stale_tasks(std::vector<clean_up_task>& out,
                                            counter_t current_gen) {
  tools::lock_guard _{reclaim_tls_vec_m};
  for (auto& r : reclaim_tls_vec) {
    auto oldest = r->oldest_unreclaimed();
    if (oldest && *oldest + config_.stale_gen_threshold <= current_gen) {
      r->try_steal_tasks(out);
    }
  }
}

inline void rcu_domain::garbage_collect() {
  counter_t current_gen = generation.load(tools::memory_order_relaxed);

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
    tools::lock_guard _{reclaim_tls_vec_m};
    std::vector<reclaim_tls*> busy;
    std::erase_if(reclaim_tls_vec, [&](const auto& x) {
      bool dead = x.use_count() == 1;
      if (!x->try_steal_tasks(tasks)) {
        busy.push_back(x.get());
        return false;
      }
      return dead;
    });
    for (auto* b : busy) {
      b->steal_tasks_blocking(tasks);
    }
  }

  synchronize();

  for (auto& t : tasks) t();
}

}  // namespace v3
