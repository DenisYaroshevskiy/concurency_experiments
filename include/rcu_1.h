// clang-format off
// Copyright 2025 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include <rcu_reading_subsystem.h>

#include <memory>

/*
 * Introduces generation counter.
 */
namespace v1 {

struct rcu_domain {
  struct tls;

  struct obj_base {};

  tools::rcu_reading_subsystem reading;

  void synchronize() { reading.synchronize(); }

  template <typename T, typename D>
  void retire(T* x, D d) {
    synchronize();
    d(x);
  }

  void barrier() { synchronize(); }
};

struct rcu_domain::tls : tools::rcu_reading_subsystem::tls {
  tls(rcu_domain* domain) :  tools::rcu_reading_subsystem::tls(domain->reading) {}

  template <typename T, typename D>
  void retire(T* x, D d) {
    subsystem().synchronize();
    d(x);
  }
};

}  // namespace v1
