//
// impl/co_spawn.hpp
// ~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_IMPL_CO_SPAWN_HPP
#define BOOST_ASIO_IMPL_CO_SPAWN_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>
#include <boost/asio/associated_cancellation_slot.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/execution/outstanding_work.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/prefer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace detail {

template <typename Executor, typename = void>
class co_spawn_work_guard
{
public:
  typedef typename decay<
      typename prefer_result<Executor,
        execution::outstanding_work_t::tracked_t
      >::type
    >::type executor_type;

  co_spawn_work_guard(const Executor& ex)
    : executor_(boost::asio::prefer(ex, execution::outstanding_work.tracked))
  {
  }

  executor_type get_executor() const BOOST_ASIO_NOEXCEPT
  {
    return executor_;
  }

private:
  executor_type executor_;
};

#if !defined(BOOST_ASIO_NO_TS_EXECUTORS)

template <typename Executor>
struct co_spawn_work_guard<Executor,
    typename enable_if<
      !execution::is_executor<Executor>::value
    >::type> : executor_work_guard<Executor>
{
  co_spawn_work_guard(const Executor& ex)
    : executor_work_guard<Executor>(ex)
  {
  }
};

#endif // !defined(BOOST_ASIO_NO_TS_EXECUTORS)

template <typename Executor>
inline co_spawn_work_guard<Executor>
make_co_spawn_work_guard(const Executor& ex)
{
  return co_spawn_work_guard<Executor>(ex);
}

template <typename T, typename Executor, typename F, typename Handler>
awaitable<awaitable_thread_entry_point, Executor> co_spawn_entry_point(
    awaitable<T, Executor>*, Executor ex, F f, Handler handler)
{
  auto spawn_work = make_co_spawn_work_guard(ex);
  auto handler_work = make_co_spawn_work_guard(
      boost::asio::get_associated_executor(handler, ex));

  (void) co_await (dispatch)(
      use_awaitable_t<Executor>{__FILE__, __LINE__, "co_spawn_entry_point"});

  (co_await awaitable_thread_has_context_switched{}) = false;
  std::exception_ptr e = nullptr;
  bool done = false;
  try
  {
    T t = co_await f();

    done = true;

    bool switched = (co_await awaitable_thread_has_context_switched{});
    if (!switched)
    {
      (void) co_await (post)(
          use_awaitable_t<Executor>{__FILE__,
            __LINE__, "co_spawn_entry_point"});
    }

    (dispatch)(handler_work.get_executor(),
        [handler = std::move(handler), t = std::move(t)]() mutable
        {
          std::move(handler)(std::exception_ptr(), std::move(t));
        });

    co_return;
  }
  catch (...)
  {
    if (done)
      throw;

    e = std::current_exception();
  }

  bool switched = (co_await awaitable_thread_has_context_switched{});
  if (!switched)
  {
    (void) co_await (post)(
        use_awaitable_t<Executor>{__FILE__, __LINE__, "co_spawn_entry_point"});
  }

  (dispatch)(handler_work.get_executor(),
      [handler = std::move(handler), e]() mutable
      {
        std::move(handler)(e, T());
      });
}

template <typename Executor, typename F, typename Handler>
awaitable<awaitable_thread_entry_point, Executor> co_spawn_entry_point(
    awaitable<void, Executor>*, Executor ex, F f, Handler handler)
{
  auto spawn_work = make_co_spawn_work_guard(ex);
  auto handler_work = make_co_spawn_work_guard(
      boost::asio::get_associated_executor(handler, ex));

  (void) co_await (dispatch)(
      use_awaitable_t<Executor>{__FILE__, __LINE__, "co_spawn_entry_point"});

  (co_await awaitable_thread_has_context_switched{}) = false;
  std::exception_ptr e = nullptr;
  try
  {
    co_await f();
  }
  catch (...)
  {
    e = std::current_exception();
  }

  bool switched = (co_await awaitable_thread_has_context_switched{});
  if (!switched)
  {
    (void) co_await (post)(
        use_awaitable_t<Executor>{__FILE__, __LINE__, "co_spawn_entry_point"});
  }

  (dispatch)(handler_work.get_executor(),
      [handler = std::move(handler), e]() mutable
      {
        std::move(handler)(e);
      });
}

