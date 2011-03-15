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
#include <ma/config.hpp>
#include <ma/custom_alloc_handler.hpp>
#include <ma/strand_wrapped_handler.hpp>
#include <ma/echo/server/error.hpp>
#include <ma/echo/server/session.hpp>
#include <ma/echo/server/session_manager.hpp>

namespace ma
{    
  namespace echo
  {    
    namespace server
    {  
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
      class session_manager::accept_handler_binder
      {
      private:
        typedef accept_handler_binder this_type;
        this_type& operator=(const this_type&);

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

#if defined(_DEBUG)
        accept_handler_binder(const this_type& other)
          : function_(other.function_)
          , session_manager_(other.session_manager_)
          , session_data_(other.session_data_)
        {
        }
#endif // defined(_DEBUG)

        accept_handler_binder(this_type&& other)
          : function_(other.function_)
          , session_manager_(std::move(other.session_manager_))
          , session_data_(std::move(other.session_data_))
        {
        }

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
        this_type& operator=(const this_type&);

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

#if defined(_DEBUG)
        session_dispatch_binder(const this_type& other)
          : function_(other.function_)
          , session_manager_(other.session_manager_)
          , session_data_(other.session_data_)
        {
        }
#endif // defined(_DEBUG)

        session_dispatch_binder(this_type&& other)
          : function_(other.function_)
          , session_manager_(std::move(other.session_manager_))
          , session_data_(std::move(other.session_data_))
        {
        }

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
        this_type& operator=(const this_type&);

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

#if defined(_DEBUG)
        session_handler_binder(const this_type& other)
          : function_(other.function_)
          , session_manager_(other.session_manager_)
          , session_data_(other.session_data_)
          , error_(other.error_)
        {
        }
#endif // defined(_DEBUG)

        session_handler_binder(this_type&& other)
          : function_(other.function_)
          , session_manager_(std::move(other.session_manager_))
          , session_data_(std::move(other.session_data_))
          , error_(std::move(other.error_))
        {
        }

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

      session_manager::session_data::session_data(boost::asio::io_service& io_service,
        const session_config& config)
        : state(ready_to_start)
        , pending_operations(0)
        , managed_session(boost::make_shared<session>(boost::ref(io_service), config))
      {
      }
      
      session_manager::session_data_list::session_data_list()
        : size_(0)
      {
      }

      void session_manager::session_data_list::push_front(const session_data_ptr& value)
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
        boost::asio::io_service& session_io_service, const session_manager_config& config)
        : accepting_endpoint_(config.accepting_endpoint())
        , listen_backlog_(config.listen_backlog())
        , max_session_count_(config.max_session_count())
        , recycled_session_count_(config.recycled_session_count())
        , managed_session_config_(config.managed_session_config())
        , accept_in_progress_(false)
        , state_(ready_to_start)
        , pending_operations_(0)
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
        state_ = ready_to_start;
        active_sessions_.clear();
        if (free_recycled_sessions)
        {
          recycled_sessions_.clear();
        }
      }

      boost::system::error_code session_manager::start()
      {         
        if (ready_to_start != state_)
        {          
          return server_error::invalid_state;
        }        
        state_ = start_in_progress;
        boost::system::error_code error;
        acceptor_.open(accepting_endpoint_.protocol(), error);
        if (!error)
        {
          acceptor_.bind(accepting_endpoint_, error);
          if (!error)
          {
            acceptor_.listen(listen_backlog_, error);          
            if (!error)
            {
              session_data_ptr new_session_data = create_session(error);
              if (!error) 
              {
                accept_session(new_session_data); 
              }            
            }
          }
          if (error)
          {
            boost::system::error_code ignored;
            acceptor_.close(ignored);          
          }
        }
        if (error)
        {
          state_ = stopped;         
        }
        else
        {          
          state_ = started;
        }       
        return error;
      }

