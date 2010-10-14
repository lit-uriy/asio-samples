//
// Copyright (c) 2010 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <stdexcept>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/throw_exception.hpp>
#include <boost/make_shared.hpp>
#include <ma/handler_allocation.hpp>
#include <ma/echo/server3/session_proxy.h>
#include <ma/echo/server3/session_manager_handler.h>
#include <ma/echo/server3/session_manager.h>

namespace ma
{    
  namespace echo
  {
    namespace server3
    {
      session_manager::config::config(const boost::asio::ip::tcp::endpoint& endpoint,
        std::size_t max_sessions, std::size_t recycled_sessions,
        int listen_backlog, const session::config& session_config)
        : listen_backlog_(listen_backlog)
        , max_sessions_(max_sessions)
        , recycled_sessions_(recycled_sessions)
        , endpoint_(endpoint)                
        , session_config_(session_config)
      {
        if (1 > max_sessions_)
        {
          boost::throw_exception(std::invalid_argument("maximum sessions number must be >= 1"));
        }
      } // session_manager::config::config

      session_manager::session_manager(boost::asio::io_service& io_service,
        boost::asio::io_service& session_io_service,
        const config& config)
        : accept_in_progress_(false)
        , state_(ready_to_start)
        , pending_operations_(0)
        , strand_(io_service)
        , acceptor_(io_service)
        , session_io_service_(session_io_service)                        
        , config_(config)        
      {          
      } // session_manager::session_manager
      
      void session_manager::async_start(const allocator_ptr& operation_allocator,
        const session_manager_start_handler_weak_ptr& handler)
      {
        strand_.post(make_custom_alloc_handler(*operation_allocator, 
          boost::bind(&this_type::do_start, shared_from_this(), operation_allocator, handler)));
      } // session_manager::async_start
      
      void session_manager::async_stop(const allocator_ptr& operation_allocator,
        const session_manager_stop_handler_weak_ptr& handler)
      {
        strand_.post(make_custom_alloc_handler(*operation_allocator, 
          boost::bind(&this_type::do_stop, shared_from_this(), operation_allocator, handler)));
      } // session_manager::async_stop
      
      void session_manager::async_wait(const allocator_ptr& operation_allocator,
        const session_manager_wait_handler_weak_ptr& handler)
      {
        strand_.post(make_custom_alloc_handler(*operation_allocator, 
          boost::bind(&this_type::do_wait, shared_from_this(), operation_allocator, handler)));
      } // session_manager::async_wait      

      void session_manager::do_start(const allocator_ptr& operation_allocator,
        const session_manager_start_handler_weak_ptr& handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          session_manager_start_handler::invoke(handler, operation_allocator, 
            boost::asio::error::operation_aborted);
        } 
        else if (ready_to_start != state_)
        {          
          session_manager_start_handler::invoke(handler, operation_allocator, 
            boost::asio::error::operation_not_supported);
        }
        else
        {
          state_ = start_in_progress;
          boost::system::error_code error;
          acceptor_.open(config_.endpoint_.protocol(), error);
          if (!error)
          {
            acceptor_.bind(config_.endpoint_, error);
            if (!error)
            {
              acceptor_.listen(config_.listen_backlog_, error);
            }          
          }          
          if (error)
          {
            boost::system::error_code ignored;
            acceptor_.close(ignored);
            state_ = stopped;
          }
          else
          {            
            accept_new_session();            
            state_ = started;
          }
          session_manager_start_handler::invoke(handler, operation_allocator, error);
        }        
      } // session_manager::do_start
      
      void session_manager::do_stop(const allocator_ptr& operation_allocator,
        const session_manager_stop_handler_weak_ptr& handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          session_manager_stop_handler::invoke(handler, operation_allocator, 
            boost::asio::error::operation_aborted);
        }
        else
        {
          // Start shutdown
          state_ = stop_in_progress;

          // Do shutdown - abort inner operations          
          acceptor_.close(stop_error_); 

          // Start stop for all active sessions
          session_proxy_ptr curr_session_proxy(active_session_proxies_.front());
          while (curr_session_proxy)
          {
            if (stop_in_progress != curr_session_proxy->state_)
            {
              stop_session(curr_session_proxy);
            }
            curr_session_proxy = curr_session_proxy->next_;
          }
          
          // Do shutdown - abort outer operations
          if (has_wait_handler())
          {
            invoke_wait_handler(boost::asio::error::operation_aborted);            
          }

          // Check for shutdown continuation
          if (may_complete_stop())
          {
            state_ = stopped;          
            // Signal shutdown completion
            session_manager_stop_handler::invoke(handler, operation_allocator, stop_error_);
          }
          else
          { 
            stop_handler_.first = handler;
            stop_handler_.second = operation_allocator;
          }
        }
      } // session_manager::do_stop
      
