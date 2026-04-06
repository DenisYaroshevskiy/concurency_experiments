// clang-format off
// Copyright 2025 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include <rcu_reading_subsystem.h>

/*
 * Introduces generation counter.
 */
namespace v1 {

struct rcu_domain {
  using reader_tls = tools::rcu_reading_subsystem::tls;
  struct reclaim_tls;

  struct obj_base {};

  tools::rcu_reading_subsystem reading;

  void synchronize() { reading.synchronize(); }
  reclaim_tls make_reclaim_tls();

  template <typename T, typename D>
  void retire(T* x, D d) {
    synchronize();
    d(x);
  }

  void barrier() { synchronize(); }
};

struct rcu_domain::reclaim_tls {
  rcu_domain* domain_ = nullptr;

  reclaim_tls() = default;
  reclaim_tls(const reclaim_tls&) = delete;
  reclaim_tls(reclaim_tls&&) = default;
  reclaim_tls& operator=(const reclaim_tls&) = delete;
  reclaim_tls& operator=(reclaim_tls&&) = default;
  ~reclaim_tls() = default;

  template <typename T, typename D = std::default_delete<T>>
  void retire(T* x, D d = {}) {
    domain_->synchronize();
    d(x);
  }
};

inline rcu_domain::reclaim_tls rcu_domain::make_reclaim_tls() {
  reclaim_tls r;
  r.domain_ = this;
  return r;
}

}  // namespace v1
