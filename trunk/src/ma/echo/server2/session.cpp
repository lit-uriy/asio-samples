//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <stdexcept>
#include <boost/throw_exception.hpp>
#include <ma/bind_asio_handler.hpp>
#include <ma/echo/server2/session.h>

namespace ma
{    
  namespace echo
  {
    namespace server2
    {
      session::settings::settings(std::size_t buffer_size,
        int socket_recv_buffer_size,
        int socket_send_buffer_size,
        bool no_delay)
        : buffer_size_(buffer_size)         
        , socket_recv_buffer_size_(socket_recv_buffer_size)
        , socket_send_buffer_size_(socket_send_buffer_size)
        , no_delay_(no_delay)
      {
        if (1 > buffer_size)
        {
          boost::throw_exception(std::invalid_argument("too small buffer_size"));
        }
        if (0 > socket_recv_buffer_size)
        {
          boost::throw_exception(std::invalid_argument("socket_recv_buffer_size must be non negative"));
        }
        if (0 > socket_send_buffer_size)
        {
          boost::throw_exception(std::invalid_argument("socket_send_buffer_size must be non negative"));
        }
      } // session::settings::settings

      session::session(boost::asio::io_service& io_service, const settings& settings)
        : io_service_(io_service)
        , strand_(io_service)
        , socket_(io_service)
        , wait_handler_(io_service)
        , stop_handler_(io_service)
        , settings_(settings)
        , state_(ready_to_start)
        , socket_write_in_progress_(false)
        , socket_read_in_progress_(false) 
        , buffer_(settings.buffer_size_)
      {          
      } // session::session

      session::~session()
      {
      } // session::~session

      void session::reset()
      {
        boost::system::error_code ignored;
        socket_.close(ignored);
        error_ = stop_error_ = boost::system::error_code();          
        state_ = ready_to_start;
        buffer_.reset();          
      } // session::reset

      boost::asio::ip::tcp::socket& session::socket()
      {
        return socket_;
      } // session::socket

      void session::async_start(const session_completion::handler& handler)
      {
        strand_.dispatch
        (
          ma::make_context_alloc_handler
          (
            handler, 
            boost::bind
            (
              &this_type::do_start,
              shared_from_this(),
              handler
            )
          )
        );  
      } // session::async_start

      void session::async_stop(const session_completion::handler& handler)
      {
        strand_.dispatch
        (
          ma::make_context_alloc_handler
          (
            handler, 
            boost::bind
            (
              &this_type::do_stop,
              shared_from_this(),
              handler
            )
          )
        ); 
      } // session::async_stop

      void session::async_wait(const session_completion::handler& handler)
      {
        strand_.dispatch
        (
          ma::make_context_alloc_handler
          (
            handler, 
            boost::bind
            (
              &this_type::do_wait,
              shared_from_this(),
              handler
            )
          )
        );  
      } // session::async_wait

