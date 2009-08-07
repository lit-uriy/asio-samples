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
      struct session_proxy_type;
      typedef boost::shared_ptr<session_proxy_type> session_proxy_ptr;
      typedef boost::weak_ptr<session_proxy_type> session_proxy_weak_ptr;

      enum session_lifecycle
      {
        none,
        start_in_progress,
        started,
        stop_in_progress,
        stopped
      };
      
      struct session_proxy_type : private boost::noncopyable
      {        
        session_proxy_weak_ptr prev;
        session_proxy_ptr next;
        session_ptr session;
        handler_allocator start_wait_allocator_;
        handler_allocator stop_allocator_;
        boost::asio::ip::tcp::endpoint endpoint;
        session_lifecycle lifecycle;        

        explicit session_proxy_type(boost::asio::io_service& io_service)
          : session(new ma::echo::session(io_service))
          , lifecycle(none)
        {
        }

        ~session_proxy_type()
        {
        }
      }; // wrapped_session

      class session_proxy_list : private boost::noncopyable
      {
      public:
        explicit session_proxy_list()
          : size_(0)
        {
        }

        void push_front(const session_proxy_ptr& session_proxy)
        {
          session_proxy->next = front_;
          session_proxy->prev.reset();
          if (front_)
          {
            front_->prev = session_proxy;
          }
          front_ = session_proxy;
          ++size_;
        }

        void erase(const session_proxy_ptr& session_proxy)
        {
          if (front_ == session_proxy)
          {
            front_ = front_->next;
          }
          session_proxy_ptr prev = session_proxy->prev.lock();
          if (prev)
          {
            prev->next = session_proxy->next;
          }
          if (session_proxy->next)
          {
            session_proxy->next->prev = prev;
          }
          session_proxy->prev.reset();
          session_proxy->next.reset();
          --size_;
        }

        std::size_t size() const
        {
          return size_;
        }

        bool empty() const
        {
          return front_;
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
        boost::asio::ip::tcp::endpoint endpoint;
        std::size_t max_sessions;
        int backlog;

        explicit settings(
          const boost::asio::ip::tcp::endpoint& listen_endpoint,
          std::size_t max_sessions_number = (std::numeric_limits<std::size_t>::max)(),
          int listen_backlog = 4)
          : endpoint(listen_endpoint)
          , max_sessions(max_sessions_number)
          , backlog(listen_backlog)
        {
        }
      }; // struct settings

      explicit server(boost::asio::io_service& io_service,
        boost::asio::io_service& session_io_service,
        const settings& settings)
        : io_service_(io_service)
        , session_io_service_(session_io_service)
        , strand_(io_service)
        , acceptor_(io_service)
        , wait_handler_(io_service)
        , stop_handler_(io_service)
        , started_(false)        
        , stopped_(false)      
        , accept_in_progress_(false)
        , settings_(settings)
      {
        if (settings.max_sessions < 1)
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
        if (stopped_ || stop_handler_.has_target())
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
        else if (started_)
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
          boost::system::error_code error;
          acceptor_.open(settings_.endpoint.protocol(), error);
          if (!error)
          {
            acceptor_.bind(settings_.endpoint, error);
            if (!error)
            {
              acceptor_.listen(settings_.backlog, error);
            }          
          }          
          if (error)
          {
            boost::system::error_code ignored;
            acceptor_.close(ignored);
          }
          else
          {
            started_ = true;
            accept_new_session();            
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
        //todo
        io_service_.post
        (
          boost::asio::detail::bind_handler
          (
            boost::get<0>(handler), 
            boost::asio::error::operation_not_supported
          )
        );
      } // do_stop

      template <typename Handler>
      void do_wait(boost::tuple<Handler> handler)
      {
        //todo
        io_service_.post
        (
          boost::asio::detail::bind_handler
          (
            boost::get<0>(handler), 
            boost::asio::error::operation_not_supported
          )
        );
      } // do_wait

      void accept_new_session()
      {
        session_proxy_ptr session_proxy(
          new session_proxy_type(session_io_service_));

        acceptor_.async_accept
        (
          session_proxy->session->socket(),
          session_proxy->endpoint, 
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
        accept_in_progress_ = true;
      }      

      void handle_accept(const session_proxy_ptr& session_proxy,
        const boost::system::error_code& error)
      {
        accept_in_progress_ = false;
        if (stop_handler_.has_target())
        {
          if (session_proxies_.empty())  
          {
            stopped_ = true;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else if (error)
        {          
          if (wait_handler_.has_target()) 
          {
            wait_handler_.post(error);
          }
          else
          {
            wait_error_ = error;
          }          
        }
        else
        { 
          // Save session
          session_proxies_.push_front(session_proxy);
          // Continue session acceptation
          if (session_proxies_.size() < settings_.max_sessions)
          {
            accept_new_session();
          }
          // Start accepted session
          session_proxy->lifecycle = start_in_progress;          
          session_proxy->session->async_start
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
        }        
      }

      static void dispatch_session_start(const server_weak_ptr& weak_server,
        const session_proxy_ptr& session_proxy, const boost::system::error_code& error)
      {
        server_ptr the_server(weak_server.lock());
        if (the_server)
        {
          the_server->strand_.dispatch
          (
            make_custom_alloc_handler
            (
              session_proxy->start_wait_allocator_,
              boost::bind
              (
                &this_type::handle_session_start,
                the_server,
                session_proxy,
                error
              )
            )
          );
        }
      }

      void handle_session_start(const session_proxy_ptr& session_proxy,
        const boost::system::error_code& error)
      {        
        if (start_in_progress == session_proxy->lifecycle)
        {        
          if (error)
          {          
            session_proxy->lifecycle = stopped;
            session_proxies_.erase(session_proxy);
            if (stop_handler_.has_target())
            {
              if (!accept_in_progress_ && session_proxies_.empty())  
              {
                stopped_ = true;
                // Signal shutdown completion
                stop_handler_.post(stop_error_);
              }
            }
            else
            {
              if (!accept_in_progress_ && !wait_error_
                && session_proxies_.size() < settings_.max_sessions)
              {
                accept_new_session();
              }
            }         
          }
          else
          {            
            session_proxy->lifecycle = started;
            if (stop_handler_.has_target())  
            {
              session_proxy->lifecycle = stop_in_progress;
              //todo
              //session->session->async_stop(...);
            }
            else
            {
              //todo
              //session->session->async_wait(...);
            }            
          }
        }
      }

      boost::asio::io_service& io_service_;
      boost::asio::io_service& session_io_service_;
      boost::asio::io_service::strand strand_;      
      boost::asio::ip::tcp::acceptor acceptor_;
      ma::handler_storage<boost::system::error_code> wait_handler_;
      ma::handler_storage<boost::system::error_code> stop_handler_;
      bool started_;      
      bool stopped_;      
      bool accept_in_progress_;      
      handler_allocator accept_allocator_;
      session_proxy_list session_proxies_;
      settings settings_;
      boost::system::error_code wait_error_;
      boost::system::error_code stop_error_;
    }; // class server

  } // namespace echo
} // namespace ma

#endif // MA_ECHO_SERVER_HPP
