//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_CONTEXT_ALLOC_HANDLER_HPP
#define MA_CONTEXT_ALLOC_HANDLER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <cstddef>
#include <ma/config.hpp>
#include <ma/handler_alloc_helpers.hpp>
#include <ma/handler_invoke_helpers.hpp>

#if defined(MA_HAS_RVALUE_REFS)
#include <utility>
#include <ma/type_traits.hpp>
#endif // defined(MA_HAS_RVALUE_REFS)

namespace ma {

template <typename Context, typename Handler>
class context_alloc_handler
{
private:
  typedef context_alloc_handler<Context, Handler> this_type;
  this_type& operator=(const this_type&);

public:
  typedef void result_type;

#if defined(MA_HAS_RVALUE_REFS)

  template <typename C, typename H>
  context_alloc_handler(C&& context, H&& handler)
    : context_(std::forward<C>(context))
    , handler_(std::forward<H>(handler))
  {
  }

  context_alloc_handler(this_type&& other)
    : context_(std::move(other.context_))
    , handler_(std::move(other.handler_))
  {
  }

#else // defined(MA_HAS_RVALUE_REFS)

  context_alloc_handler(const Context& context, const Handler& handler)
    : context_(context)
    , handler_(handler)
  {
  }

#endif // defined(MA_HAS_RVALUE_REFS)

  ~context_alloc_handler()
  {
  }

  friend void* asio_handler_allocate(std::size_t size, this_type* context)
  {
    return ma_asio_handler_alloc_helpers::allocate(size, context->context_);
  }

  friend void asio_handler_deallocate(void* pointer, std::size_t size, 
      this_type* context)
  {
    ma_asio_handler_alloc_helpers::deallocate(pointer, size, context->context_);
  }

#if defined(MA_HAS_RVALUE_REFS)

  template <typename Function>
  friend void asio_handler_invoke(Function&& function, this_type* context)
  {
    ma_asio_handler_invoke_helpers::invoke(std::forward<Function>(function), 
        context->handler_);
  }

#else // defined(MA_HAS_RVALUE_REFS)

  template <typename Function>
  friend void asio_handler_invoke(const Function& function, this_type* context)
  {
    ma_asio_handler_invoke_helpers::invoke(function, context->handler_);
  }

#endif // defined(MA_HAS_RVALUE_REFS)    

  void operator()()
  {
    handler_();
  }    

  template <typename Arg1>
  void operator()(const Arg1& arg1)
  {
    handler_(arg1);
  }

  template <typename Arg1, typename Arg2>
  void operator()(const Arg1& arg1, const Arg2& arg2)
  {
    handler_(arg1, arg2);
  }

  template <typename Arg1, typename Arg2, typename Arg3>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3)
  {
    handler_(arg1, arg2, arg3);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4)
  {
    handler_(arg1, arg2, arg3, arg4);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, 
      typename Arg5>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4, const Arg5& arg5)
  {
    handler_(arg1, arg2, arg3, arg4, arg5);
  }

  void operator()() const
  {
    handler_();
  }

  template <typename Arg1>
  void operator()(const Arg1& arg1) const
  {
    handler_(arg1);
  }

  template <typename Arg1, typename Arg2>
  void operator()(const Arg1& arg1, const Arg2& arg2) const
  {
    handler_(arg1, arg2);
  }

  template <typename Arg1, typename Arg2, typename Arg3>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3) const
  {
    handler_(arg1, arg2, arg3);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4) const
  {
    handler_(arg1, arg2, arg3, arg4);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, 
      typename Arg5>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4, const Arg5& arg5) const
  {
    handler_(arg1, arg2, arg3, arg4, arg5);
  }

private:
  Context context_;
  Handler handler_;
}; //class context_alloc_handler

#if defined(MA_HAS_RVALUE_REFS)

template <typename Context, typename Handler>
inline context_alloc_handler<
    typename ma::remove_cv_reference<Context>::type, 
    typename ma::remove_cv_reference<Handler>::type>
make_context_alloc_handler(Context&& context, Handler&& handler)
{
  typedef typename ma::remove_cv_reference<Context>::type context_type;
  typedef typename ma::remove_cv_reference<Handler>::type handler_type;
  return context_alloc_handler<context_type, handler_type>(
      std::forward<Context>(context), std::forward<Handler>(handler));
}

#else // defined(MA_HAS_RVALUE_REFS)

template <typename Context, typename Handler>
inline context_alloc_handler<Context, Handler>
make_context_alloc_handler(const Context& context, const Handler& handler)
{
  return context_alloc_handler<Context, Handler>(context, handler);
}

