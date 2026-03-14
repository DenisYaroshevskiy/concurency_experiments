// Copyright 2025 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)


#pragma once

#include "atomic_wrappers.h"

namespace tools {

struct atrocious_mutex {
 public:
  void lock() {
    bool val = false;
    while (!locked.compare_exchange_weak(val, true, tools::memory_order_acquire)) {
      val = false;
      this_thread_yield();
    }
  }

  void unlock() { locked.store(false, tools::memory_order_release); }

 private:
  tools::atomic<bool> locked;
};

}  // namespace tools