      void session::do_start(const session_completion::handler& handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              boost::asio::error::operation_aborted
            )
          );          
        } 
        else if (ready_to_start != state_)
        {          
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              boost::asio::error::operation_not_supported
            )
          );          
        }
        else
        {
          boost::system::error_code start_error;
          using boost::asio::ip::tcp;
          socket_.set_option(tcp::socket::receive_buffer_size(settings_.socket_recv_buffer_size_), start_error);
          if (!start_error)
          {
            socket_.set_option(tcp::socket::send_buffer_size(settings_.socket_recv_buffer_size_), start_error);
            if (!start_error)
            {
              if (settings_.no_delay_)
              {
                socket_.set_option(tcp::no_delay(true), start_error);
              }
              if (!start_error) 
              {
                state_ = started;          
                read_some();
              }
            }
          }                        
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              start_error
            )
          ); 
        }
      } // session::do_start

      void session::do_stop(const session_completion::handler& handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              boost::asio::error::operation_aborted
            )
          );          
        }
        else
        {
          // Start shutdown
          state_ = stop_in_progress;
          // Do shutdown - abort outer operations
          if (wait_handler_.has_target())
          {
            wait_handler_.post(boost::asio::error::operation_aborted);
          }
          // Do shutdown - flush socket's write_some buffer
          if (!socket_write_in_progress_) 
          {
            socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, stop_error_);            
          }
          // Check for shutdown continuation
          if (may_complete_stop())
          {
            complete_stop();
            // Signal shutdown completion
            io_service_.post
            (
              detail::bind_handler
              (
                handler, 
                stop_error_
              )
            );
          }
          else
          {
            stop_handler_.store(handler);
          }
        }
      } // session::do_stop

      bool session::may_complete_stop() const
      {
        return !socket_write_in_progress_ && !socket_read_in_progress_;
      } // session::may_complete_stop

      void session::complete_stop()
      {        
        boost::system::error_code error;
        socket_.close(error);
        if (!stop_error_)
        {
          stop_error_ = error;
        }
        state_ = stopped;  
      } // session::complete_stop

      void session::do_wait(const session_completion::handler& handler)
      {
        if (stopped == state_ || stop_in_progress == state_)
        {          
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              boost::asio::error::operation_aborted
            )
          );          
        } 
        else if (started != state_)
        {          
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              boost::asio::error::operation_not_supported
            )
          );          
        }
        else if (!socket_read_in_progress_ && !socket_write_in_progress_)
        {
          io_service_.post
          (
            detail::bind_handler
            (
              handler, 
              error_
            )
          );
        }
        else
        {          
          wait_handler_.store(handler);
        } 
      } // session::do_wait

      void session::read_some()
      {
        ma::cyclic_buffer::mutable_buffers_type buffers(buffer_.prepared());
        std::size_t buffers_size = boost::asio::buffers_end(buffers) - 
          boost::asio::buffers_begin(buffers);
        if (buffers_size)
        {
          socket_.async_read_some
          (
            buffers,
            strand_.wrap
            (
              ma::make_custom_alloc_handler
              (
                read_allocator_,
                boost::bind
                (
                  &this_type::handle_read_some,
                  shared_from_this(),
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred
                )
              )
            )
          );
          socket_read_in_progress_ = true;
        }        
      } // session::read_some

      void session::write_some()
      {
        ma::cyclic_buffer::const_buffers_type buffers(buffer_.data());
        std::size_t buffers_size = boost::asio::buffers_end(buffers) - 
          boost::asio::buffers_begin(buffers);
        if (buffers_size)
        {
          socket_.async_write_some
          (
            buffers,
            strand_.wrap
            (
              ma::make_custom_alloc_handler
              (
                write_allocator_,
                boost::bind
                (
                  &this_type::handle_write_some,
                  shared_from_this(),
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred
                )
              )
            )
          );
          socket_write_in_progress_ = true;
        }   
      } // session::write_some

      void session::handle_read_some(const boost::system::error_code& error,
          const std::size_t bytes_transferred)
      {
        socket_read_in_progress_ = false;
        if (stop_in_progress == state_)
        {  
          if (may_complete_stop())
          {
            complete_stop();       
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else if (error)
        {
          if (!error_)
          {
            error_ = error;
          }                    
          wait_handler_.post(error);
        }
        else 
        {
          buffer_.consume(bytes_transferred);
          read_some();
          if (!socket_write_in_progress_)
          {
            write_some();
          }
        }
      } // session::handle_read_some

      void session::handle_write_some(const boost::system::error_code& error,
          const std::size_t bytes_transferred)
      {
        socket_write_in_progress_ = false;
        if (stop_in_progress == state_)
        {
          socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_send, stop_error_);
          if (may_complete_stop())
          {
            complete_stop();       
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else if (error)
        {
          if (!error_)
          {
            error_ = error;
          }                    
          wait_handler_.post(error);
        }
        else
        {
          buffer_.commit(bytes_transferred);
          write_some();
          if (!socket_read_in_progress_)
          {
            read_some();
          }
        }
      } // session::handle_write_some

    } // namespace server2
  } // namespace echo
} // namespace ma