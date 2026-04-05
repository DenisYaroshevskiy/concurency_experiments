// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)

#include "relacy/context.hpp"
#include "relacy/thread_local.hpp"
#define RL_TEST
#include "shared_ptr.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

// Single-thread: basic construction, copy, move, use_count.
struct shared_ptr_basic : rl::test_suite<shared_ptr_basic, 1> {
  void thread(unsigned) {
    auto sp = rl_extra::shared_ptr<int>(new int(42));
    RL_ASSERT(sp.use_count() == 1);

    auto sp2 = sp;
    RL_ASSERT(sp.use_count() == 2);
    RL_ASSERT(sp2.use_count() == 2);

    auto sp3 = std::move(sp2);
    RL_ASSERT(!sp2);
    RL_ASSERT(sp2.use_count() == 0);
    RL_ASSERT(sp3.use_count() == 2);
  }
};

// Single-thread: make_shared stores data inline; object survives until last ref drops.
struct shared_ptr_make_shared : rl::test_suite<shared_ptr_make_shared, 1> {
  void thread(unsigned) {
    auto sp = rl_extra::make_shared<rl::var<int>>();
    (*sp)($) = 42;
    RL_ASSERT(sp.use_count() == 1);
    RL_ASSERT((int)(*sp)($) == 42);
  }
};

// Two threads each release their own copy concurrently; the last release deletes the object.
struct shared_ptr_concurrent_release : rl::test_suite<shared_ptr_concurrent_release, 2> {
  rl_extra::shared_ptr<rl::var<int>> sp;
  rl_extra::shared_ptr<rl::var<int>> copies[2];

  void before() {
    sp = rl_extra::make_shared<rl::var<int>>();
    copies[0] = sp;
    copies[1] = sp;
    // count == 3
  }

  void thread(unsigned idx) {
    copies[idx] = {};
  }

  void after() {
    RL_ASSERT(sp.use_count() == 1);
  }
};

// Thread 0 releases its copy (2→1); thread 1 spins on use_count() until it sees 1.
// With rl::atomic backing use_count(), relacy can interleave the release and the load.
struct shared_ptr_release_visible : rl::test_suite<shared_ptr_release_visible, 2> {
  rl_extra::shared_ptr<rl::var<int>> sp1;
  rl_extra::shared_ptr<rl::var<int>> sp2;

  void before() {
    sp1 = rl_extra::make_shared<rl::var<int>>();
    sp2 = sp1;  // count == 2
    (*sp1)($) = 42;
  }

  void thread(unsigned idx) {
    if (idx == 0) {
      sp1 = {};  // release; count 2 → 1
    } else {
      while (sp2.use_count() != 1) rl::yield(1, $);
      RL_ASSERT((int)(*sp2)($) == 42);
    }
  }
};

int main() {
  rl::simulate<shared_ptr_basic>();
  rl::simulate<shared_ptr_make_shared>();
  rl::simulate<shared_ptr_concurrent_release>();
  rl::simulate<shared_ptr_release_visible>();
}
