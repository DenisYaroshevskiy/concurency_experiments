// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)


#include <atomic_wrappers.h>

#include <array>
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
  std::array<std::vector<T>, 2> vecs_;
  tools::atomic<std::vector<T>*> senders_{&vecs_[0]};
};

template <typename T>
void mailbox<T>::push(T x) {
  auto* locked = senders_.exchange(nullptr, tools::memory_order_acquire);
  locked->push_back(std::move(x));

  /*
   * maybe free some memory here in the future in case
   * the vector has grown too big.
   */

  senders_.exchange(locked, tools::memory_order_release);
}

template <typename T>
bool mailbox<T>::try_get(std::vector<T>& out) {
  auto* cur = senders_.load(tools::memory_order_relaxed);
  // locked
  if (!cur) {
    return false;
  }

  // Trying to switch the publisher to a different buffer.
  auto* desired = cur == &vecs_[0] ? &vecs_[1] : &vecs_[0];
  if (!senders_.compare_exchange_strong(
          cur, desired,
          tools::memory_order_acq_rel,
          tools::memory_order_relaxed)) {
    return false;
  }

  // Append rather than replace so callers can collect from multiple mailboxes.
  out.insert(out.end(),
             std::make_move_iterator(cur->begin()),
             std::make_move_iterator(cur->end()));
  cur->clear();
  return true;
}

}  // namespace tools
