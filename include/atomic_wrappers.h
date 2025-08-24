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

#ifdef RL_TEST
#include <relacy/atomic.hpp>
#include <relacy/atomic_fence.hpp>
#include <relacy/backoff.hpp>
#include <relacy/stdlib/mutex.hpp>
#include <relacy/var.hpp>
#else
#include <atomic>
#include <thread>
#endif

namespace tools {

#ifdef RL_TEST
template <typename T>
using atomic = rl::atomic<T>;

void this_thread_yield(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
  rl::yield(1, info);
}

inline constexpr auto memory_order_relaxed = rl::memory_order::mo_relaxed;
inline constexpr auto memory_order_acquire = rl::memory_order::mo_acquire;
inline constexpr auto memory_order_release = rl::memory_order::mo_release;
inline constexpr auto memory_order_acq_rel = rl::memory_order::mo_acq_rel;
inline constexpr auto memory_order_seq_cst = rl::memory_order::mo_seq_cst;

struct std_shared_mutex;
class shared_mutex : rl::generic_mutex<std_shared_mutex>, rl::nocopy<> {
  rl::debug_info_param constructor_info_;

  using tag = std_shared_mutex;

 public:
  /*implicit*/ shared_mutex(rl::debug_info_param info DEFAULTED_DEBUG_INFO)
      : constructor_info_(info) {
    generic_mutex<tag>::init(false, false, false, true, constructor_info_);
  }

  ~shared_mutex() { rl::generic_mutex<tag>::deinit(constructor_info_); }

  void lock(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    generic_mutex<tag>::lock_exclusive(info);
  }

  bool try_lock(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    return generic_mutex<tag>::try_lock_exclusive(info);
  }

  void unlock(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
    generic_mutex<tag>::unlock_exclusive(info);
  }
};

using rl::lock_guard;

void asymmetric_thread_fence_light(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
  rl::atomic_thread_fence(memory_order_seq_cst, info);
}

void asymmetric_thread_fence_heavy(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
  rl::atomic_thread_fence(memory_order_seq_cst, info);
}

#else

template <typename T>
using atomic = std::atomic<T>;

void this_thread_yield() { std::this_thread::yield(); }

inline constexpr auto memory_order_relaxed = rl::memory_order::relaxed;
inline constexpr auto memory_order_acquire = rl::memory_order::acquire;
inline constexpr auto memory_order_release = rl::memory_order::release;
inline constexpr auto memory_order_acq_rel = rl::memory_order::acq_rel;
inline constexpr auto memory_order_seq_cst = rl::memory_order::seq_cst;

#endif

}  // namespace tools