      boost::optional<boost::system::error_code> session_manager::stop()
      {        
        if (stopped == state_ || stop_in_progress == state_)
        {          
          return boost::system::error_code(server_error::invalid_state);
        }        
        // Start shutdown
        state_ = stop_in_progress;
        // Do shutdown - abort inner operations          
        acceptor_.close(stop_error_);
        // Stop all active sessions
        session_data_ptr active_session = active_sessions_.front();
        while (active_session)
        {
          if (session_data::stop_in_progress != active_session->state)
          {
            stop_session(active_session);
          }
          active_session = active_session->next;
        }
        // Do shutdown - abort outer operations
        if (wait_handler_.has_target())
        {
          wait_handler_.post(server_error::operation_aborted);
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
        if (started != state_ || wait_handler_.has_target())
        {
          return boost::system::error_code(server_error::invalid_state);
        }        
        if (may_complete_wait())
        {
          return wait_error_;
        }        
        return boost::optional<boost::system::error_code>();
      }

      session_manager::session_data_ptr session_manager::create_session(boost::system::error_code& error)
      {        
        if (!recycled_sessions_.empty())
        {          
          session_data_ptr new_session_data = recycled_sessions_.front();
          recycled_sessions_.erase(new_session_data);
          error = boost::system::error_code();
          return new_session_data;
        }        
        try 
        {   
          session_data_ptr new_session_data = boost::make_shared<session_data>(
              boost::ref(session_io_service_), managed_session_config_);
          error = boost::system::error_code();
          return new_session_data;
        }
        catch (const std::bad_alloc&) 
        {
          error = boost::system::errc::make_error_code(boost::system::errc::not_enough_memory);
          return session_data_ptr();
        }        
      }

      void session_manager::accept_session(const session_data_ptr& new_session_data)
      {
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
        acceptor_.async_accept(new_session_data->managed_session->socket(), new_session_data->remote_endpoint, 
          MA_STRAND_WRAP(strand_, make_custom_alloc_handler(accept_allocator_,
			      accept_handler_binder(&this_type::handle_accept, shared_from_this(), new_session_data))));
#else
        acceptor_.async_accept(new_session_data->managed_session->socket(), new_session_data->remote_endpoint, 
          MA_STRAND_WRAP(strand_, make_custom_alloc_handler(accept_allocator_,
			      boost::bind(&this_type::handle_accept, shared_from_this(), 
              new_session_data, boost::asio::placeholders::error))));
#endif
        // Register pending operation
        ++pending_operations_;
        accept_in_progress_ = true;
      }

      void session_manager::accept_new_session()
      {
        // Get new, ready to start session
        boost::system::error_code error;
        session_data_ptr new_session_data = create_session(error);
        if (error)
        {
          // Handle new session creation error
          wait_error_ = error;
          // Notify wait handler
          if (wait_handler_.has_target() && may_complete_wait()) 
          {            
            wait_handler_.post(wait_error_);
          }      
          // Can't do more
          return;
        }        
        // Start session acceptation
        accept_session(new_session_data);
      }

      void session_manager::handle_accept(const session_data_ptr& accepted_session_data, 
        const boost::system::error_code& error)
      {
        // Unregister pending operation
        --pending_operations_;
        accept_in_progress_ = false;
        // Check for pending session manager stop operation 
        if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            complete_stop();
            post_stop_handler();
          }
          recycle_session(accepted_session_data);
          return;
        }
        if (error)
        {   
          wait_error_ = error;
          // Notify wait handler
          if (wait_handler_.has_target() && may_complete_wait()) 
          {            
            wait_handler_.post(wait_error_);
          }
          recycle_session(accepted_session_data);
          if (may_continue_accept())
          {
            accept_new_session();
          }
          return;
        }
        if (active_sessions_.size() < max_session_count_)
        { 
          // Start accepted session 
          start_session(accepted_session_data);          
          // Save session as active
          active_sessions_.push_front(accepted_session_data);
          // Continue session acceptation if can
          if (may_continue_accept())
          {
            accept_new_session();
          }
          return;
        }
        recycle_session(accepted_session_data);
      }

      bool session_manager::may_complete_stop() const
      {
        return 0 == pending_operations_ && active_sessions_.empty();
      }

      bool session_manager::may_complete_wait() const
      {
        return wait_error_ && active_sessions_.empty();
      }

      void session_manager::complete_stop()
      {
        state_ = stopped;  
      }

      bool session_manager::may_continue_accept() const
      {
        return !accept_in_progress_ && !wait_error_
          && active_sessions_.size() < max_session_count_;
      }

