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
#include <relacy/backoff.hpp>
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