#endif // defined(MA_HAS_RVALUE_REFS)
  
template <typename Context, typename Handler>
class context_alloc_handler2
{
private:
  typedef context_alloc_handler2<Context, Handler> this_type;
  this_type& operator=(const this_type&);

public:
  typedef void result_type;

#if defined(MA_HAS_RVALUE_REFS)

  template <typename C, typename H>
  context_alloc_handler2(C&& context, H&& handler)
    : context_(std::forward<C>(context))
    , handler_(std::forward<H>(handler))
  {
  }

  context_alloc_handler2(this_type&& other)
    : context_(std::move(other.context_))
    , handler_(std::move(other.handler_))
  {
  }

#else // defined(MA_HAS_RVALUE_REFS)

  context_alloc_handler2(const Context& context, const Handler& handler)
    : context_(context)
    , handler_(handler)
  {
  }

#endif // defined(MA_HAS_RVALUE_REFS)

  ~context_alloc_handler2()
  {
  }

  friend void* asio_handler_allocate(std::size_t size, this_type* context)
  {
    return ma_asio_handler_alloc_helpers::allocate(size, context->context_);
  }

  friend void asio_handler_deallocate(void* pointer, std::size_t size, 
      this_type* context)
  {
    ma_asio_handler_alloc_helpers::deallocate(pointer, size, 
        context->context_);
  }

#if defined(MA_HAS_RVALUE_REFS)

  template <typename Function>
  friend void asio_handler_invoke(Function&& function, this_type* context)
  {
    ma_asio_handler_invoke_helpers::invoke(std::forward<Function>(function), 
        context->handler_);
  }

#else // defined(MA_HAS_RVALUE_REFS)

  template <typename Function>
  friend void asio_handler_invoke(const Function& function, this_type* context)
  {
    ma_asio_handler_invoke_helpers::invoke(function, context->handler_);
  }

#endif // defined(MA_HAS_RVALUE_REFS)

  void operator()()
  {
    handler_(context_);
  }

  template <typename Arg1>
  void operator()(const Arg1& arg1)
  {
    handler_(context_, arg1);
  }

  template <typename Arg1, typename Arg2>
  void operator()(const Arg1& arg1, const Arg2& arg2)
  {
    handler_(context_, arg1, arg2);
  }

  template <typename Arg1, typename Arg2, typename Arg3>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3)
  {
    handler_(context_, arg1, arg2, arg3);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4)
  {
    handler_(context_, arg1, arg2, arg3, arg4);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, 
      typename Arg5>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4, const Arg5& arg5)
  {
    handler_(context_, arg1, arg2, arg3, arg4, arg5);
  }

  void operator()() const
  {
    handler_(context_);
  }

  template <typename Arg1>
  void operator()(const Arg1& arg1) const
  {
    handler_(context_, arg1);
  }

  template <typename Arg1, typename Arg2>
  void operator()(const Arg1& arg1, const Arg2& arg2) const
  {
    handler_(context_, arg1, arg2);
  }

  template <typename Arg1, typename Arg2, typename Arg3>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3) const
  {
    handler_(context_, arg1, arg2, arg3);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4) const
  {
    handler_(context_, arg1, arg2, arg3, arg4);
  }

  template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, 
      typename Arg5>
  void operator()(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, 
      const Arg4& arg4, const Arg5& arg5) const
  {
    handler_(context_, arg1, arg2, arg3, arg4, arg5);
  }

private:
  Context context_;
  Handler handler_;
}; //class context_alloc_handler2  

#if defined(MA_HAS_RVALUE_REFS)

template <typename Context, typename Handler>
inline context_alloc_handler2<
    typename ma::remove_cv_reference<Context>::type, 
    typename ma::remove_cv_reference<Handler>::type>
make_context_alloc_handler2(Context&& context, Handler&& handler)
{
  typedef typename ma::remove_cv_reference<Context>::type context_type;
  typedef typename ma::remove_cv_reference<Handler>::type handler_type;
  return context_alloc_handler2<context_type, handler_type>(
      std::forward<Context>(context), std::forward<Handler>(handler));
}

#else // defined(MA_HAS_RVALUE_REFS)

template <typename Context, typename Handler>
inline context_alloc_handler2<Context, Handler>
make_context_alloc_handler2(const Context& context, const Handler& handler)
{
  return context_alloc_handler2<Context, Handler>(context, handler);
}

#endif // defined(MA_HAS_RVALUE_REFS)  

} //namespace ma

#endif // MA_CONTEXT_ALLOC_HANDLER_HPP
