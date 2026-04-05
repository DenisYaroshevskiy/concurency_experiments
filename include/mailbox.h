// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <owner_stealer.h>

#include <vector>

namespace tools {
/*
 * Single producer single consumer.
 */
template <typename T>
class mailbox {
 public:
  mailbox() = default;
  mailbox(const mailbox&) = delete;
  mailbox(mailbox&&) = delete;
  mailbox& operator=(const mailbox&) = delete;
  mailbox& operator=(mailbox&&) = delete;

  // always succeedes
  void push(T x);

  // returns all avaliable mail (or nothing if there is a concurrent push)
  bool try_get(std::vector<T>& out);

 private:
  owner_stealer<std::vector<T>> impl_;
};

template <typename T>
void mailbox<T>::push(T x) {
  impl_.owner_access([&](std::vector<T>& v) { v.push_back(std::move(x)); });
}

template <typename T>
bool mailbox<T>::try_get(std::vector<T>& out) {
  return impl_.try_stealer_access([&](std::vector<T>& v) {
    out.insert(out.end(),
               std::make_move_iterator(v.begin()),
               std::make_move_iterator(v.end()));
    v.clear();
  });
}

}  // namespace tools
