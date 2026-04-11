// clang-format off
// Copyright 2025 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on


#ifndef RCU_RL_TESTS_H
#define RCU_RL_TESTS_H

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include <array>
#include <concepts>
#include <format>
#include <memory>

template <typename T>
concept has_background_task = requires(T& x) {
  { x.background_task() };
};

template <template <typename> class test, typename Domain,
          rl::thread_id_t static_thread_count_param>
struct rcu_test_base
    : rl::test_suite<test<Domain>,
                     static_thread_count_param + has_background_task<Domain>> {
  using reader_tls_type = typename Domain::reader_tls;
  using reclaim_tls_type = typename Domain::reclaim_tls;

  Domain domain;

  auto& self() { return *static_cast<test<Domain>*>(this); }

  reader_tls_type make_reader_tls(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "make_reader_tls");
    return reader_tls_type{domain.reading};
  }

  reclaim_tls_type make_reclaim_tls(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "make_reclaim_tls");
    return reclaim_tls_type{domain};
  }

  void thread(unsigned idx) {
    if constexpr (has_background_task<Domain>) {
      if (idx == static_thread_count_param) {
        for (int i = 0; i != 3; ++i) {
          domain.background_task();
        }
        return;
      }
    }
    self().thread_(idx);
  }

  void synchronize(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "synchronize");
    domain.synchronize();
  }

  void barrier(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    rl::ctx().exec_log_msg(info, "barrier");
    domain.barrier();
  }

  template <typename Tls, typename T, typename D = std::default_delete<T>>
  void retire(Tls& tls, T* ptr, D d = {},
              rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    auto msg = std::format("retire({})", static_cast<const void*>(ptr));
    rl::ctx().exec_log_msg(info, msg.c_str());
    tls.retire(ptr, d);
  }
};

template <typename Domain>
struct rcu_test_no_mutation : rcu_test_base<rcu_test_no_mutation, Domain, 2> {
  rl::var<int> x{1};

  void thread_(unsigned idx) {
    auto tls = this->make_reader_tls();
    tls.enter();
    RL_ASSERT(1 == x($));
    tls.exit();
  }
};

template <typename Domain>
struct rcu_test_access_and_sync
    : rcu_test_base<rcu_test_access_and_sync, Domain, 3> {
  void thread_(unsigned idx) {
    if (idx != 0) {
      auto tls = this->make_reader_tls();
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
    auto tls = this->make_reader_tls();
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

  void thread_(unsigned idx) {
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
    auto tls = this->make_reader_tls($);

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

  void thread_(unsigned idx) {
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
    auto tls = this->make_reader_tls();
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

  void thread_(unsigned idx) {
    if (idx != 0) {
      thread_read();
    } else {
      thread_write();
    }
  }

  void after() { delete config.load(rl::memory_order_acquire); }
};

template <typename Domain>
struct rcu_test_retire : rcu_test_base<rcu_test_retire, Domain, 2> {
  rl::atomic<const rl::var<int>*> config = 0;

  void before() { config.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread_read() {
    auto tls = this->make_reader_tls();
    tls.enter();
    const rl::var<int>* loaded = config.load(rl::memory_order_acquire);
    int val = (*loaded)($);
    tls.exit();
    RL_ASSERT(val == 1 || val == 2);
  }

  void thread_write() {
    auto tls = this->make_reclaim_tls();
    auto* upd = new rl::var<int>(2);
    this->retire(tls, config.exchange(upd, rl::memory_order_acq_rel));
  }

  void thread_(unsigned idx) {
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
struct rcu_test_barrier_concurrent
    : rcu_test_base<rcu_test_barrier_concurrent, Domain, 3> {
  rl::atomic<const rl::var<int>*> config = 0;

  void before() { config.store(new rl::var<int>(1), rl::memory_order_release); }

  void thread_write() {
    auto tls = this->make_reclaim_tls();
    auto* upd = new rl::var<int>(2);
    auto* old = config.exchange(upd, rl::memory_order_acq_rel);
    this->retire(tls, old);
  }

  void thread_read() {
    auto tls = this->make_reader_tls();
    tls.enter();
    const rl::var<int>* loaded = config.load(rl::memory_order_acquire);
    int val = (*loaded)($);
    tls.exit();
    RL_ASSERT(val == 1 || val == 2);
  }

  void thread_(unsigned idx) {
    if (idx == 0) thread_write();
    else if (idx == 1) this->barrier();
    else thread_read();
  }

  void after() {
    this->barrier();
    delete config.load(rl::memory_order_acquire);
  }
};

template <typename Domain>
struct rcu_test_retire_then_tls_death
    : rcu_test_base<rcu_test_retire_then_tls_death, Domain, 2> {
  rl::atomic<int> deleted{0};

  void thread_(unsigned idx) {
    if (idx == 0) {
      auto tls = this->make_reclaim_tls();
      this->retire(tls, &deleted, [this](rl::atomic<int>* p) {
        p->store(1, rl::memory_order_relaxed);
      });
    } else {
      this->barrier();
    }
  }

  void after() {
    this->barrier();
    RL_ASSERT(deleted.load(rl::memory_order_relaxed) == 1);
  }
};

// Thread 0 retires one task and stops. Thread 1 retires several tasks,
// potentially triggering reclamation that collects thread 0's stale task.
template <typename Domain>
struct rcu_test_cross_thread_reclaim
    : rcu_test_base<rcu_test_cross_thread_reclaim, Domain, 2> {
  rl::atomic<int> deleted{0};

  void thread_(unsigned idx) {
    auto tls = this->make_reclaim_tls();
    int n = (idx == 0) ? 1 : 4;
    for (int i = 0; i < n; ++i) {
      this->retire(tls, &deleted, [](rl::atomic<int>* p) {
        p->fetch_add(1, rl::memory_order_relaxed);
      });
    }
  }

  void after() {
    this->barrier();
    RL_ASSERT(deleted.load(rl::memory_order_relaxed) == 5);
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
  rl::simulate<rcu_test_barrier_concurrent<Domain>>();
  rl::simulate<rcu_test_retire_then_tls_death<Domain>>();
  rl::simulate<rcu_test_cross_thread_reclaim<Domain>>();
}

#endif  // RCU_RL_TESTS_H
