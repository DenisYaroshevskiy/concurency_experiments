/*
 * Copyright 2026 Denis Yaroshevskiy
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
#include "mailbox.h"

#include <relacy/relacy.hpp>
#include <relacy/test_suite.hpp>
#include <relacy/var.hpp>

#include <numeric>
#include <vector>

struct move_only {
  int val_ = -1;
  bool moved_from_ = false;

  explicit move_only(int v) : val_(v) {}
  move_only(const move_only&) = delete;
  move_only& operator=(const move_only&) = delete;
  move_only(move_only&& o) noexcept : val_(o.val_) { o.moved_from_ = true; }
  move_only& operator=(move_only&& o) noexcept {
    val_ = o.val_;
    o.moved_from_ = true;
    return *this;
  }
};

struct mailbox_simple : rl::test_suite<mailbox_simple, 2> {
  tools::mailbox<int> mb;

  void thread(unsigned idx) {
    if (idx == 0) {
      mb.push(42);
    } else {
      std::vector<int> out;
      while (!mb.try_get(out) || out.empty()) {
        rl::yield(1, $);
      }
      RL_ASSERT(out.size() == 1);
      RL_ASSERT(out[0] == 42);
    }
  }
};

struct mailbox_memory_order : rl::test_suite<mailbox_memory_order, 2> {
  tools::mailbox<int> mb;
  rl::var<int> shared_data{0};

  void thread(unsigned idx) {
    if (idx == 0) {
      shared_data($) = 42;
      mb.push(1);
    } else {
      std::vector<int> out;
      while (!mb.try_get(out) || out.empty()) {
        rl::yield(1, $);
      }
      RL_ASSERT(out[0] == 1);
      RL_ASSERT(shared_data($) == 42);
    }
  }
};

struct mailbox_multi : rl::test_suite<mailbox_multi, 2> {
  static constexpr int kCount = 3;
  tools::mailbox<move_only> mb;

  void thread(unsigned idx) {
    if (idx == 0) {
      for (int i = 1; i <= kCount; ++i) {
        mb.push(move_only{i});
      }
    } else {
      std::vector<move_only> collected;
      while (static_cast<int>(collected.size()) < kCount) {
        mb.try_get(collected);
      }
      RL_ASSERT(static_cast<int>(collected.size()) == kCount);
      for (int i = 0; i < kCount; ++i) {
        RL_ASSERT(!collected[i].moved_from_);
        RL_ASSERT(collected[i].val_ == i + 1);
      }
    }
  }
};

int main() {
  rl::simulate<mailbox_simple>();
  rl::simulate<mailbox_memory_order>();
  rl::simulate<mailbox_multi>();
}
