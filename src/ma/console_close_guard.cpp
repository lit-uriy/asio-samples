//
// Copyright (c) 2010-2013 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <csignal>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <ma/console_close_guard.hpp>

namespace {

class console_close_guard_base_0 : private boost::noncopyable
{
public:
  console_close_guard_base_0()
    : io_service_()
  {
  }

protected:
  ~console_close_guard_base_0()
  {
  }

  boost::asio::io_service io_service_;
}; // console_close_guard_base_0

class console_close_guard_base_1 : public console_close_guard_base_0
{
public:
  console_close_guard_base_1()
    : console_close_guard_base_0()
    , work_guard_(io_service_)
  {
  }

protected:
  ~console_close_guard_base_1()
  {
  }

  boost::asio::io_service::work work_guard_;
}; // console_close_guard_base_1

class console_close_guard_base_2 : public console_close_guard_base_1
{
public:
  console_close_guard_base_2()
    : console_close_guard_base_1()
    , work_thread_(boost::bind(&boost::asio::io_service::run, 
          boost::ref(io_service_)))
  {
  }

protected:
  ~console_close_guard_base_2()
  {
    io_service_.stop();
    work_thread_.join();
  }

  boost::thread work_thread_;
}; // console_close_guard_base_2

} // anonymous namespace

namespace ma {

class console_close_guard::implementation : private console_close_guard_base_2
{
public:
  implementation(const ctrl_function_type& ctrl_function)
    : console_close_guard_base_2()
    , signal_set_(io_service_, SIGINT, SIGTERM)
  {
#if defined(SIGQUIT)
    signal_set_.add(SIGQUIT);
#endif // defined(SIGQUIT)
    signal_set_.async_wait(boost::bind(&handle_signal, _1, ctrl_function));
  }

  ~implementation()
  {
    boost::system::error_code ignored;
    signal_set_.cancel(ignored);
  }  
  
private:
  static void handle_signal(const boost::system::error_code& error,
      ctrl_function_type ctrl_function)
  {
    if (boost::asio::error::operation_aborted != error)
    {
      ctrl_function();
    }
  }

  boost::asio::signal_set signal_set_;
}; // class console_close_guard::implementation

console_close_guard::console_close_guard(
    const ctrl_function_type& ctrl_function)
  : implementation_(new implementation(ctrl_function))
{
}

console_close_guard::~console_close_guard()
{
}

} // namespace ma