template <typename T, typename Executor>
class awaitable_as_function
{
public:
  explicit awaitable_as_function(awaitable<T, Executor>&& a)
    : awaitable_(std::move(a))
  {
  }

  awaitable<T, Executor> operator()()
  {
    return std::move(awaitable_);
  }

private:
  awaitable<T, Executor> awaitable_;
};

template <typename Handler, typename Executor, typename = void>
class co_spawn_cancellation_handler
{
public:
  co_spawn_cancellation_handler(const Handler&, const Executor& ex)
    : ex_(ex)
  {
  }

  cancellation_slot slot()
  {
    return signal_.slot();
  }

  void operator()(cancellation_type_t type)
  {
    cancellation_signal* sig = &signal_;
    boost::asio::dispatch(ex_, [sig, type]{ sig->emit(type); });
  }

private:
  cancellation_signal signal_;
  Executor ex_;
};


template <typename Handler, typename Executor>
class co_spawn_cancellation_handler<Handler, Executor,
    typename enable_if<
      is_same<
        typename associated_executor<Handler,
          Executor>::asio_associated_executor_is_unspecialised,
        void
      >::value
    >::type>
{
public:
  co_spawn_cancellation_handler(const Handler&, const Executor&)
  {
  }

  cancellation_slot slot()
  {
    return signal_.slot();
  }

  void operator()(cancellation_type_t type)
  {
    signal_.emit(type);
  }

private:
  cancellation_signal signal_;
};

template <typename Executor>
class initiate_co_spawn
{
public:
  typedef Executor executor_type;

  template <typename OtherExecutor>
  explicit initiate_co_spawn(const OtherExecutor& ex)
    : ex_(ex)
  {
  }

  executor_type get_executor() const BOOST_ASIO_NOEXCEPT
  {
    return ex_;
  }

  template <typename Handler, typename F>
  void operator()(Handler&& handler, F&& f) const
  {
    typedef typename result_of<F()>::type awaitable_type;
    typedef typename decay<Handler>::type handler_type;
    typedef co_spawn_cancellation_handler<
      handler_type, Executor> cancel_handler_type;

    auto slot = boost::asio::get_associated_cancellation_slot(handler);
    cancel_handler_type* cancel_handler = slot.is_connected()
      ? &slot.template emplace<cancel_handler_type>(handler, ex_)
      : nullptr;

    cancellation_slot proxy_slot(
        cancel_handler
          ? cancel_handler->slot()
          : cancellation_slot());

    cancellation_state cancel_state(proxy_slot);

    auto a = (co_spawn_entry_point)(static_cast<awaitable_type*>(nullptr),
        ex_, std::forward<F>(f), std::forward<Handler>(handler));
    awaitable_handler<executor_type, void>(std::move(a),
        ex_, proxy_slot, cancel_state).launch();
  }

private:
  Executor ex_;
};

} // namespace detail

template <typename Executor, typename T, typename AwaitableExecutor,
    BOOST_ASIO_COMPLETION_TOKEN_FOR(
      void(std::exception_ptr, T)) CompletionToken>
inline BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(
    CompletionToken, void(std::exception_ptr, T))
co_spawn(const Executor& ex,
    awaitable<T, AwaitableExecutor> a, CompletionToken&& token,
    typename constraint<
      (is_executor<Executor>::value || execution::is_executor<Executor>::value)
        && is_convertible<Executor, AwaitableExecutor>::value
    >::type)
{
  return async_initiate<CompletionToken, void(std::exception_ptr, T)>(
      detail::initiate_co_spawn<AwaitableExecutor>(AwaitableExecutor(ex)),
      token, detail::awaitable_as_function<T, AwaitableExecutor>(std::move(a)));
}

