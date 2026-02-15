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

#ifndef RCU_RL_TESTS_H
#define RCU_RL_TESTS_H

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include <array>
#include <format>
#include <memory>

template <template <typename> class test, typename Domain,
          rl::thread_id_t static_thread_count_param>
struct rcu_test_base : rl::test_suite<test<Domain>, static_thread_count_param> {
  using tls_type = typename Domain::tls;

  Domain domain;

  tls_type make_tls(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "make_tls");
    return tls_type{&domain};
  }

  void synchronize(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "synchronize");
    domain.synchronize();
  }

  void barrier(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "barrier");
    domain.barrier();
  }

  template <typename T, typename D = std::default_delete<T>>
  void retire(T* ptr, D d= {}, rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    auto msg = std::format("retire({})", static_cast<const void*>(ptr));
    rl::ctx().exec_log_msg(info, msg.c_str());
    domain.retire(ptr, d);
  }
};

template <typename Domain>
struct rcu_test_no_mutation : rcu_test_base<rcu_test_no_mutation, Domain, 2> {
  rl::var<int> x{1};

  void thread(unsigned idx) {
    auto tls = this->make_tls();
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
      auto tls = this->make_tls();
      tls.enter();
      tls.exit();
    } else {
      this->synchronize();
    }
  }
};

template <typename Domain>
struct rcu_test_min_sync : rcu_test_base<rcu_test_min_sync, Domain, 2> {
  std::array<rl::var<int>, 2> stages{0, 0};
  rl::atomic<int> cur_stage = 0;

  void before() { stages[0]($) = 1; }

  void thread_read() {
    auto tls = this->make_tls();
    tls.enter();
    int stage = cur_stage.load(rl::memory_order_acquire);
    int val = stages[stage]($);
    tls.exit();
    RL_ASSERT(val == stage + 1);
  }

  void thread_write() {
    stages[1]($) = 2;
    cur_stage.store(1, rl::memory_order_release);
    this->synchronize();
    stages[0]($) = -1;
  }

  void thread(unsigned idx) {
    if (idx != 0) {
      thread_read();
    } else {
      thread_write();
    }
  }

  void after() { RL_ASSERT(cur_stage == 1); }
};

template <typename Domain>
struct rcu_test_proper_sync : rcu_test_base<rcu_test_proper_sync, Domain, 3> {
  std::size_t kNumStages = 4;

  std::array<rl::var<int>, 4> stages{0, 0, 0, 0};
  rl::atomic<int> cur_stage = 0;

  void before() { stages[0]($) = 1; }

  void thread_read() {
    auto tls = this->make_tls($);

    for (std::size_t i = 0; i != kNumStages; ++i) {
      tls.enter();
      int stage = cur_stage.load(rl::memory_order_acquire);
      int val = stages[stage]($);
      tls.exit();
      RL_ASSERT(val == stage + 1);
    }
  }

  void thread_write() {
    stages[1]($) = 2;
    cur_stage.store(1, rl::memory_order_release);
    this->synchronize();
    stages[0]($) = -1;

    stages[2]($) = 3;
    cur_stage.store(2, rl::memory_order_release);
    this->synchronize();
    stages[1]($) = -1;

    stages[3]($) = 4;
    cur_stage.store(3, rl::memory_order_release);
    this->synchronize();
    stages[2]($) = -1;
  }

  void thread(unsigned idx) {
    if (idx != 0) {
      thread_read();
    } else {
      thread_write();
    }
  }

  void after() { RL_ASSERT(cur_stage == 3); }
};

template <typename Domain>
struct rcu_test_sync_delete : rcu_test_base<rcu_test_sync_delete, Domain, 2> {
  rl::atomic<const rl::var<int>*> config = 0;

  void before() { config.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread_read() {
    auto tls = this->make_tls();
    tls.enter();
    const rl::var<int>* loaded = config.load(rl::memory_order_acquire);
    int val = (*loaded)($);
    tls.exit();
    RL_ASSERT(val == 1 || val == 2);
  }

  void thread_write() {
    auto* upd = new rl::var<int>(2);
    auto* old = config.exchange(upd, rl::memory_order_acq_rel);
    this->synchronize();
    delete old;
  }

  void thread(unsigned idx) {
    if (idx != 0) {
      thread_read();
    } else {
      thread_write();
    }
  }

  void after() {
    delete config.load(rl::memory_order_acquire);
  }
};


template <typename Domain>
struct rcu_test_retire : rcu_test_base<rcu_test_retire, Domain, 2> {
  rl::atomic<const rl::var<int>*> config = 0;

  void before() { config.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread_read() {
    auto tls = this->make_tls();
    tls.enter();
    const rl::var<int>* loaded = config.load(rl::memory_order_acquire);
    int val = (*loaded)($);
    tls.exit();
    RL_ASSERT(val == 1 || val == 2);
  }

  void thread_write() {
    auto* upd = new rl::var<int>(2);
    this->retire(config.exchange(upd, rl::memory_order_acq_rel));
  }

  void thread(unsigned idx) {
    if (idx != 0) {
      thread_read();
    } else {
      thread_write();
    }
  }

  void after() {
    this->barrier();
    delete config.load(rl::memory_order_acquire);
  }
};


template <typename Domain>
void simulate() {
  rl::simulate<rcu_test_no_mutation<Domain>>();
  rl::simulate<rcu_test_access_and_sync<Domain>>();
  rl::simulate<rcu_test_min_sync<Domain>>();
  rl::simulate<rcu_test_proper_sync<Domain>>();
  rl::simulate<rcu_test_sync_delete<Domain>>();
  rl::simulate<rcu_test_retire<Domain>>();
}

#endif // RCU_RL_TESTS_H
