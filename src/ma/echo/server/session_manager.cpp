//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <new>
#include <boost/ref.hpp>
#include <boost/assert.hpp>
#include <boost/make_shared.hpp>
#include <boost/utility/addressof.hpp>
#include <ma/config.hpp>
#include <ma/custom_alloc_handler.hpp>
#include <ma/strand_wrapped_handler.hpp>
#include <ma/echo/server/error.hpp>
#include <ma/echo/server/session.hpp>
#include <ma/echo/server/session_manager.hpp>

namespace ma {

namespace echo {

namespace server {

#if defined(MA_HAS_RVALUE_REFS) \
  && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

// Home-grown binders to support move semantic
class session_manager::accept_handler_binder
{
private:
  typedef accept_handler_binder this_type;

public:
  typedef void result_type;
  typedef void (session_manager::*function_type)(
      const session_manager::session_data_ptr&,
      const boost::system::error_code&);

  template <typename SessionManagerPtr, typename SessionWrapperPtr>
  accept_handler_binder(function_type function, 
      SessionManagerPtr&& the_session_manager, 
      SessionWrapperPtr&& the_session_data)
    : function_(function)
    , session_manager_(std::forward<SessionManagerPtr>(the_session_manager))
    , session_data_(std::forward<SessionWrapperPtr>(the_session_data))
  {
  }

#if defined(MA_USE_EXPLICIT_MOVE_CONSTRUCTOR)

  accept_handler_binder(this_type&& other)
    : function_(other.function_)
    , session_manager_(std::move(other.session_manager_))
    , session_data_(std::move(other.session_data_))
  {
  }

  accept_handler_binder(const this_type& other)
    : function_(other.function_)
    , session_manager_(other.session_manager_)
    , session_data_(other.session_data_)
  {
  }

#endif // defined(MA_USE_EXPLICIT_MOVE_CONSTRUCTOR)

  void operator()(const boost::system::error_code& error)
  {
    ((*session_manager_).*function_)(session_data_, error);
  }

private:
  function_type function_;
  session_manager_ptr session_manager_;
  session_manager::session_data_ptr session_data_;
}; // class session_manager::accept_handler_binder

class session_manager::session_dispatch_binder
{
private:
  typedef session_dispatch_binder this_type;

public:
  typedef void (*function_type)(
    const session_manager_weak_ptr&,
    const session_manager::session_data_ptr&, 
    const boost::system::error_code&);

  template <typename SessionManagerPtr, typename SessionWrapperPtr>
  session_dispatch_binder(function_type function, 
    SessionManagerPtr&& the_session_manager,
    SessionWrapperPtr&& the_session_data)
    : function_(function)
    , session_manager_(std::forward<SessionManagerPtr>(the_session_manager))
    , session_data_(std::forward<SessionWrapperPtr>(the_session_data))
  {
  }

#if defined(MA_USE_EXPLICIT_MOVE_CONSTRUCTOR)

  session_dispatch_binder(this_type&& other)
    : function_(other.function_)
    , session_manager_(std::move(other.session_manager_))
    , session_data_(std::move(other.session_data_))
  {
  }

  session_dispatch_binder(const this_type& other)
    : function_(other.function_)
    , session_manager_(other.session_manager_)
    , session_data_(other.session_data_)
  {
  }

#endif // defined(MA_USE_EXPLICIT_MOVE_CONSTRUCTOR)

  void operator()(const boost::system::error_code& error)
  {
    function_(session_manager_, session_data_, error);
  }

private:
  function_type function_;
  session_manager_weak_ptr session_manager_;
  session_manager::session_data_ptr session_data_;
}; // class session_manager::session_dispatch_binder      

class session_manager::session_handler_binder
{
private:
  typedef session_handler_binder this_type;

public:
  typedef void result_type;
  typedef void (session_manager::*function_type)(
      const session_manager::session_data_ptr&,
      const boost::system::error_code&);

  template <typename SessionManagerPtr, typename SessionWrapperPtr>
  session_handler_binder(function_type function, 
      SessionManagerPtr&& the_session_manager, 
      SessionWrapperPtr&& the_session_data, 
      const boost::system::error_code& error)
    : function_(function)
    , session_manager_(std::forward<SessionManagerPtr>(the_session_manager))
    , session_data_(std::forward<SessionWrapperPtr>(the_session_data))
    , error_(error)
  {
  }                  

#if defined(MA_USE_EXPLICIT_MOVE_CONSTRUCTOR)

  session_handler_binder(this_type&& other)
    : function_(other.function_)
    , session_manager_(std::move(other.session_manager_))
    , session_data_(std::move(other.session_data_))
    , error_(std::move(other.error_))
  {
  }

