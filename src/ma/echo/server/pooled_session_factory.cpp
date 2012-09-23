//
// Copyright (c) 2010-2012 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <new>
#include <boost/ref.hpp>
#include <boost/make_shared.hpp>
#include <ma/echo/server/error.hpp>
#include <ma/echo/server/pooled_session_factory.hpp>

namespace ma {
namespace echo {
namespace server {

pooled_session_factory::pooled_session_factory(
    const io_service_vector& io_services, std::size_t max_recycled)
  : pool_()
  , current_()
{
  for (io_service_vector::const_iterator i = io_services.begin(),
      end = io_services.end(); i != end; ++i)
  {
    pool_.push_back(boost::make_shared<pool_item>(
        boost::ref(**i), max_recycled));
  }
  current_ = pool_.begin();
}

session_ptr pooled_session_factory::create(const session_config& config,
    boost::system::error_code& error)
{
  // Select appropriate item of pool
  pool_item& selected_pool_item = **current_;

  // Create new session by means of selected pool item
  session_wrapper_ptr session = 
      selected_pool_item.create(current_, config, error);

  // Prepare for the next call
  if (!error)
  {
    ++current_;
    if (current_ == pool_.end())
    {
      current_ = pool_.begin();
    }
  }

  return session;
}

void pooled_session_factory::release(const session_ptr& session)
{
  // Find session's pool item
  session_wrapper_ptr wrapped_session =
      boost::static_pointer_cast<session_wrapper>(session);
  pool_item& session_pool_item = **(wrapped_session->back_link);
  // Release session by means of its pool item
  session_pool_item.release(wrapped_session);
}

pooled_session_factory::session_wrapper_ptr
pooled_session_factory::pool_item::create(const pool_link& back_link, 
    const session_config& config, boost::system::error_code& error)
{
  session_wrapper_ptr session;
  
  if (recycled_.empty())
  {
    try
    {
      session = boost::make_shared<session_wrapper>(
          boost::ref(io_service_), config, back_link);
      ++size_;
      error = boost::system::error_code();
    }
    catch (const std::bad_alloc&)
    {
      error = server_error::no_memory;
    }
  }
  else
  {
    session = recycled_.front();
    recycled_.erase(session);
    ++size_;
    error = boost::system::error_code();
  }

  return session;
}

void pooled_session_factory::pool_item::release(
    const session_wrapper_ptr& session)
{
  if (max_recycled_ > recycled_.size())
  {
    recycled_.push_front(session);
  }
}

} // namespace server
} // namespace echo
} // namespace ma
