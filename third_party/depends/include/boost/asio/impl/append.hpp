//
// impl/append.hpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2022 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_IMPL_APPEND_HPP
#define BOOST_ASIO_IMPL_APPEND_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>

#include <boost/asio/associator.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_cont_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/asio/detail/type_traits.hpp>
#include <boost/asio/detail/utility.hpp>
#include <boost/asio/detail/variadic_templates.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace detail {

// Class to adapt an append_t as a completion handler.
template <typename Handler, typename... Values>
class append_handler
{
public:
  typedef void result_type;

  template <typename H>
  append_handler(BOOST_ASIO_MOVE_ARG(H) handler, std::tuple<Values...> values)
    : handler_(BOOST_ASIO_MOVE_CAST(H)(handler)),
      values_(BOOST_ASIO_MOVE_CAST(std::tuple<Values...>)(values))
  {
  }

  template <typename... Args>
  void operator()(BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    this->invoke(
        index_sequence_for<Values...>{},
        BOOST_ASIO_MOVE_CAST(Args)(args)...);
  }

  template <std::size_t... I, typename... Args>
  void invoke(index_sequence<I...>, BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    BOOST_ASIO_MOVE_OR_LVALUE(Handler)(handler_)(
        BOOST_ASIO_MOVE_CAST(Args)(args)...,
        BOOST_ASIO_MOVE_CAST(Values)(std::get<I>(values_))...);
  }

//private:
  Handler handler_;
  std::tuple<Values...> values_;
};

template <typename Handler>
inline asio_handler_allocate_is_deprecated
asio_handler_allocate(std::size_t size,
    append_handler<Handler>* this_handler)
{
#if defined(BOOST_ASIO_NO_DEPRECATED)
  boost_asio_handler_alloc_helpers::allocate(size, this_handler->handler_);
  return asio_handler_allocate_is_no_longer_used();
#else // defined(BOOST_ASIO_NO_DEPRECATED)
  return boost_asio_handler_alloc_helpers::allocate(
      size, this_handler->handler_);
#endif // defined(BOOST_ASIO_NO_DEPRECATED)
}

template <typename Handler>
inline asio_handler_deallocate_is_deprecated
asio_handler_deallocate(void* pointer, std::size_t size,
    append_handler<Handler>* this_handler)
{
  boost_asio_handler_alloc_helpers::deallocate(
      pointer, size, this_handler->handler_);
#if defined(BOOST_ASIO_NO_DEPRECATED)
  return asio_handler_deallocate_is_no_longer_used();
#endif // defined(BOOST_ASIO_NO_DEPRECATED)
}

template <typename Handler>
inline bool asio_handler_is_continuation(
    append_handler<Handler>* this_handler)
{
  return boost_asio_handler_cont_helpers::is_continuation(
      this_handler->handler_);
}

template <typename Function, typename Handler>
inline asio_handler_invoke_is_deprecated
asio_handler_invoke(Function& function,
    append_handler<Handler>* this_handler)
{
  boost_asio_handler_invoke_helpers::invoke(
      function, this_handler->handler_);
#if defined(BOOST_ASIO_NO_DEPRECATED)
  return asio_handler_invoke_is_no_longer_used();
#endif // defined(BOOST_ASIO_NO_DEPRECATED)
}

template <typename Function, typename Handler>
inline asio_handler_invoke_is_deprecated
asio_handler_invoke(const Function& function,
    append_handler<Handler>* this_handler)
{
  boost_asio_handler_invoke_helpers::invoke(
      function, this_handler->handler_);
#if defined(BOOST_ASIO_NO_DEPRECATED)
  return asio_handler_invoke_is_no_longer_used();
#endif // defined(BOOST_ASIO_NO_DEPRECATED)
}

template <typename Signature, typename... Values>
struct append_signature;

template <typename R, typename... Args, typename... Values>
struct append_signature<R(Args...), Values...>
{
  typedef R type(typename decay<Args>::type..., Values...);
};

} // namespace detail

#if !defined(GENERATING_DOCUMENTATION)

template <typename CompletionToken, typename... Values, typename Signature>
struct async_result<
    append_t<CompletionToken, Values...>, Signature>
  : async_result<CompletionToken,
      typename detail::append_signature<
        Signature, Values...>::type>
{
  typedef typename detail::append_signature<
      Signature, Values...>::type signature;

  template <typename Initiation>
  struct init_wrapper
  {
    init_wrapper(Initiation init)
      : initiation_(BOOST_ASIO_MOVE_CAST(Initiation)(init))
    {
    }

    template <typename Handler, typename... Args>
    void operator()(
        BOOST_ASIO_MOVE_ARG(Handler) handler,
        std::tuple<Values...> values,
        BOOST_ASIO_MOVE_ARG(Args)... args)
    {
      BOOST_ASIO_MOVE_CAST(Initiation)(initiation_)(
          detail::append_handler<
            typename decay<Handler>::type, Values...>(
              BOOST_ASIO_MOVE_CAST(Handler)(handler),
              BOOST_ASIO_MOVE_CAST(std::tuple<Values...>)(values)),
          BOOST_ASIO_MOVE_CAST(Args)(args)...);
    }

    Initiation initiation_;
  };

  template <typename Initiation, typename RawCompletionToken, typename... Args>
  static BOOST_ASIO_INITFN_DEDUCED_RESULT_TYPE(CompletionToken, signature,
      (async_initiate<CompletionToken, signature>(
        declval<init_wrapper<typename decay<Initiation>::type> >(),
        declval<CompletionToken&>(),
        declval<std::tuple<Values...> >(),
        declval<BOOST_ASIO_MOVE_ARG(Args)>()...)))
  initiate(
      BOOST_ASIO_MOVE_ARG(Initiation) initiation,
      BOOST_ASIO_MOVE_ARG(RawCompletionToken) token,
      BOOST_ASIO_MOVE_ARG(Args)... args)
  {
    return async_initiate<CompletionToken, signature>(
        init_wrapper<typename decay<Initiation>::type>(
          BOOST_ASIO_MOVE_CAST(Initiation)(initiation)),
        token.token_,
        BOOST_ASIO_MOVE_CAST(std::tuple<Values...>)(token.values_),
        BOOST_ASIO_MOVE_CAST(Args)(args)...);
  }
};

template <template <typename, typename> class Associator,
    typename Handler, typename... Values, typename DefaultCandidate>
struct associator<Associator,
    detail::append_handler<Handler, Values...>, DefaultCandidate>
  : Associator<Handler, DefaultCandidate>
{
  static typename Associator<Handler, DefaultCandidate>::type
  get(const detail::append_handler<Handler, Values...>& h) BOOST_ASIO_NOEXCEPT
  {
    return Associator<Handler, DefaultCandidate>::get(h.handler_);
  }

  static BOOST_ASIO_AUTO_RETURN_TYPE_PREFIX2(
      typename Associator<Handler, DefaultCandidate>::type)
  get(const detail::append_handler<Handler, Values...>& h,
      const DefaultCandidate& c) BOOST_ASIO_NOEXCEPT
    BOOST_ASIO_AUTO_RETURN_TYPE_SUFFIX((
      Associator<Handler, DefaultCandidate>::get(h.handler_, c)))
  {
    return Associator<Handler, DefaultCandidate>::get(h.handler_, c);
  }
};

#endif // !defined(GENERATING_DOCUMENTATION)

} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_ASIO_IMPL_APPEND_HPP
