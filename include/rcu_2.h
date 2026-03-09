/*
 * Copyright 2025 Denis Yaroshevskiy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
 */
namespace v2 {

struct rcu_domain {
  using counter_t = std::uint64_t;
  using clean_up_task = std::move_only_function<void()>;

  struct tls_impl;
  struct tls;

  struct obj_base {};

  ~rcu_domain() {
    barrier();
  }

  tools::mutex tls_vec_m;
  std::vector<std::shared_ptr<tls_impl>> tls_vec;
  rl::atomic<counter_t> generation = 1;

  void synchronize();

  void background_task() {
    auto tasks = collect_some_clean_up_tasks();
    synchronize();
    for (auto& t : tasks) t();
  }

  std::vector<clean_up_task> collect_some_clean_up_tasks();
  std::vector<clean_up_task> collect_all_clean_up_tasks();

  // Synchronous fallback used by the test harness (no TLS available).
  template <typename T, typename D>
  void retire(T* x, D d) {
    synchronize();
    d(x);
  }

  void barrier() {
    auto tasks = collect_all_clean_up_tasks();
    synchronize();
    for (auto& t : tasks) t();
  }
};

struct rcu_domain::tls_impl {
  tools::atomic<counter_t> counter;
  rcu_domain* domain_;
  tools::mailbox<clean_up_task> to_clean_up_;

  tls_impl(rcu_domain* domain) : domain_(domain) {}
};

struct rcu_domain::tls {
  std::shared_ptr<tls_impl> impl_;

  tls(rcu_domain* domain);

  tls(const tls&) = delete;
  tls(tls&&) = delete;
  tls& operator=(const tls&) = delete;
  tls& operator=(tls&&) = delete;
  ~tls();

  void enter() {
    counter_t loaded_generation =
        impl_->domain_->generation.load(tools::memory_order_relaxed);
    impl_->counter.store(loaded_generation, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }
  void exit() {
    tools::asymmetric_thread_fence_light();
    impl_->counter.store(0, tools::memory_order_relaxed);
  }

  template <typename T, typename D>
  void retire(T* x, D d) {
    impl_->to_clean_up_.push(
        clean_up_task([x, d = std::move(d)]() mutable { d(x); }));
  }
};

inline rcu_domain::tls::tls(rcu_domain* domain) {
  impl_ = std::make_shared<tls_impl>(domain);
  tools::lock_guard _{domain->tls_vec_m};
  domain->tls_vec.push_back(impl_);
}

inline rcu_domain::tls::~tls() {}

inline void rcu_domain::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{tls_vec_m};

  counter_t desired = generation.load(tools::memory_order_relaxed) + 1;
  generation.store(desired, tools::memory_order_relaxed);

  while (true) {
    bool wait_more = std::ranges::any_of(
        tls_vec, [desired](const std::shared_ptr<tls_impl>& x) {
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
  tools::lock_guard _{tls_vec_m};
  for (auto& x : tls_vec) {
    x->to_clean_up_.try_get(todo);
  }
  return todo;
}

// Collect from every mailbox, retrying any that were temporarily locked.
inline std::vector<rcu_domain::clean_up_task>
rcu_domain::collect_all_clean_up_tasks() {
  std::vector<clean_up_task> todo;
  std::vector<std::shared_ptr<tls_impl>> busy;

  tools::lock_guard _{tls_vec_m};

  for (auto& x : tls_vec) {
    if (!x->to_clean_up_.try_get(todo)) {
      busy.push_back(x);
    }
  }

  while (!busy.empty()) {
    tools::this_thread_yield();
    std::erase_if(busy,
                  [&todo](auto& x) { return x->to_clean_up_.try_get(todo); });
  }

  return todo;
}

}  // namespace v2