template <typename Executor, typename AwaitableExecutor,
    BOOST_ASIO_COMPLETION_TOKEN_FOR(
      void(std::exception_ptr)) CompletionToken>
inline BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(
    CompletionToken, void(std::exception_ptr))
co_spawn(const Executor& ex,
    awaitable<void, AwaitableExecutor> a, CompletionToken&& token,
    typename constraint<
      (is_executor<Executor>::value || execution::is_executor<Executor>::value)
        && is_convertible<Executor, AwaitableExecutor>::value
    >::type)
{
  return async_initiate<CompletionToken, void(std::exception_ptr)>(
      detail::initiate_co_spawn<AwaitableExecutor>(AwaitableExecutor(ex)),
      token, detail::awaitable_as_function<
        void, AwaitableExecutor>(std::move(a)));
}

template <typename ExecutionContext, typename T, typename AwaitableExecutor,
    BOOST_ASIO_COMPLETION_TOKEN_FOR(
      void(std::exception_ptr, T)) CompletionToken>
inline BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(
    CompletionToken, void(std::exception_ptr, T))
co_spawn(ExecutionContext& ctx,
    awaitable<T, AwaitableExecutor> a, CompletionToken&& token,
    typename constraint<
      is_convertible<ExecutionContext&, execution_context&>::value
        && is_convertible<typename ExecutionContext::executor_type,
          AwaitableExecutor>::value
    >::type)
{
  return (co_spawn)(ctx.get_executor(), std::move(a),
      std::forward<CompletionToken>(token));
}

template <typename ExecutionContext, typename AwaitableExecutor,
    BOOST_ASIO_COMPLETION_TOKEN_FOR(
      void(std::exception_ptr)) CompletionToken>
inline BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(
    CompletionToken, void(std::exception_ptr))
co_spawn(ExecutionContext& ctx,
    awaitable<void, AwaitableExecutor> a, CompletionToken&& token,
    typename constraint<
      is_convertible<ExecutionContext&, execution_context&>::value
        && is_convertible<typename ExecutionContext::executor_type,
          AwaitableExecutor>::value
    >::type)
{
  return (co_spawn)(ctx.get_executor(), std::move(a),
      std::forward<CompletionToken>(token));
}

template <typename Executor, typename F,
    BOOST_ASIO_COMPLETION_TOKEN_FOR(typename detail::awaitable_signature<
      typename result_of<F()>::type>::type) CompletionToken>
inline BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
    typename detail::awaitable_signature<typename result_of<F()>::type>::type)
co_spawn(const Executor& ex, F&& f, CompletionToken&& token,
    typename constraint<
      is_executor<Executor>::value || execution::is_executor<Executor>::value
    >::type)
{
  return async_initiate<CompletionToken,
    typename detail::awaitable_signature<typename result_of<F()>::type>::type>(
      detail::initiate_co_spawn<
        typename result_of<F()>::type::executor_type>(ex),
      token, std::forward<F>(f));
}

template <typename ExecutionContext, typename F,
    BOOST_ASIO_COMPLETION_TOKEN_FOR(typename detail::awaitable_signature<
      typename result_of<F()>::type>::type) CompletionToken>
inline BOOST_ASIO_INITFN_AUTO_RESULT_TYPE(CompletionToken,
    typename detail::awaitable_signature<typename result_of<F()>::type>::type)
co_spawn(ExecutionContext& ctx, F&& f, CompletionToken&& token,
    typename constraint<
      is_convertible<ExecutionContext&, execution_context&>::value
    >::type)
{
  return (co_spawn)(ctx.get_executor(), std::forward<F>(f),
      std::forward<CompletionToken>(token));
}

} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_ASIO_IMPL_CO_SPAWN_HPP
