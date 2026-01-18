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

namespace v0 {

struct rcu_domain {
  using counter_t = std::uint64_t;

  struct tls {
    tools::atomic<counter_t> counter;
    rcu_domain* domain_;

    tls(rcu_domain* domain);
    ~tls();

    void enter() { counter.fetch_add(1); }
    void exit() { counter.fetch_add(1); }
  };

  tools::mutex tls_vec_m;
  std::vector<tls*> tls_vec;

  void synchronize();
};

inline
rcu_domain::tls::tls(rcu_domain* domain) : domain_(domain) {
  tools::lock_guard _{domain_->tls_vec_m};
  domain_->tls_vec.push_back(this);
}

inline
rcu_domain::tls::~tls() {
  tools::lock_guard _{domain_->tls_vec_m};
  std::iter_swap(std::ranges::find(domain_->tls_vec, this),
                 std::prev(domain_->tls_vec.end()));
  domain_->tls_vec.pop_back();
}

inline
void rcu_domain::synchronize() {
  auto collect = [&] {
    tools::lock_guard _{tls_vec_m};

    std::vector<std::pair<const tls*, counter_t>> res;
    res.reserve(tls_vec.size());

    for (const auto* tls : tls_vec) {
      auto loaded = tls->counter.load();
      if (loaded) {
        res.emplace_back(tls, tls->counter);
      }
    }
    return res;
  };

  auto to_clear_vec = collect();
  auto to_clear_map =
      std::unordered_map(to_clear_vec.begin(), to_clear_vec.end());

  while (!to_clear_map.empty()) {
    tools::this_thread_yield();
    auto cur = collect();

    for (auto [tls, count] : cur) {
      auto found = to_clear_map.find(tls);
      if (found == to_clear_map.end()) {
        continue;
      }
      if (count != found->second) {
        to_clear_map.erase(found);
      }
    }
  }
}

}  // namespace v0
