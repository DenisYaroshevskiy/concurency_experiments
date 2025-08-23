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

#ifdef RELACY_TEST
#include <relacy/atomic.hpp>
#include <relacy/var.hpp>
#include <relacy/backoff.hpp>
#else
#include <atomic>
#include <thread>
#endif

namespace tools {


#ifdef RELACY_TEST
template <typename T>
using atomic = rl::atomic<T>;

void this_thread_yield(debug_info_param info DEFAULTED_DEBUG_INFO) {
 rl::yield(1, info);
}

#else

template <typename T>
using atomic = std::atomic<T>;

void this_thread_yield() {
  std::this_thread::yield();
}

#endif

} // namespace tools
