//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_ECHO_SERVER_HPP
#define MA_ECHO_SERVER_HPP

#include <limits>
#include <stdexcept>
#include <boost/utility.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/asio.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/circular_buffer.hpp>
#include <ma/handler_allocation.hpp>
#include <ma/handler_storage.hpp>
#include <ma/echo/session.hpp>

namespace ma
{    
  namespace echo
  {    
    class server;
    typedef boost::shared_ptr<server> server_ptr;    
    typedef boost::weak_ptr<server> server_weak_ptr;    

    class server 
      : private boost::noncopyable 
      , public boost::enable_shared_from_this<server>
    {
    private:
      typedef server this_type;      
      enum state_type
      {
        ready_to_start,
        start_in_progress,
        started,
        stop_in_progress,
        stopped
      };
      struct session_proxy_type;
      typedef boost::shared_ptr<session_proxy_type> session_proxy_ptr;
      typedef boost::weak_ptr<session_proxy_type> session_proxy_weak_ptr;      
      
      struct session_proxy_type : private boost::noncopyable
      {        
        session_proxy_weak_ptr prev_;
        session_proxy_ptr next_;
        session_ptr session_;        
        boost::asio::ip::tcp::endpoint endpoint_;        
        std::size_t pending_operations_;
        state_type state_;        
        in_place_handler_allocator<256> start_wait_allocator_;
        in_place_handler_allocator<256> stop_allocator_;

        explicit session_proxy_type(boost::asio::io_service& io_service,
          const session::settings& session_settings)
          : session_(new ma::echo::session(io_service, session_settings))
          , pending_operations_(0)
          , state_(ready_to_start)
        {
        }

        ~session_proxy_type()
        {
        }
      }; // session_proxy_type

      class session_proxy_list : private boost::noncopyable
      {
      public:
        explicit session_proxy_list()
          : size_(0)
        {
        }

        void push_front(const session_proxy_ptr& session_proxy)
        {
          session_proxy->next_ = front_;
          session_proxy->prev_.reset();
          if (front_)
          {
            front_->prev_ = session_proxy;
          }
          front_ = session_proxy;
          ++size_;
        }

        void erase(const session_proxy_ptr& session_proxy)
        {
          if (front_ == session_proxy)
          {
            front_ = front_->next_;
          }
          session_proxy_ptr prev = session_proxy->prev_.lock();
          if (prev)
          {
            prev->next_ = session_proxy->next_;
          }
          if (session_proxy->next_)
          {
            session_proxy->next_->prev_ = prev;
          }
          session_proxy->prev_.reset();
          session_proxy->next_.reset();
          --size_;
        }

        std::size_t size() const
        {
          return size_;
        }

        bool empty() const
        {
          return 0 == size_;
        }

        session_proxy_ptr front() const
        {
          return front_;
        }

      private:
        std::size_t size_;
        session_proxy_ptr front_;
      }; // session_proxy_list

    public:
      struct settings
      {      
        boost::asio::ip::tcp::endpoint endpoint_;
        std::size_t max_sessions_;
        std::size_t recycled_sessions_;
        int listen_backlog_;
        session::settings session_settings_;

        explicit settings(
          const boost::asio::ip::tcp::endpoint& endpoint,
          std::size_t max_sessions,
          std::size_t recycled_sessions,
          int listen_backlog,
          const session::settings& session_settings)
          : endpoint_(endpoint)
          , max_sessions_(max_sessions)
          , recycled_sessions_(recycled_sessions)
          , listen_backlog_(listen_backlog)
          , session_settings_(session_settings)
        {
        }
      }; // struct settings

      explicit server(boost::asio::io_service& io_service,
        boost::asio::io_service& session_io_service,
        const settings& settings)
        : io_service_(io_service)
        , strand_(io_service)
        , acceptor_(io_service)
        , session_io_service_(session_io_service)                
        , wait_handler_(io_service)
        , stop_handler_(io_service)
        , settings_(settings)
        , pending_operations_(0)
        , state_(ready_to_start)
        , accept_in_progress_(false) 
        //, accept_allocator_(1024, sizeof(std::size_t))
      {
        if (settings.max_sessions_ < 1)
        {
          boost::throw_exception(std::runtime_error("maximum sessions number must be >= 1"));
        }
      }

      ~server()
      {        
      }

      template <typename Handler>
      void async_start(Handler handler)
      {
        strand_.dispatch
        (
          ma::make_context_alloc_handler
          (
            handler, 
            boost::bind
            (
              &this_type::do_start<Handler>,
              shared_from_this(),              
              boost::make_tuple(handler)
            )
          )
        );  
      } // async_start

      template <typename Handler>
      void async_stop(Handler handler)
      {
        strand_.dispatch
        (
          ma::make_context_alloc_handler
          (
            handler, 
            boost::bind
            (
              &this_type::do_stop<Handler>,
              shared_from_this(),
              boost::make_tuple(handler)
            )
          )
        ); 
      } // async_stop

      template <typename Handler>
      void async_wait(Handler handler)
      {
        strand_.dispatch
        (
          ma::make_context_alloc_handler
          (
            handler, 
            boost::bind
            (
              &this_type::do_wait<Handler>,
              shared_from_this(),
              boost::make_tuple(handler)
            )
          )
        );  
      } // async_wait

    private:
      template <typename Handler>
      void do_start(boost::tuple<Handler> handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::asio::error::operation_aborted
            )
          );          
        } 
        else if (ready_to_start != state_)
        {          
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::asio::error::operation_not_supported
            )
          );          
        }
        else
        {
          state_ = start_in_progress;
          boost::system::error_code error;
          acceptor_.open(settings_.endpoint_.protocol(), error);
          if (!error)
          {
            acceptor_.bind(settings_.endpoint_, error);
            if (!error)
            {
              acceptor_.listen(settings_.listen_backlog_, error);
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
            accept_session();            
            state_ = started;
          }
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              error
            )
          );                        
        }        
      } // do_start

      template <typename Handler>
      void do_stop(boost::tuple<Handler> handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::asio::error::operation_aborted
            )
          );          
        }
        else
        {
          // Start shutdown
          state_ = stop_in_progress;

          // Do shutdown - abort inner operations          
          acceptor_.close(stop_error_); 

          // Start stop for all active sessions
          session_proxy_ptr session_proxy(active_session_proxies_.front());
          while (session_proxy)
          {
            if (stop_in_progress != session_proxy->state_)
            {
              stop_session(session_proxy);
            }
            session_proxy = session_proxy->next_;
          }
          
          // Do shutdown - abort outer operations
          wait_handler_.cancel();

          // Check for shutdown continuation
          if (may_complete_stop())
          {
            state_ = stopped;          
            // Signal shutdown completion
            io_service_.post
            (
              boost::asio::detail::bind_handler
              (
                boost::get<0>(handler), 
                stop_error_
              )
            );
          }
          else
          { 
            stop_handler_.store(
              boost::asio::error::operation_aborted,                        
              boost::get<0>(handler));            
          }
        }
      } // do_stop

      template <typename Handler>
      void do_wait(boost::tuple<Handler> handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::asio::error::operation_aborted
            )
          );          
        } 
        else if (started != state_)
        {          
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::asio::error::operation_not_supported
            )
          );          
        }
        else if (last_accept_error_)
        {
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              last_accept_error_
            )
          );
        }
        else
        {          
          wait_handler_.store(
            boost::asio::error::operation_aborted,                        
            boost::get<0>(handler));
        }  
      } // do_wait

      void accept_session()
      {
        // Get new ready to start session
        session_proxy_ptr session_proxy;
        if (recycled_session_proxies_.empty())
        {
          session_proxy.reset(
            new session_proxy_type(session_io_service_, settings_.session_settings_));
        }
        else
        {
          session_proxy = recycled_session_proxies_.front();
          recycled_session_proxies_.erase(session_proxy);
        }
        // Start session acceptation
        acceptor_.async_accept
        (
          session_proxy->session_->socket(),
          session_proxy->endpoint_, 
          strand_.wrap
          (
            make_custom_alloc_handler
            (
              accept_allocator_,
              boost::bind
              (
                &this_type::handle_accept,
                shared_from_this(),
                session_proxy,
                _1
              )
            )
          )
        );
        ++pending_operations_;
        accept_in_progress_ = true;
      } // accept_session

      void handle_accept(const session_proxy_ptr& session_proxy,
        const boost::system::error_code& error)
      {
        --pending_operations_;
        accept_in_progress_ = false;
        if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else if (error)
        {   
          last_accept_error_ = error;
          if (active_session_proxies_.empty()) 
          {
            // Server can't work more time
            wait_handler_.post(error);
          }
        }
        else
        { 
          // Start accepted session 
          start_session(session_proxy);          
          // Save session as active
          active_session_proxies_.push_front(session_proxy);
          // Continue session acceptation if can
          if (may_accept_more())
          {
            accept_session();
          }          
        }        
      } // handle_accept

      bool may_accept_more() const
      {
        return !accept_in_progress_ && !last_accept_error_
          && active_session_proxies_.size() < settings_.max_sessions_;
      }

      bool may_complete_stop() const
      {
        return 0 == pending_operations_ && active_session_proxies_.empty();
      }      

      void start_session(const session_proxy_ptr& session_proxy)
      {        
        session_proxy->session_->async_start
        (
          make_custom_alloc_handler
          (
            session_proxy->start_wait_allocator_,
            boost::bind
            (
              &this_type::dispatch_session_start,
              server_weak_ptr(shared_from_this()),
              session_proxy,
              _1
            )
          )           
        );  
        ++pending_operations_;
        ++session_proxy->pending_operations_;
        session_proxy->state_ = start_in_progress;
      } // start_session

      void stop_session(const session_proxy_ptr& session_proxy)
      {
        session_proxy->session_->async_stop
        (
          make_custom_alloc_handler
          (
            session_proxy->stop_allocator_,
            boost::bind
            (
              &this_type::dispatch_session_stop,
              server_weak_ptr(shared_from_this()),
              session_proxy,
              _1
            )
          )
        );
        ++pending_operations_;
        ++session_proxy->pending_operations_;
        session_proxy->state_ = stop_in_progress;
      } // stop_session

      void wait_session(const session_proxy_ptr& session_proxy)
      {
        session_proxy->session_->async_wait
        (
          make_custom_alloc_handler
          (
            session_proxy->start_wait_allocator_,
            boost::bind
            (
              &this_type::dispatch_session_wait,
              server_weak_ptr(shared_from_this()),
              session_proxy,
              _1
            )
          )
        );
        ++pending_operations_;
        ++session_proxy->pending_operations_;
      } // wait_session

      static void dispatch_session_start(const server_weak_ptr& weak_server,
        const session_proxy_ptr& session_proxy, const boost::system::error_code& error)
      {
        server_ptr this_server(weak_server.lock());
        if (this_server)
        {
          this_server->strand_.dispatch
          (
            make_custom_alloc_handler
            (
              session_proxy->start_wait_allocator_,
              boost::bind
              (
                &this_type::handle_session_start,
                this_server,
                session_proxy,
                error
              )
            )
          );
        }
      } // dispatch_session_start

      void handle_session_start(const session_proxy_ptr& session_proxy,
        const boost::system::error_code& error)
      {
        --pending_operations_;
        --session_proxy->pending_operations_;
        if (start_in_progress == session_proxy->state_)
        {          
          if (error)
          {
            session_proxy->state_ = stopped;
            active_session_proxies_.erase(session_proxy);            
            if (stop_in_progress == state_)
            {
              if (may_complete_stop())  
              {
                state_ = stopped;
                // Signal shutdown completion
                stop_handler_.post(stop_error_);
              }
            }
            else 
            {
              recycle_session(session_proxy);
              // Continue session acceptation if can
              if (may_accept_more())
              {                
                accept_session();
              } 
            }
          }
          else // !error
          {
            session_proxy->state_ = started;
            if (stop_in_progress == state_)  
            {                            
              stop_session(session_proxy);
            }
            else
            { 
              // Wait until session needs to stop
              wait_session(session_proxy);
            }            
          }  
        } 
        else if (stop_in_progress == state_) 
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else
        {
          recycle_session(session_proxy);
        }        
      } // handle_session_start

      static void dispatch_session_wait(const server_weak_ptr& weak_server,
        const session_proxy_ptr& session_proxy, const boost::system::error_code& error)
      {
        server_ptr this_server(weak_server.lock());
        if (this_server)
        {
          this_server->strand_.dispatch
          (
            make_custom_alloc_handler
            (
              session_proxy->start_wait_allocator_,
              boost::bind
              (
                &this_type::handle_session_wait,
                this_server,
                session_proxy,
                error
              )
            )
          );
        }
      } // dispatch_session_wait

      void handle_session_wait(const session_proxy_ptr& session_proxy,
        const boost::system::error_code& /*error*/)
      {
        --pending_operations_;
        --session_proxy->pending_operations_;
        if (started == session_proxy->state_)
        {
          stop_session(session_proxy);
        }
        else if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else
        {
          recycle_session(session_proxy);
        }
      } // handle_session_wait

      static void dispatch_session_stop(const server_weak_ptr& weak_server,
        const session_proxy_ptr& session_proxy, const boost::system::error_code& error)
      {
        server_ptr this_server(weak_server.lock());
        if (this_server)
        {
          this_server->strand_.dispatch
          (
            make_custom_alloc_handler
            (
              session_proxy->stop_allocator_,
              boost::bind
              (
                &this_type::handle_session_stop,
                this_server,
                session_proxy,
                error
              )
            )
          );
        }
      } // dispatch_session_stop

      void handle_session_stop(const session_proxy_ptr& session_proxy,
        const boost::system::error_code& /*error*/)
      {
        --pending_operations_;
        --session_proxy->pending_operations_;
        if (stop_in_progress == session_proxy->state_)        
        {
          session_proxy->state_ = stopped;
          active_session_proxies_.erase(session_proxy);            
          if (stop_in_progress == state_)
          {
            if (may_complete_stop())  
            {
              state_ = stopped;
              // Signal shutdown completion
              stop_handler_.post(stop_error_);
            }
          }
          else 
          {
            recycle_session(session_proxy);
            // Continue session acceptation if can
            if (may_accept_more())
            {                
              accept_session();
            } 
          }
        }
        else if (stop_in_progress == state_)
        {
          if (may_complete_stop())  
          {
            state_ = stopped;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else
        {
          recycle_session(session_proxy);
        }
      } // handle_session_stop

      void recycle_session(const session_proxy_ptr& session_proxy)
      {
        if (0 == session_proxy->pending_operations_
          && recycled_session_proxies_.size() < settings_.recycled_sessions_)
        {
          session_proxy->session_->reset();
          session_proxy->state_ = ready_to_start;
          recycled_session_proxies_.push_front(session_proxy);
        }        
      } // recycle_session

      boost::asio::io_service& io_service_;
      boost::asio::io_service::strand strand_;      
      boost::asio::ip::tcp::acceptor acceptor_;
      boost::asio::io_service& session_io_service_;            
      ma::handler_storage<boost::system::error_code> wait_handler_;
      ma::handler_storage<boost::system::error_code> stop_handler_;
      session_proxy_list active_session_proxies_;
      session_proxy_list recycled_session_proxies_;
      boost::system::error_code last_accept_error_;
      boost::system::error_code stop_error_;      
      settings settings_;
      std::size_t pending_operations_;
      state_type state_;
      bool accept_in_progress_;      
      //in_heap_handler_allocator accept_allocator_;
      in_place_handler_allocator<512> accept_allocator_;
    }; // class server

  } // namespace echo
} // namespace ma

#endif // MA_ECHO_SERVER_HPP
