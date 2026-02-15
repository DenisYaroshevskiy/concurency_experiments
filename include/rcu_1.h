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

#include <algorithm>
#include <memory>

namespace v1 {

struct rcu_domain {
  using counter_t = std::uint64_t;

  struct tls;

  struct obj_base {};

  tools::mutex tls_vec_m;
  std::vector<tls*> tls_vec;
  rl::atomic<counter_t> generation = 1;

  void synchronize();
  template <typename T, typename D>
  void retire(T* x, D d) {
    synchronize();
    d(x);
  }
  void barrier() { synchronize(); }
};

struct rcu_domain::tls {
  tools::atomic<counter_t> counter;
  rcu_domain* domain_;

  tls(rcu_domain* domain);

  tls(const tls&) = delete;
  tls(tls&&) = delete;
  tls& operator=(const tls&&) = delete;
  tls& operator=(tls&&) = delete;
  ~tls();

  void enter() {
    counter_t loaded_generation = domain_->generation.load(tools::memory_order_relaxed);
    counter.store(loaded_generation, tools::memory_order_relaxed);
    tools::asymmetric_thread_fence_light();
  }
  void exit() {
    tools::asymmetric_thread_fence_light();
    counter.store(0, tools::memory_order_relaxed);
  }
};

inline rcu_domain::tls::tls(rcu_domain* domain) : domain_(domain) {
  tools::lock_guard _{domain_->tls_vec_m};
  domain_->tls_vec.push_back(this);
}

inline rcu_domain::tls::~tls() {
  tools::lock_guard _{domain_->tls_vec_m};
  std::iter_swap(std::ranges::find(domain_->tls_vec, this),
                 std::prev(domain_->tls_vec.end()));
  domain_->tls_vec.pop_back();
}

inline void rcu_domain::synchronize() {
  tools::asymmetric_thread_fence_heavy();

  tools::lock_guard _{tls_vec_m};

  counter_t desired = generation.load(tools::memory_order_relaxed) + 1;
  generation.store(desired, tools::memory_order_relaxed);

  while (true) {
    bool wait_more = std::ranges::any_of(tls_vec, [desired](const tls* x) {
      counter_t tls_counter = x->counter.load(tools::memory_order_relaxed);
      return 0 < tls_counter && tls_counter < desired;
    });
    tools::asymmetric_thread_fence_heavy();

    if (!wait_more) {
      break;
    }

    tools::this_thread_yield();
  }
}

}  // namespace v1
