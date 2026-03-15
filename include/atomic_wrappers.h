// Copyright 2025 Denis Yaroshevskiy
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at https://www.boost.org/LICENSE_1_0.txt)


#pragma once

#ifdef RL_TEST
#include <relacy/atomic.hpp>
#include <relacy/atomic_fence.hpp>
#include <relacy/backoff.hpp>
#include <relacy/dyn_thread.hpp>
#include <relacy/stdlib/mutex.hpp>
#include <relacy/var.hpp>
#include <functional>
#include <memory>
#else
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#endif

namespace tools {

#ifdef RL_TEST
template <typename T>
using atomic = rl::atomic<T>;

using mutex = rl::mutex;

void this_thread_yield(rl::debug_info_param info DEFAULTED_DEBUG_INFO) {
  rl::yield(1, info);
}

inline constexpr auto memory_order_relaxed = rl::memory_order::relaxed;
inline constexpr auto memory_order_acquire = rl::memory_order::acquire;
inline constexpr auto memory_order_release = rl::memory_order::release;
inline constexpr auto memory_order_acq_rel = rl::memory_order::acq_rel;
inline constexpr auto memory_order_seq_cst = rl::memory_order::seq_cst;

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

template<typename T>
struct var_write_proxy {
  T value;
  rl::debug_info info;

  var_write_proxy(T v, rl::debug_info info = std::source_location::current())
      : value(std::move(v)), info(info) {}
};

template<typename T>
struct var {
  rl::var<T> impl_;

  var() = default;
  var(T val) : impl_(val) {}

  T operator()(rl::debug_info_param info DEFAULTED_DEBUG_INFO) const { return T(impl_(info)); }

  var& operator=(var_write_proxy<T> w) { impl_(w.info) = std::move(w.value); return *this; }
  var& operator=(var const& other)     { impl_(rl::debug_info{}) = other(); return *this; }

  friend bool operator< (var const& a, var const& b) { return a() <  b(); }
  friend bool operator<=(var const& a, var const& b) { return a() <= b(); }
  friend bool operator> (var const& a, var const& b) { return a() >  b(); }
  friend bool operator>=(var const& a, var const& b) { return a() >= b(); }
  friend bool operator==(var const& a, var const& b) { return a() == b(); }
  friend bool operator!=(var const& a, var const& b) { return a() != b(); }
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

template <typename... Ts>
using lock_guard = std::lock_guard<Ts...>;

void this_thread_yield() { std::this_thread::yield(); }

inline constexpr auto memory_order_relaxed = std::memory_order::relaxed;
inline constexpr auto memory_order_acquire = std::memory_order::acquire;
inline constexpr auto memory_order_release = std::memory_order::release;
inline constexpr auto memory_order_acq_rel = std::memory_order::acq_rel;
inline constexpr auto memory_order_seq_cst = std::memory_order::seq_cst;

template<typename T>
using var = T;

// TODO: production periodic_runner

#endif

}  // namespace tools
