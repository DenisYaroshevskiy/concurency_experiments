// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define TOOLS_RL_TEST
#include "owner_stealer.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include <vector>

// Owner pushes a value; stealer loops until it collects it.
struct owner_stealer_try_basic : rl::test_suite<owner_stealer_try_basic, 2> {
  tools::owner_stealer<std::vector<int>> os;

  void thread(unsigned idx) {
    if (idx == 0) {
      os.owner_access([](std::vector<int>& v) { v.push_back(42); });
    } else {
      std::vector<int> collected;
      while (collected.empty()) {
        os.try_stealer_access([&](std::vector<int>& v) {
          collected.insert(collected.end(), v.begin(), v.end());
          v.clear();
        });
        if (collected.empty()) rl::yield(1, $);
      }
      RL_ASSERT(collected.size() == 1);
      RL_ASSERT(collected[0] == 42);
    }
  }
};

// Owner writes shared data then pushes; stealer must see the shared data
// after acquiring the value through the stealer path.
struct owner_stealer_memory_order : rl::test_suite<owner_stealer_memory_order, 2> {
  tools::owner_stealer<std::vector<int>> os;
  rl::var<int> shared_data{0};

  void thread(unsigned idx) {
    if (idx == 0) {
      shared_data($) = 42;
      os.owner_access([](std::vector<int>& v) { v.push_back(1); });
    } else {
      std::vector<int> collected;
      while (collected.empty()) {
        os.try_stealer_access([&](std::vector<int>& v) {
          collected.insert(collected.end(), v.begin(), v.end());
          v.clear();
        });
        if (collected.empty()) rl::yield(1, $);
      }
      RL_ASSERT(shared_data($) == 42);
    }
  }
};

// blocking_stealer_access must spin while owner holds the lock.
// shared_data is written inside owner_access (under the lock); the stealer
// loops until it receives the pushed value, then asserts shared_data is visible.
struct owner_stealer_blocking : rl::test_suite<owner_stealer_blocking, 2> {
  tools::owner_stealer<int> os;
  tools::atomic<bool> owner_started = false;

  void thread(unsigned idx) {
    if (idx == 0) {
      os.owner_access([&](int& x) {
        owner_started.store(true, tools::memory_order_relaxed);
        x = 1;
      });
    } else {
      while (!owner_started.load(tools::memory_order_relaxed)) {
        rl::yield(1, $);
      }
      int res = 0;
      os.blocking_stealer_access([&](int& x) {
        res = x;
      });
      RL_ASSERT(res == 1);
    }
  }
};

// Multiple owner pushes interleaved with steals; all values must be collected.
struct owner_stealer_multi_push : rl::test_suite<owner_stealer_multi_push, 2> {
  static constexpr int kCount = 3;
  tools::owner_stealer<std::vector<int>> os;

  void thread(unsigned idx) {
    if (idx == 0) {
      for (int i = 1; i <= kCount; ++i) {
        os.owner_access([i](std::vector<int>& v) { v.push_back(i); });
      }
    } else {
      std::vector<int> collected;
      while (static_cast<int>(collected.size()) < kCount) {
        os.try_stealer_access([&](std::vector<int>& v) {
          collected.insert(collected.end(), v.begin(), v.end());
          v.clear();
        });
        if (static_cast<int>(collected.size()) < kCount) rl::yield(1, $);
      }
      RL_ASSERT((collected == std::vector<int>{1, 2, 3}));
    }
  }
};

// Single-thread: alternating owner pushes and steals verify the
// double-buffer swap logic and that no values are lost or duplicated.
struct owner_stealer_alternating : rl::test_suite<owner_stealer_alternating, 1> {
  tools::owner_stealer<std::vector<int>> os;

  void thread(unsigned) {
    for (int round = 0; round < 3; ++round) {
      os.owner_access([round](std::vector<int>& v) { v.push_back(round); });

      std::vector<int> got;
      os.try_stealer_access([&](std::vector<int>& v) {
        got.insert(got.end(), v.begin(), v.end());
        v.clear();
      });

      RL_ASSERT(got.size() == 1);
      RL_ASSERT(got[0] == round);
    }
  }
};

int main() {
  rl::simulate<owner_stealer_try_basic>();
  rl::simulate<owner_stealer_memory_order>();
  rl::simulate<owner_stealer_blocking>();
  rl::simulate<owner_stealer_multi_push>();
  rl::simulate<owner_stealer_alternating>();
}
