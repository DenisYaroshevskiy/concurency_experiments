// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#pragma once

namespace tools {

struct nomove {
  nomove() = default;
  nomove(const nomove&) = delete;
  nomove(nomove&&) = delete;
  nomove& operator=(const nomove&) = delete;
  nomove& operator=(nomove&&) = delete;
};

}  // namespace tools
