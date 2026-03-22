// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)


#include <atomic_wrappers.h>
#include <mailbox.h>

#include <algorithm>
#include <functional>
#include <memory>

/*
 * Background thread reclaimer.
 * retire() enqueues tasks into a per-thread mailbox.
 * A background thread periodically drains the mailboxes, synchronizes, then
 * executes the tasks.
 * barrier() drains all mailboxes, synchronizes, and executes.
 *
 * reader_tls and reclaim_tls are independent:
 *   - reader_tls is stored directly (raw pointer) in reader_tls_vec.
 *   - reclaim_tls is stored via shared_ptr in reclaim_tls_vec.
 */
namespace v2 {

struct rcu_domain {
  using counter_t = std::uint64_t;
  using clean_up_task = std::move_only_function<void()>;

  struct reader_tls;
  struct reclaim_tls;
  struct tls;

  struct obj_base {};

  ~rcu_domain() {
    barrier();
  }

  tools::mutex reader_tls_vec_m;
  std::vector<reader_tls*> reader_tls_vec;

  tools::mutex reclaim_tls_vec_m;
  std::vector<std::shared_ptr<reclaim_tls>> reclaim_tls_vec;

  rl::atomic<counter_t> generation = 1;

  void synchronize();

  std::vector<clean_up_task> collect_some_clean_up_tasks();
  std::vector<clean_up_task> collect_all_clean_up_tasks();

  void background_task() {
    auto tasks = collect_some_clean_up_tasks();
    synchronize();
    for (auto& t : tasks) t();
  }

  void barrier() {
    auto tasks = collect_all_clean_up_tasks();
    synchronize();
    for (auto& t : tasks) t();
  }
};

struct rcu_domain::reader_tls {
  tools::atomic<counter_t> counter;
  rcu_domain* domain_;

  reader_tls(rcu_domain* domain) : domain_(domain) {
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
    counter_t loaded_generation =
        domain_->generation.load(tools::memory_order_relaxed);
    counter.store(loaded_generation, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }

  void exit() {
    tools::asymmetric_thread_fence_light();
    counter.store(0, tools::memory_order_relaxed);
  }
};

struct rcu_domain::reclaim_tls {
  tools::mailbox<clean_up_task> to_clean_up_;

  template <typename T, typename D>
  void retire(T* x, D d) {
    to_clean_up_.push(
        clean_up_task([x, d = std::move(d)]() mutable { d(x); }));
  }
};

struct rcu_domain::tls {
  reader_tls reader_;
  std::shared_ptr<reclaim_tls> reclaimer_;

  tls(rcu_domain* domain) : reader_(domain) {
    reclaimer_ = std::make_shared<reclaim_tls>();
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

  template <typename T, typename D>
  void retire(T* x, D d) { reclaimer_->retire(x, d); }
};

inline void rcu_domain::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{reader_tls_vec_m};

  counter_t desired = generation.load(tools::memory_order_relaxed) + 1;
  generation.store(desired, tools::memory_order_relaxed);

  while (true) {
    bool wait_more = std::ranges::any_of(
        reader_tls_vec, [desired](const reader_tls* x) {
          counter_t tls_counter = x->counter.load(tools::memory_order_relaxed);
          return 0 < tls_counter && tls_counter < desired;
        });

    if (!wait_more) {
      break;
    }

    tools::this_thread_yield();
  }
  tools::asymmetric_thread_fence_heavy();
}

// Collect from every mailbox once, skipping any that are currently locked.
inline std::vector<rcu_domain::clean_up_task>
rcu_domain::collect_some_clean_up_tasks() {
  std::vector<clean_up_task> todo;
  tools::lock_guard _{reclaim_tls_vec_m};
  for (auto& x : reclaim_tls_vec) x->to_clean_up_.try_get(todo);
  return todo;
}

// Collect from every mailbox, retrying any that were temporarily locked.
// Also evicts dead reclaim_tls entries (use_count == 1 means owning tls destroyed).
inline std::vector<rcu_domain::clean_up_task>
rcu_domain::collect_all_clean_up_tasks() {
  std::vector<clean_up_task> todo;
  tools::lock_guard _{reclaim_tls_vec_m};
  std::vector<std::weak_ptr<reclaim_tls>> busy;
  std::erase_if(reclaim_tls_vec, [&](const auto& x) {
    bool drained = x->to_clean_up_.try_get(todo);
    if (!drained) busy.emplace_back(x);
    return drained && x.use_count() == 1;
  });
  while (!busy.empty()) {
    tools::this_thread_yield();
    std::erase_if(busy, [&todo](const auto& w) {
      return w.lock()->to_clean_up_.try_get(todo);
    });
  }
  return todo;
}

}  // namespace v2
