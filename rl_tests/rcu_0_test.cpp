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

#define RL_TEST
#include "rcu_0.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include <memory>

template <typename Domain>
struct domain_test {
  Domain domain;
  using tls_type = typename Domain::tls;

  std::vector<std::unique_ptr<tls_type>> tls_vec;

  tls_type& tls_access(unsigned i) {
    if (tls_vec.size() < i) {
        tls_vec.resize(i * 2);
    }

    if (!tls_vec[i]) {
        tls_vec[i] = std::make_unique<tls_type>(&domain);
    }
    return *tls_vec[i];
  }

};

template <typename Domain>
struct rcu_test_var : rl::test_suite<rcu_test_var<Domain>, 3> {
  rl::var<int> var;

  domain_test<Domain> env;

  void before() {
    var($) = 0;
  }

  auto& tls(unsigned idx) {
    return env.tls_access(idx);
  }


  void thread(unsigned idx) {
    tls(idx).enter();
    tls(idx).exit();
  }
  void after() {}
};

int main() { rl::simulate<rcu_test_var<v0::rcu_domain>>(); }
