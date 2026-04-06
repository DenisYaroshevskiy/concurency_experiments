// clang-format off
// Copyright 2026 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)
// clang-format on

#include <atomic_wrappers.h>
#include <owner_stealer.h>
#include <rcu_reading_subsystem.h>

#include <functional>
#include <memory>

/*
 * Background thread reclaimer.
 * retire() enqueues tasks into a per-thread mailbox.
 * A background thread periodically drains the mailboxes, synchronizes, then
 * executes the tasks.
 * barrier() drains all mailboxes, synchronizes, and executes.
 *
 * reader_tls and reclaim_tls are independent.
 * Most threads only need reader_tls.
 */
namespace v2 {

struct rcu_domain {
  using clean_up_task = std::move_only_function<void()>;

  using reader_tls = tools::rcu_reading_subsystem::tls;
  struct reclaim_tls;

  struct obj_base {};

  ~rcu_domain() { barrier(); }

  tools::rcu_reading_subsystem reading;

  void synchronize() { reading.synchronize(); }
  reclaim_tls make_reclaim_tls();

  std::vector<clean_up_task> collect_some_clean_up_tasks();
  std::vector<clean_up_task> collect_all_clean_up_tasks();

  void background_task() {
    auto tasks = collect_some_clean_up_tasks();
    synchronize();
    for (auto& t : tasks) t();
  }

  void barrier() {
    auto tasks = collect_all_clean_up_tasks();
    synchronize();
    for (auto& t : tasks) t();
  }

 private:
  struct reclaim_mailbox;

  tools::mutex reclaim_tls_vec_m;
  std::vector<tools::shared_ptr<reclaim_mailbox>> reclaim_mailbox_vec;
};

struct rcu_domain::reclaim_mailbox {
  tools::owner_stealer<std::vector<clean_up_task>> to_clean_up_;
};

struct rcu_domain::reclaim_tls {
  tools::shared_ptr<reclaim_mailbox> mailbox_;

  reclaim_tls() = default;
  reclaim_tls(const reclaim_tls&) = delete;
  reclaim_tls(reclaim_tls&&) = default;
  reclaim_tls& operator=(const reclaim_tls&) = delete;
  reclaim_tls& operator=(reclaim_tls&&) = default;
  ~reclaim_tls() = default;

  template <typename T, typename D = std::default_delete<T>>
  void retire(T* x, D d = {}) {
    mailbox_->to_clean_up_.owner_access([&](std::vector<clean_up_task>& v) {
      v.push_back(clean_up_task([x, d = std::move(d)]() mutable { d(x); }));
    });
  }
};

inline rcu_domain::reclaim_tls rcu_domain::make_reclaim_tls() {
  reclaim_tls r;
  r.mailbox_ = tools::make_shared<reclaim_mailbox>();
  tools::lock_guard _{reclaim_tls_vec_m};
  reclaim_mailbox_vec.push_back(r.mailbox_);
  return r;
}

// Collect from every slot once, skipping any that are currently locked.
inline std::vector<rcu_domain::clean_up_task>
rcu_domain::collect_some_clean_up_tasks() {
  std::vector<clean_up_task> todo;
  auto drain = [&](auto& os) {
    os.try_stealer_access([&](auto& v) {
      todo.insert(todo.end(), std::make_move_iterator(v.begin()),
                  std::make_move_iterator(v.end()));
      v.clear();
    });
  };
  tools::lock_guard _{reclaim_tls_vec_m};
  for (auto& x : reclaim_mailbox_vec) drain(x->to_clean_up_);
  return todo;
}

// Collect from every slot, retrying any that were temporarily locked.
// Also evicts dead reclaim_mailbox entries (use_count == 1 means owning
// reclaim_tls destroyed).
inline std::vector<rcu_domain::clean_up_task>
rcu_domain::collect_all_clean_up_tasks() {
  std::vector<clean_up_task> todo;
  auto move_tasks = [&](std::vector<clean_up_task>& v) {
    todo.insert(todo.end(), std::make_move_iterator(v.begin()),
                std::make_move_iterator(v.end()));
    v.clear();
  };

  tools::lock_guard _{reclaim_tls_vec_m};
  std::vector<reclaim_mailbox*> busy;
  std::erase_if(reclaim_mailbox_vec, [&](const auto& x) {
    bool dead = x.use_count() == 1;
    if (!x->to_clean_up_.try_stealer_access(move_tasks)) {
      busy.emplace_back(x.get());
      return false;
    }
    return dead;
  });
  for (auto& b : busy) {
    b->to_clean_up_.blocking_stealer_access(move_tasks);
  }

  return todo;
}

}  // namespace v2
