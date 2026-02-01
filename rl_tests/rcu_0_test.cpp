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

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define RL_TEST
#include "rcu_0.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include <array>
#include <memory>

template <template <typename> class test, typename Domain,
          rl::thread_id_t static_thread_count_param>
struct rcu_test_base : rl::test_suite<test<Domain>, static_thread_count_param> {
  using tls_type = typename Domain::tls;

  Domain domain;
  rl::cxx_thread_local_var<tls_type> tls_storage;

  tls_type& tls(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    return tls_storage.get([&]() { return tls_type{&domain}; });
  }
};

template <typename Domain>
struct rcu_test_no_mutation : rcu_test_base<rcu_test_no_mutation, Domain, 2> {
  rl::var<int> x{1};

  void thread(unsigned idx) {
    auto& tls = this->tls();
    tls.enter();
    RL_ASSERT(1 == x($));
    tls.exit();
  }
};

template <typename Domain>
struct rcu_test_access_and_sync
    : rcu_test_base<rcu_test_no_mutation, Domain, 3> {
  void thread(unsigned idx) {
    if (idx != 0) {
      auto& tls = this->tls();
      tls.enter();
      tls.exit();
    } else {
      this->domain.synchronize();
    }
  }
};

template <typename Domain>
struct rcu_test_proper_sync
    : rcu_test_base<rcu_test_proper_sync, Domain, 3> {
  std::array<rl::var<int>, 4> stages {0, 0, 0, 0};
  rl::atomic<int> cur_stage = 0;

  void before() {
    stages[0]($) = 1;
  }

  void thread_read() {
    auto& tls = this->tls();
    tls.enter();
    int stage = cur_stage.load(rl::memory_order_acquire);
    int val = stages[stage]($);
    tls.exit();
    RL_ASSERT(val == stage + 1);
  }

  void thread_write() {
    stages[1]($) = 2;
    cur_stage.store(1, rl::memory_order_release);
    this->domain.synchronize();
    stages[0]($) = -1;

    stages[2]($) = 3;
    cur_stage.store(2, rl::memory_order_release);
    this->domain.synchronize();
    stages[1]($) = -1;

    stages[3]($) = 4;
    cur_stage.store(3, rl::memory_order_release);
    this->domain.synchronize();
    stages[2]($) = -1;
  }

  void thread(unsigned idx) {
    if (idx != 0) {
      for (int i = 0; i != 4; ++i) {
        thread_read();
      }
    } else {
      thread_write();
    }
  }

  void after() {
    RL_ASSERT(cur_stage == 3);
  }
};


template <typename Domain>
void simulate() {
  rl::simulate<rcu_test_no_mutation<Domain>>();
  rl::simulate<rcu_test_access_and_sync<Domain>>();
  rl::simulate<rcu_test_proper_sync<Domain>>();
}

int main() { simulate<v0::rcu_domain>(); }