  session_handler_binder(const this_type& other)
    : function_(other.function_)
    , session_manager_(other.session_manager_)
    , session_data_(other.session_data_)
    , error_(other.error_)
  {
  }

#endif // defined(MA_USE_EXPLICIT_MOVE_CONSTRUCTOR)

  void operator()()
  {
    ((*session_manager_).*function_)(session_data_, error_);
  }

private:
  function_type function_;
  session_manager_ptr session_manager_;
  session_manager::session_data_ptr session_data_;
  boost::system::error_code error_;
}; // class session_manager::session_handler_binder

#endif // defined(MA_HAS_RVALUE_REFS) 
       //     && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

session_manager::session_data::session_data(
    boost::asio::io_service& io_service, const session_config& config)
  : state(ready)
  , pending_operations(0)
  , managed_session(boost::make_shared<session>(
        boost::ref(io_service), config))
{
}            

void session_manager::session_data_list::push_front(
    const session_data_ptr& value)
{
  BOOST_ASSERT((!value->next && !value->prev.lock()));

  value->next = front_;
  value->prev.reset();
  if (front_)
  {
    front_->prev = value;
  }
  front_ = value;
  ++size_;
}

void session_manager::session_data_list::erase(const session_data_ptr& value)
{
  if (front_ == value)
  {
    front_ = front_->next;
  }
  session_data_ptr prev = value->prev.lock();
  if (prev)
  {
    prev->next = value->next;
  }
  if (value->next)
  {
    value->next->prev = prev;
  }
  value->prev.reset();
  value->next.reset();
  --size_;
}

void session_manager::session_data_list::clear()
{
  front_.reset();
}

session_manager::session_manager(boost::asio::io_service& io_service, 
    boost::asio::io_service& session_io_service, 
    const session_manager_config& config)
  : accepting_endpoint_(config.accepting_endpoint)
  , listen_backlog_(config.listen_backlog)
  , max_session_count_(config.max_session_count)
  , recycled_session_count_(config.recycled_session_count)
  , managed_session_config_(config.managed_session_config)
  , accept_in_progress_(false)
  , pending_operations_(0)
  , external_state_(external_state::ready)
  , io_service_(io_service)
  , session_io_service_(session_io_service)
  , strand_(io_service)
  , acceptor_(io_service)        
  , wait_handler_(io_service)
  , stop_handler_(io_service)        
{          
}

void session_manager::reset(bool free_recycled_sessions)
{
  boost::system::error_code ignored;
  acceptor_.close(ignored);

  wait_error_.clear();
  stop_error_.clear();
        
  active_sessions_.clear();
  if (free_recycled_sessions)
  {
    recycled_sessions_.clear();
  }

  external_state_ = external_state::ready;
}

boost::system::error_code session_manager::start()
{
  if (external_state::ready != external_state_)
  {
    return server_error::invalid_state;
  }
  external_state_ = external_state::start;

  boost::system::error_code error;
  open(acceptor_, accepting_endpoint_, listen_backlog_, error);

  if (!error)
  {
    session_data_ptr the_session_data = create_session(error);
    if (error)
    {
      boost::system::error_code ignored;
      acceptor_.close(ignored);
    }
    else
    {
      accept_session(the_session_data);
    }
  }

  if (error)
  {
    external_state_ = external_state::stopped;         
  }
  else
  {          
    external_state_ = external_state::work;
  }

  return error;
}

boost::optional<boost::system::error_code> session_manager::stop()
{        
  if ((external_state::stopped == external_state_)
      || (external_state::stop == external_state_))
  {          
    return boost::system::error_code(server_error::invalid_state);
  }

  // Start shutdown
  external_state_ = external_state::stop;

  // Do shutdown - abort outer operations
  if (wait_handler_.has_target())
  {
    wait_handler_.post(server_error::operation_aborted);
  }

  // Do shutdown - abort inner operations
  acceptor_.close(stop_error_);

  // Stop all active sessions
  session_data_ptr the_session_data = active_sessions_.front();
  while (the_session_data)
  {
    if (session_data::stop != the_session_data->state)
    {
      stop_session(the_session_data);
    }
    the_session_data = the_session_data->next;
  }

  // Check for shutdown continuation
  if (may_complete_stop())
  {
    complete_stop();
    return stop_error_;          
  }

  return boost::optional<boost::system::error_code>();
}

boost::optional<boost::system::error_code> session_manager::wait()
{        
  if ((external_state::work != external_state_)
      || wait_handler_.has_target())
  {
    return boost::system::error_code(server_error::invalid_state);
  }        
  if (may_complete_wait())
  {
    return wait_error_;
  }        
  return boost::optional<boost::system::error_code>();
}

session_manager::session_data_ptr session_manager::create_session(
    boost::system::error_code& error)
{        
  if (!recycled_sessions_.empty())
  {
    session_data_ptr the_session_data = recycled_sessions_.front();
    recycled_sessions_.erase(the_session_data);          
    error = boost::system::error_code();
    return the_session_data;
  }

  try 
  {   
    session_data_ptr the_session_data = boost::make_shared<session_data>(
        boost::ref(session_io_service_), managed_session_config_);
    error = boost::system::error_code();
    return the_session_data;
  }
  catch (const std::bad_alloc&) 
  {
    error = server_error::no_memory;
    return session_data_ptr();
  }
}

void session_manager::accept_session(const session_data_ptr& the_session_data)
{
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

  acceptor_.async_accept(the_session_data->managed_session->socket(), 
      the_session_data->remote_endpoint, MA_STRAND_WRAP(strand_, 
          make_custom_alloc_handler(accept_allocator_, accept_handler_binder(
              &this_type::handle_session_accept, shared_from_this(), 
              the_session_data))));

#else

  acceptor_.async_accept(the_session_data->managed_session->socket(), 
      the_session_data->remote_endpoint, MA_STRAND_WRAP(strand_, 
          make_custom_alloc_handler(accept_allocator_, boost::bind(
              &this_type::handle_session_accept, shared_from_this(), 
              the_session_data, boost::asio::placeholders::error))));

#endif

  // Register pending operation
  ++pending_operations_;
  accept_in_progress_ = true;
}

void session_manager::continue_accept()
{
  // Get new, ready to start session
  boost::system::error_code error;
  session_data_ptr the_session_data = create_session(error);
  if (error)
  {
    // Handle new session creation error
    set_wait_error(error);
    // Can't do anything more
    return;
  }
  // Start session acceptation
  accept_session(the_session_data);
}

void session_manager::handle_session_accept(
    const session_data_ptr& the_session_data, 
    const boost::system::error_code& error)
{
  // Unregister pending operation
  --pending_operations_;
  accept_in_progress_ = false;

  // Check for pending session manager stop operation 
  if (external_state::stop == external_state_)
  {
    if (may_complete_stop())
    {
      complete_stop();
      post_stop_handler();
    }
    recycle_session(the_session_data);
    return;
  }

  if (error)
  {   
    set_wait_error(error);
    recycle_session(the_session_data);
    if (may_continue_accept())
    {
      continue_accept();
    }
    return;
  }

  if (active_sessions_.size() < max_session_count_)
  {
    // Start accepted session 
    start_session(the_session_data);
    // Save session as active
    active_sessions_.push_front(the_session_data);
    // Continue session acceptation if can
    if (may_continue_accept())
    {
      continue_accept();
    }
    return;
  }

  recycle_session(the_session_data);
}

void session_manager::open(protocol_type::acceptor& acceptor, 
    const protocol_type::endpoint& endpoint, int backlog, 
    boost::system::error_code& error)
{
  boost::system::error_code local_error;

  acceptor.open(endpoint.protocol(), local_error);
  if (local_error)
  {
    error = local_error;
    return;
  }

  class acceptor_guard : private boost::noncopyable
  {
  public:
    typedef protocol_type::acceptor guarded_type;

    acceptor_guard(guarded_type& guarded)
      : guarded_(boost::addressof(guarded))
    {
    }

    ~acceptor_guard()
    {
      if (guarded_)
      {
        boost::system::error_code ignored;
        guarded_->close(ignored);
      }            
    }

    void release()
    {
      guarded_ = 0;
    }

  private:    
    guarded_type* guarded_;
  }; // class acceptor_guard

  acceptor_guard closing_guard(acceptor);                

  protocol_type::acceptor::reuse_address reuse_address_opt(true);
  acceptor.set_option(reuse_address_opt, local_error);
  if (local_error)
  {       
    error = local_error;
    return;
  }

  acceptor.bind(endpoint, local_error);
  if (local_error)
  {     
    error = local_error;
    return;
  }

  acceptor.listen(backlog, error);

  if (!error)
  {
    closing_guard.release();
  }
}

bool session_manager::may_complete_stop() const
{
  return (0 == pending_operations_) && active_sessions_.empty();
}

bool session_manager::may_complete_wait() const
{
  return wait_error_ && active_sessions_.empty() && !accept_in_progress_;
}

void session_manager::complete_stop()
{
  external_state_ = external_state::stopped;  
}

void session_manager::set_wait_error(const boost::system::error_code& error)
{
  if (!wait_error_)
  {
    wait_error_ = error;
  }
  // Notify wait handler
  if (wait_handler_.has_target() && may_complete_wait())
  {            
    wait_handler_.post(wait_error_);
  }
}

bool session_manager::may_continue_accept() const
{
  if (accept_in_progress_ || (active_sessions_.size() >= max_session_count_))
  {
    return false;
  }

  if (wait_error_)
  {
    if (server_error::no_memory == wait_error_)
    {
      return true;
    }
    return false;
  }

  return true;
}

void session_manager::start_session(const session_data_ptr& the_session_data)
{ 
  // Asynchronously start wrapped session
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

  the_session_data->managed_session->async_start(make_custom_alloc_handler(
      the_session_data->start_wait_allocator, session_dispatch_binder(
          &this_type::dispatch_session_start, shared_from_this(), 
          the_session_data)));

#else

  the_session_data->managed_session->async_start(make_custom_alloc_handler(
      the_session_data->start_wait_allocator, boost::bind(
          &this_type::dispatch_session_start, 
          session_manager_weak_ptr(shared_from_this()), 
          the_session_data, _1)));

#endif

  the_session_data->state = session_data::start;

  // Register pending operation
  ++pending_operations_;
  ++the_session_data->pending_operations;        
}

void session_manager::stop_session(const session_data_ptr& the_session_data)
{
  // Asynchronously stop wrapped session
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

  the_session_data->managed_session->async_stop(make_custom_alloc_handler(
      the_session_data->stop_allocator, session_dispatch_binder(
          &this_type::dispatch_session_stop, shared_from_this(), 
          the_session_data)));

#else

  the_session_data->managed_session->async_stop(make_custom_alloc_handler(
      the_session_data->stop_allocator, boost::bind(
          &this_type::dispatch_session_stop, 
          session_manager_weak_ptr(shared_from_this()), 
          the_session_data, _1)));

#endif

  the_session_data->state = session_data::stop;

  // Register pending operation
  ++pending_operations_;
  ++the_session_data->pending_operations;        
}

void session_manager::wait_session(const session_data_ptr& the_session_data)
{
  // Asynchronously wait on wrapped session
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

  the_session_data->managed_session->async_wait(make_custom_alloc_handler(
      the_session_data->start_wait_allocator, session_dispatch_binder(
          &this_type::dispatch_session_wait, shared_from_this(), 
          the_session_data)));

#else

  the_session_data->managed_session->async_wait(make_custom_alloc_handler(
      the_session_data->start_wait_allocator, boost::bind(
          &this_type::dispatch_session_wait, 
          session_manager_weak_ptr(shared_from_this()), 
          the_session_data, _1)));

#endif

  // Register pending operation
  ++pending_operations_;
  ++the_session_data->pending_operations;
}

void session_manager::dispatch_session_start(
    const session_manager_weak_ptr& this_weak_ptr, 
    const session_data_ptr& the_session_data, 
    const boost::system::error_code& error)
{
  // Try to lock the manager
  if (session_manager_ptr this_ptr = this_weak_ptr.lock())
  {
    // Forward invocation
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

    this_ptr->strand_.dispatch(make_custom_alloc_handler(
        the_session_data->start_wait_allocator, session_handler_binder(
            &this_type::handle_session_start, this_ptr, 
            the_session_data, error)));

#else

    this_ptr->strand_.dispatch(make_custom_alloc_handler(
        the_session_data->start_wait_allocator, boost::bind(
            &this_type::handle_session_start, this_ptr, 
            the_session_data, error)));

#endif
  }
}

void session_manager::handle_session_start(
    const session_data_ptr& the_session_data, 
    const boost::system::error_code& error)
{
  // Unregister pending operation
  --pending_operations_;
  --the_session_data->pending_operations;

  // Check if handler is not called too late
  if (session_data::start == the_session_data->state)
  {
    if (error)
    {
      the_session_data->state = session_data::stopped;
      active_sessions_.erase(the_session_data);
      recycle_session(the_session_data);

      // Check for pending session manager stop operation 
      if (external_state::stop == external_state_)
      {
        if (may_complete_stop())
        {
          complete_stop();
          post_stop_handler();
        }
        return;
      }

      // Continue session acceptation if can
      if (may_continue_accept())
      {                
        continue_accept();
      }

      if (wait_handler_.has_target() && may_complete_wait())
      {
        wait_handler_.post(wait_error_);
      }
      return;
    }

    the_session_data->state = session_data::work;
    // Check for pending session manager stop operation 
    if (external_state::stop == external_state_)  
    {                            
      stop_session(the_session_data);
      return;
    }

    // Wait until session needs to stop
    wait_session(the_session_data);          
    return;
  }

  // Handler is called too late - complete handler's waiters
  recycle_session(the_session_data);
  // Check for pending session manager stop operation 
  if (external_state::stop == external_state_) 
  {
    if (may_complete_stop())  
    {
      complete_stop();
      post_stop_handler();
    }
  }        
}

void session_manager::dispatch_session_wait(
    const session_manager_weak_ptr& this_weak_ptr,
    const session_data_ptr& the_session_data, 
    const boost::system::error_code& error)
{
  // Try to lock the manager
  if (session_manager_ptr this_ptr = this_weak_ptr.lock())
  {
    // Forward invocation
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

    this_ptr->strand_.dispatch(make_custom_alloc_handler(
        the_session_data->start_wait_allocator, session_handler_binder(
            &this_type::handle_session_wait, this_ptr, 
            the_session_data, error)));

#else

    this_ptr->strand_.dispatch(make_custom_alloc_handler(
        the_session_data->start_wait_allocator, boost::bind(
            &this_type::handle_session_wait, this_ptr, 
            the_session_data, error)));

#endif
  }
}

void session_manager::handle_session_wait(
    const session_data_ptr& the_session_data, 
    const boost::system::error_code& /*error*/)
{
  // Unregister pending operation
  --pending_operations_;
  --the_session_data->pending_operations;

  // Check if handler is not called too late
  if (session_data::work == the_session_data->state)
  {
    stop_session(the_session_data);
    return;
  }

  // Handler is called too late - complete handler's waiters
  recycle_session(the_session_data);

  // Check for pending session manager stop operation
  if (external_state::stop == external_state_)
  {
    if (may_complete_stop())  
    {
      complete_stop();
      post_stop_handler();
    }          
  }        
}

void session_manager::dispatch_session_stop(
    const session_manager_weak_ptr& this_weak_ptr,
    const session_data_ptr& the_session_data, 
    const boost::system::error_code& error)
{     
  // Try to lock the manager
  if (session_manager_ptr this_ptr = this_weak_ptr.lock())
  {
    // Forward invocation
#if defined(MA_HAS_RVALUE_REFS) \
    && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

    this_ptr->strand_.dispatch(make_custom_alloc_handler(
        the_session_data->stop_allocator, session_handler_binder(
            &this_type::handle_session_stop, this_ptr, 
            the_session_data, error)));

#else

    this_ptr->strand_.dispatch(make_custom_alloc_handler(
        the_session_data->stop_allocator, boost::bind(
            &this_type::handle_session_stop, this_ptr, 
            the_session_data, error)));

#endif
  }
}

void session_manager::handle_session_stop(
    const session_data_ptr& the_session_data,
    const boost::system::error_code& /*error*/)
{
  // Unregister pending operation
  --pending_operations_;
  --the_session_data->pending_operations;

  // Check if handler is not called too late
  if (session_data::stop == the_session_data->state)        
  {
    the_session_data->state = session_data::stopped;
    active_sessions_.erase(the_session_data);
    recycle_session(the_session_data);

    // Check for pending session manager stop operation
    if (external_state::stop == external_state_)
    {
      if (may_complete_stop())  
      {
        complete_stop();
        post_stop_handler();
      }
      return;
    }

    // Continue session acceptation if can
    if (may_continue_accept())
    {                
      continue_accept();
    }

    if (wait_handler_.has_target() && may_complete_wait()) 
    {
      wait_handler_.post(wait_error_);
    }
    return;
  }

  // Handler is called too late - complete handler's waiters
  recycle_session(the_session_data);

  // Check for pending session manager stop operation
  if (external_state::stop == external_state_)
  {
    if (may_complete_stop())  
    {
      complete_stop();
      post_stop_handler();
    }          
  }                
}

void session_manager::recycle_session(const session_data_ptr& the_session_data)
{
  // Check session's pending operation number and recycle bin size
  if ((0 == the_session_data->pending_operations) 
      && (recycled_sessions_.size() < recycled_session_count_))
  {
    // Reset session state
    the_session_data->managed_session->reset();
    // Reset wrapper
    the_session_data->state = session_data::ready;
    // Add to recycle bin
    recycled_sessions_.push_front(the_session_data);
  }
}

void session_manager::post_stop_handler()
{
  if (stop_handler_.has_target()) 
  {
    // Signal shutdown completion
    stop_handler_.post(stop_error_);
  }
}

} // namespace server
} // namespace echo
} // namespace ma

