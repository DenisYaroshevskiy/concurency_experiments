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

#pragma once

#include "atomic_wrappers.h"

#include <vector>

namespace tools {

// typically stored in the thread_local
class rcu_user_state;

class rcu_domain {
 public:
 private:
  friend rcu_user_state;

  tools::atomic<std::uint64_t> generation_;

  tools::shared_mutex m_;
  std::vector<rcu_user_state*> users_;
};

class rcu_user_marker_state {
 public:
  void lock(rcu_domain&);
  void unlock(rcu_domain&);

  std::uint64_t locked_generation_relaxed() const {
    return marker_.load(tools::memory_order_relaxed);
  }

 private:
  tools::atomic<std::uint64_t> marker_ = 0;
  // might make sense to pad though probably not
  std::uint32_t recursive_count_ = 0;
};

class rcu_user_state {
 public:
  explicit rcu_user_state(rcu_domain&);

  void lock() { marker_.lock(*domain_); }
  void unlock() { marker_.unlock(*domain_); }

  std::uint64_t locked_generation_relaxed() const {
    return marker_.locked_generation_relaxed();
  }

 private:
  rcu_domain* domain_;

  rcu_user_marker_state marker_;
};

}  // namespace tools