      void session_manager::start_session(const session_data_ptr& accepted_session_data)
      { 
        // Asynchronously start wrapped session
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
        accepted_session_data->managed_session->async_start(
          make_custom_alloc_handler(accepted_session_data->start_wait_allocator, 
            session_dispatch_binder(&this_type::dispatch_session_start, shared_from_this(), accepted_session_data)));
#else
        accepted_session_data->managed_session->async_start(
          make_custom_alloc_handler(accepted_session_data->start_wait_allocator, 
            boost::bind(&this_type::dispatch_session_start, 
              session_manager_weak_ptr(shared_from_this()), accepted_session_data, _1)));
#endif
        accepted_session_data->state = session_data::start_in_progress;
        // Register pending operation
        ++pending_operations_;
        ++accepted_session_data->pending_operations;        
      }

      void session_manager::stop_session(const session_data_ptr& active_session_data)
      {
        // Asynchronously stop wrapped session
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
        active_session_data->managed_session->async_stop(
          make_custom_alloc_handler(active_session_data->stop_allocator,
            session_dispatch_binder(&this_type::dispatch_session_stop, shared_from_this(), active_session_data)));
#else
        active_session_data->managed_session->async_stop(
          make_custom_alloc_handler(active_session_data->stop_allocator,
            boost::bind(&this_type::dispatch_session_stop, 
              session_manager_weak_ptr(shared_from_this()), active_session_data, _1)));
#endif
        active_session_data->state = session_data::stop_in_progress;
        // Register pending operation
        ++pending_operations_;
        ++active_session_data->pending_operations;        
      }

      void session_manager::wait_session(const session_data_ptr& active_session_data)
      {
        // Asynchronously wait on wrapped session
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
        active_session_data->managed_session->async_wait(
          make_custom_alloc_handler(active_session_data->start_wait_allocator,
            session_dispatch_binder(&this_type::dispatch_session_wait, shared_from_this(), active_session_data)));
#else
        active_session_data->managed_session->async_wait(
          make_custom_alloc_handler(active_session_data->start_wait_allocator,
            boost::bind(&this_type::dispatch_session_wait, 
              session_manager_weak_ptr(shared_from_this()), active_session_data, _1)));
#endif
        // Register pending operation
        ++pending_operations_;
        ++active_session_data->pending_operations;
      }

      void session_manager::dispatch_session_start(const session_manager_weak_ptr& this_weak_ptr,
        const session_data_ptr& handled_session_data, const boost::system::error_code& error)
      {
        // Try to lock the manager
        if (session_manager_ptr this_ptr = this_weak_ptr.lock())
        {
          // Forward invocation
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
          this_ptr->strand_.dispatch(
            make_custom_alloc_handler(handled_session_data->start_wait_allocator,
              session_handler_binder(&this_type::handle_session_start, this_ptr, handled_session_data, error)));
#else
          this_ptr->strand_.dispatch(
            make_custom_alloc_handler(handled_session_data->start_wait_allocator,
              boost::bind(&this_type::handle_session_start, this_ptr, handled_session_data, error)));
#endif
        }
      }

      void session_manager::handle_session_start(const session_data_ptr& handled_session_data, 
        const boost::system::error_code& error)
      {
        // Unregister pending operation
        --pending_operations_;
        --handled_session_data->pending_operations;
        // Check if handler is not called too late
        if (session_data::start_in_progress == handled_session_data->state)
        {
          if (error) 
          {
            handled_session_data->state = session_data::stopped;
            active_sessions_.erase(handled_session_data);
            recycle_session(handled_session_data);
            // Check for pending session manager stop operation 
            if (stop_in_progress == state_)
            {
              if (may_complete_stop())  
              {
                complete_stop();
                post_stop_handler();
              }            
              return;
            }
            if (wait_handler_.has_target() && may_complete_wait()) 
            {
              wait_handler_.post(wait_error_);            
            }
            // Continue session acceptation if can
            if (may_continue_accept())
            {                
              accept_new_session();
            }                            
            return;
          }
          handled_session_data->state = session_data::started;
          // Check for pending session manager stop operation 
          if (stop_in_progress == state_)  
          {                            
            stop_session(handled_session_data);
            return;
          }
          // Wait until session needs to stop
          wait_session(handled_session_data);          
          return;
        }
        // Handler is called too late - complete handler's waiters
        recycle_session(handled_session_data);
        // Check for pending session manager stop operation 
        if (stop_in_progress == state_) 
        {
          if (may_complete_stop())  
          {
            complete_stop();
            post_stop_handler();
          }
        }        
      }

