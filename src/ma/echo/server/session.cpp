//
// Copyright (c) 2010 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <stdexcept>
#include <boost/throw_exception.hpp>
#include <ma/echo/server/session.hpp>

namespace ma
{    
  namespace echo
  {
    namespace server
    {          
      session::settings::settings(std::size_t buffer_size,
        int socket_recv_buffer_size, int socket_send_buffer_size,
        bool no_delay)
          : no_delay_(no_delay)          
          , socket_recv_buffer_size_(socket_recv_buffer_size)
          , socket_send_buffer_size_(socket_send_buffer_size)
          , buffer_size_(buffer_size)         
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
        : socket_write_in_progress_(false)
        , socket_read_in_progress_(false) 
        , state_(ready_to_start)
        , io_service_(io_service)
        , strand_(io_service)
        , socket_(io_service)
        , wait_handler_(io_service)
        , stop_handler_(io_service)
        , settings_(settings)                
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

      void session::read_some()
      {
        cyclic_buffer::mutable_buffers_type buffers(buffer_.prepared());
        std::size_t buffers_size = boost::asio::buffers_end(buffers) - 
          boost::asio::buffers_begin(buffers);
        if (buffers_size)
        {
          socket_.async_read_some
          (
            buffers,
            strand_.wrap
            (
              make_custom_alloc_handler
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
        cyclic_buffer::const_buffers_type buffers(buffer_.data());
        std::size_t buffers_size = boost::asio::buffers_end(buffers) - 
          boost::asio::buffers_begin(buffers);
        if (buffers_size)
        {
          socket_.async_write_some
          (
            buffers,
            strand_.wrap
            (
              make_custom_alloc_handler
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

      void session::handle_read_some(const boost::system::error_code& error, const std::size_t bytes_transferred)
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

      void session::handle_write_some(const boost::system::error_code& error, const std::size_t bytes_transferred)
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
        
    } // namespace server
  } // namespace echo
} // namespace ma