      void session_manager::do_wait(const allocator_ptr& operation_allocator,
        const session_manager_wait_handler_weak_ptr& handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          session_manager_wait_handler::invoke(handler, operation_allocator, 
            boost::asio::error::operation_aborted);
        } 
        else if (started != state_)
        {          
          session_manager_wait_handler::invoke(handler, operation_allocator, 
            boost::asio::error::operation_not_supported);
        }
        else if (last_accept_error_ && active_session_proxies_.empty())
        {
          session_manager_wait_handler::invoke(handler, operation_allocator, last_accept_error_);          
        }
        else if (has_wait_handler())
        {
          session_manager_wait_handler::invoke(handler, operation_allocator, 
            boost::asio::error::operation_not_supported);          
        }
        else
        {          
          wait_handler_.first = handler;
          wait_handler_.second = operation_allocator;          
        }  
      } // session_manager::do_wait

      void session_manager::accept_new_session()
      {
        // Get new ready to start session
        session_proxy_ptr proxy;
        if (recycled_session_proxies_.empty())
        {
          proxy = boost::make_shared<session_proxy>(
            boost::ref(session_io_service_), 
            boost::weak_ptr<this_type>(shared_from_this()),
            config_.session_config_);
        }
        else
        {
          proxy = recycled_session_proxies_.front();
          recycled_session_proxies_.erase(proxy);
        }
        // Start session acceptation
        acceptor_.async_accept
        (
          proxy->session_->socket(),
          proxy->endpoint_, 
          strand_.wrap
          (
            make_custom_alloc_handler
            (
              accept_allocator_,
              boost::bind
              (
                &this_type::handle_accept,
                shared_from_this(),
                proxy,
                boost::asio::placeholders::error
              )
            )
          )
        );
        ++pending_operations_;
        accept_in_progress_ = true;
      } // session_manager::accept_new_session

      void session_manager::handle_accept(const session_proxy_ptr& proxy, const boost::system::error_code& error)
      {
        --pending_operations_;
        accept_in_progress_ = false;
        if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            invoke_stop_handler(stop_error_);            
          }
        }
        else if (error)
        {   
          last_accept_error_ = error;
          if (active_session_proxies_.empty()) 
          {
            // Server can't work more time
            if (has_wait_handler())
            {
              invoke_wait_handler(error);
            }
          }
        }
        else if (active_session_proxies_.size() < config_.max_sessions_)
        { 
          // Start accepted session 
          start_session(proxy);          
          // Save session as active
          active_session_proxies_.push_front(proxy);
          // Continue session acceptation if can
          if (active_session_proxies_.size() < config_.max_sessions_)
          {
            accept_new_session();
          }          
        }
        else
        {
          recycle_session(proxy);
        }
      } // session_manager::handle_accept        

      bool session_manager::may_complete_stop() const
      {
        return 0 == pending_operations_ && active_session_proxies_.empty();
      } // session_manager::may_complete_stop

      void session_manager::start_session(const session_proxy_ptr& proxy)
      {        
        proxy->session_->async_start
        (
          proxy->start_wait_allocator_,
          proxy
        );          
        proxy->state_ = session_proxy::start_in_progress;
        ++proxy->pending_operations_;
        ++pending_operations_;        
      } // session_manager::start_session

      void session_manager::stop_session(const session_proxy_ptr& proxy)
      {
        proxy->session_->async_stop
        (
          proxy->stop_allocator_,
          proxy
        );
        proxy->state_ = session_proxy::stop_in_progress;
        ++proxy->pending_operations_;
        ++pending_operations_;
      } // session_manager::stop_session

      void session_manager::wait_session(const session_proxy_ptr& proxy)
      {
        proxy->session_->async_wait
        (
          proxy->start_wait_allocator_,
          proxy
        );
        ++proxy->pending_operations_;
        ++pending_operations_;        
      } // session_manager::wait_session      

      void session_manager::handle_session_start(const session_proxy_ptr& proxy,
        const allocator_ptr& /*operation_allocator*/, const boost::system::error_code& error)
      {
        --pending_operations_;
        --proxy->pending_operations_;
        if (session_proxy::start_in_progress == proxy->state_)
        {          
          if (error)
          {
            proxy->state_ = session_proxy::stopped;
            active_session_proxies_.erase(proxy);            
            if (stop_in_progress == state_)
            {
              if (may_complete_stop())  
              {
                state_ = stopped;
                // Signal shutdown completion
                invoke_stop_handler(stop_error_);
              }
            }
            else if (last_accept_error_ && active_session_proxies_.empty()) 
            {
              if (has_wait_handler())
              {
                invoke_wait_handler(last_accept_error_);
              }              
            }
            else
            {
              recycle_session(proxy);
              // Continue session acceptation if can
              if (!accept_in_progress_ && !last_accept_error_
                && active_session_proxies_.size() < config_.max_sessions_)
              {                
                accept_new_session();
              }                
            }
          }
          else // !error
          {
            proxy->state_ = session_proxy::started;
            if (stop_in_progress == state_)  
            {                            
              stop_session(proxy);
            }
            else
            { 
              // Wait until session needs to stop
              wait_session(proxy);
            }            
          }  
        } 
        else if (stop_in_progress == state_) 
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            invoke_stop_handler(stop_error_);
          }
        }
        else
        {
          recycle_session(proxy);
        }        
      } // session_manager::handle_session_start      

      void session_manager::handle_session_wait(const session_proxy_ptr& proxy,
        const allocator_ptr& /*operation_allocator*/, const boost::system::error_code& /*error*/)
      {
        --pending_operations_;
        --proxy->pending_operations_;
        if (session_proxy::started == proxy->state_)
        {
          stop_session(proxy);
        }
        else if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            invoke_stop_handler(stop_error_);
          }
        }
        else
        {
          recycle_session(proxy);
        }
      } // session_manager::handle_session_wait      

      void session_manager::handle_session_stop(const session_proxy_ptr& proxy,
        const allocator_ptr& /*operation_allocator*/, const boost::system::error_code& /*error*/)
      {
        --pending_operations_;
        --proxy->pending_operations_;
        if (session_proxy::stop_in_progress == proxy->state_)        
        {
          proxy->state_ = session_proxy::stopped;
          active_session_proxies_.erase(proxy);            
          if (stop_in_progress == state_)
          {
            if (may_complete_stop())  
            {
              state_ = stopped;
              // Signal shutdown completion
              invoke_stop_handler(stop_error_);
            }
          }
          else if (last_accept_error_ && active_session_proxies_.empty()) 
          {
            if (has_wait_handler())
            {
              invoke_wait_handler(last_accept_error_);              
            }
          }
          else
          {
            recycle_session(proxy);
            // Continue session acceptation if can
            if (!accept_in_progress_ && !last_accept_error_
              && active_session_proxies_.size() < config_.max_sessions_)
            {                
              accept_new_session();
            }              
          }
        }
        else if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            invoke_stop_handler(stop_error_);
          }
        }
        else
        {
          recycle_session(proxy);
        }
      } // session_manager::handle_session_stop

      void session_manager::recycle_session(const session_proxy_ptr& proxy)
      {
        if (0 == proxy->pending_operations_
          && recycled_session_proxies_.size() < config_.recycled_sessions_)
        {
          proxy->session_->reset();
          proxy->state_ = session_proxy::ready_to_start;
          recycled_session_proxies_.push_front(proxy);
        }        
      } // session_manager::recycle_session

      bool session_manager::has_wait_handler() const
      {
        return wait_handler_.second;
      } // session_manager::has_wait_handler

      void session_manager::invoke_wait_handler(const boost::system::error_code& error)
      {
        session_manager_wait_handler::invoke(wait_handler_.first, wait_handler_.second, error);
        wait_handler_storage().swap(wait_handler_);
      } // session_manager::invoke_wait_handler

      void session_manager::invoke_stop_handler(const boost::system::error_code& error)
      {
        session_manager_stop_handler::invoke(stop_handler_.first, stop_handler_.second, error);                   
        stop_handler_storage().swap(stop_handler_);
      } // session_manager::invoke_stop_handler

    } // namespace server3
  } // namespace echo
} // namespace ma