      void session_manager::dispatch_session_wait(const session_manager_weak_ptr& this_weak_ptr,
        const session_data_ptr& handled_session_data, const boost::system::error_code& error)
      {
        // Try to lock the manager
        if (session_manager_ptr this_ptr = this_weak_ptr.lock())
        {
          // Forward invocation
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
          this_ptr->strand_.dispatch(
            make_custom_alloc_handler(handled_session_data->start_wait_allocator,
              session_handler_binder(&this_type::handle_session_wait, this_ptr, handled_session_data, error)));
#else
          this_ptr->strand_.dispatch(
            make_custom_alloc_handler(handled_session_data->start_wait_allocator,
              boost::bind(&this_type::handle_session_wait, this_ptr, handled_session_data, error)));
#endif
        }
      }

      void session_manager::handle_session_wait(const session_data_ptr& handled_session_data, 
        const boost::system::error_code& /*error*/)
      {
        // Unregister pending operation
        --pending_operations_;
        --handled_session_data->pending_operations;
        // Check if handler is not called too late
        if (session_data::started == handled_session_data->state)
        {
          stop_session(handled_session_data);
          return;
        }
        // Handler is called too late - complete handler's waiters
        recycle_session(handled_session_data);
        // Check for pending session manager stop operation
        if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            complete_stop();
            post_stop_handler();
          }          
        }        
      }

      void session_manager::dispatch_session_stop(const session_manager_weak_ptr& this_weak_ptr,
        const session_data_ptr& handled_session_data, const boost::system::error_code& error)
      {     
        // Try to lock the manager
        if (session_manager_ptr this_ptr = this_weak_ptr.lock())
        {
          // Forward invocation
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
          this_ptr->strand_.dispatch(
            make_custom_alloc_handler(handled_session_data->stop_allocator,
              session_handler_binder(&this_type::handle_session_stop, this_ptr, handled_session_data, error)));
#else
          this_ptr->strand_.dispatch(
            make_custom_alloc_handler(handled_session_data->stop_allocator,
              boost::bind(&this_type::handle_session_stop, this_ptr, handled_session_data, error)));
#endif
        }
      }

      void session_manager::handle_session_stop(const session_data_ptr& handled_session_data,
        const boost::system::error_code& /*error*/)
      {
        // Unregister pending operation
        --pending_operations_;
        --handled_session_data->pending_operations;
        // Check if handler is not called too late
        if (session_data::stop_in_progress == handled_session_data->state)        
        {
          handled_session_data->state = session_data::stopped;
          active_sessions_.erase(handled_session_data);
          recycle_session(handled_session_data);
          // Check for pending session manager stop operation
          if (stop_in_progress == state_)
          {
            if (may_complete_stop())  
            {
              complete_stop();
              post_stop_handler();
            }
            return;
          }
          if (wait_handler_.has_target() && may_complete_wait()) 
          {
            wait_handler_.post(wait_error_);
          }
          // Continue session acceptation if can
          if (may_continue_accept())
          {                
            accept_new_session();
          }
          return;
        }
        // Handler is called too late - complete handler's waiters
        recycle_session(handled_session_data);
        // Check for pending session manager stop operation
        if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            complete_stop();
            post_stop_handler();
          }          
        }                
      }

      void session_manager::recycle_session(const session_data_ptr& recycled_session_data)
      {
        // Check session's pending operation number and recycle bin size
        if (0 == recycled_session_data->pending_operations
          && recycled_sessions_.size() < recycled_session_count_)
        {
          // Reset session state
          recycled_session_data->managed_session->reset();
          // Reset wrapper
          recycled_session_data->state = session_data::ready_to_start;
          // Add to recycle bin
          recycled_sessions_.push_front(recycled_session_data);
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

