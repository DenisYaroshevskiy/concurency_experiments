// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>

struct wait_notify_test : rl::test_suite<wait_notify_test, 2> {
  rl::atomic<bool> busy{false};
  rl::atomic<bool> waiting{false};

  void critical_section() {
    busy.store(true, rl::memory_order_relaxed);
    busy.store(false, rl::memory_order_relaxed);

    rl::atomic_thread_fence(rl::memory_order_seq_cst);
    if (waiting.load(rl::memory_order_relaxed)) {
      waiting.store(false, rl::memory_order_relaxed);  // MARKED
      waiting.notify_one();
    }
  }

  void wait_to_exit() {
    if (!busy.load(rl::memory_order_relaxed)) {
      return;
    }
    waiting.store(true, rl::memory_order_relaxed);

    rl::atomic_thread_fence(rl::memory_order_seq_cst);

    if (!busy.load(rl::memory_order_relaxed)) {
      waiting.store(false, rl::memory_order_relaxed);
      return;
    }

    // We believe that it's possible that
    // * we are here because the busy variable was reset (ABA).
    // * we do not see the MARKED store(false)
    // * and we don't see the notify
    waiting.wait(true, rl::memory_order_relaxed);
  }

  void thread(unsigned idx) {
    if (idx == 0) {
      critical_section();
      critical_section();
    } else {
      wait_to_exit();
    }
  }
};


// The standard https://eel.is/c++draft/atomics.wait
struct just_wait_and_notify : rl::test_suite<just_wait_and_notify, 2> {
  rl::atomic<bool> waiting{true};

  void thread(unsigned idx) {
    if (idx == 0) {
      waiting.store(false, rl::memory_order_relaxed);
      waiting.notify_one();
    } else {
      waiting.wait(true, rl::memory_order_relaxed);
    }
  }
};

int main() {
  rl::test_params params;
  params.search_type = rl::fair_full_search_scheduler_type;
  return rl::simulate<wait_notify_test>(params) &&
                 rl::simulate<just_wait_and_notify>(params)
             ? 0
             : 1;
}
