// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

#include <utility>

namespace tools {

struct nomove {
  nomove() = default;
  nomove(const nomove&) = delete;
  nomove(nomove&&) = delete;
  nomove& operator=(const nomove&) = delete;
  nomove& operator=(nomove&&) = delete;
};

template <typename F>
struct [[nodiscard]] scope_exit {
  F f_;
  explicit scope_exit(F f) : f_(std::move(f)) {}
  ~scope_exit() { f_(); }
  scope_exit(scope_exit&&) = delete;
};

}  // namespace tools
