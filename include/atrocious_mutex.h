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

namespace tools {

struct atrocious_mutex {
 public:
   void lock() {
    while (!locked.compare_exchange_weak(false, true)) {
        this_thread_yield();
    }
   }

   void unlock() {
    locked = false;
   }

 private:
  tools::atomic<bool> locked;
};

} // namespace tools
