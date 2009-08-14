//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_ECHO_SESSION_HPP
#define MA_ECHO_SESSION_HPP

#include <vector>
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

namespace ma
{    
  namespace echo
  {    
    class session;
    typedef boost::shared_ptr<session> session_ptr;

    class session 
      : private boost::noncopyable 
      , public boost::enable_shared_from_this<session>
    {
    private:
      typedef session this_type;
      enum state_type
      {
        ready_to_start,
        start_in_progress,
        started,
        stop_in_progress,
        stopped
      };

      class buffer_type : private boost::noncopyable
      {
      public:
        typedef std::vector<boost::asio::const_buffer> const_buffers_type;
        typedef std::vector<boost::asio::mutable_buffer> mutable_buffers_type;

        buffer_type(std::size_t size)
          : data_(new char[size])
          , size_(size)
          , input_start_(0)
          , input_size_(size)
          , output_start_(0)
          , output_size_(0)
        {
        }

        void reset()
        {
          input_size_  = size_;
          input_start_ = output_start_ = output_size_ = 0;
        }

        void commit(std::size_t size)
        {
          if (size > output_size_)
          {
            boost::throw_exception(
              std::length_error("output sequence size is too small to consume given size"));
          }
          output_size_ -= size;
          input_size_  += size;
          std::size_t d = size_ - output_start_;
          if (size < d)
          {
            output_start_ += size;
          }
          else
          {
            output_start_ = size - d;
          }
        }

        void consume(std::size_t size)         
        {
          if (size > input_size_)
          {
            boost::throw_exception(
              std::length_error("input sequence size is too small to consume given size"));
          }
          output_size_ += size;
          input_size_  -= size;
          std::size_t d = size_ - input_start_;
          if (size < d)
          {
            input_start_ += size;
          }
          else
          {
            input_start_ = size - d;
          }
        }

        const_buffers_type data() const
        {          
          const_buffers_type buffers;          
          if (output_size_)
          {
            std::size_t d = size_ - output_start_;
            if (output_size_ > d)
            {
              buffers.push_back(
                boost::asio::const_buffer(
                data_.get() + output_start_, d));
              buffers.push_back(
                boost::asio::const_buffer(
                data_.get(), output_size_ - d));
            }
            else
            {
              buffers.push_back(
                boost::asio::const_buffer(
                  data_.get() + output_start_, output_size_));
            }
          }
          return buffers;
        }

        mutable_buffers_type prepare() const
        {          
          mutable_buffers_type buffers;
          if (input_size_)
          {
            std::size_t d = size_ - input_start_;
            if (input_size_ > d)
            {
              buffers.push_back(
                boost::asio::mutable_buffer(
                data_.get() + input_start_, d));
              buffers.push_back(
                boost::asio::mutable_buffer(
                data_.get(), input_size_ - d));
            }
            else
            {
              buffers.push_back(
                boost::asio::mutable_buffer(
                  data_.get() + input_start_, input_size_));
            }
          }
          return buffers;
        }

      private:
        boost::scoped_array<char> data_;
        std::size_t size_;
        std::size_t input_start_;
        std::size_t input_size_;
        std::size_t output_start_;
        std::size_t output_size_;
      }; // class buffer_type
      
    public:
      struct settings
      {              
        std::size_t buffer_size_;        

        explicit settings(std::size_t buffer_size)
          : buffer_size_(buffer_size)          
        {
        }
      }; // struct settings

      explicit session(boost::asio::io_service& io_service,
        const settings& settings)
        : io_service_(io_service)
        , strand_(io_service)
        , socket_(io_service)
        , wait_handler_(io_service)
        , stop_handler_(io_service)
        , state_(ready_to_start)
        , socket_write_in_progress_(false)
        , socket_read_in_progress_(false) 
        , buffer_(settings.buffer_size_)
      {        
      }

      ~session()
      {        
      }

      void reset()
      {
        error_ = boost::system::error_code();
        stop_error_ = boost::system::error_code();        
        state_ = ready_to_start;
        buffer_.reset();
      }
      
      boost::asio::ip::tcp::socket& socket()
      {
        return socket_;
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
          state_ = started;          
          read_some();
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::system::error_code()
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
          // Do shutdown - abort outer operations
          wait_handler_.cancel();
          // Do shutdown - flush socket's write buffer
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

      bool may_complete_stop() const
      {
        return !socket_write_in_progress_ && !socket_read_in_progress_;
      }

      void complete_stop()
      {        
        boost::system::error_code error;
        socket_.close(error);
        if (!stop_error_)
        {
          stop_error_ = error;
        }
        state_ = stopped;  
      }      

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
        else if (!socket_read_in_progress_ && !socket_write_in_progress_)
        {
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              error_
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

      void read_some()
      {
        buffer_type::mutable_buffers_type buffers(buffer_.prepare());
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
      }

      void write()
      {
        buffer_type::const_buffers_type buffers(buffer_.data());
        std::size_t buffers_size = boost::asio::buffers_end(buffers) - 
          boost::asio::buffers_begin(buffers);
        if (buffers_size)
        {
          boost::asio::async_write
          (
            socket_, buffers,
            strand_.wrap
            (
              ma::make_custom_alloc_handler
              (
                write_allocator_,
                boost::bind
                (
                  &this_type::handle_write,
                  shared_from_this(),
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred
                )
              )
            )
          );
          socket_write_in_progress_ = true;
        }   
      }

      void handle_read_some(const boost::system::error_code& error,
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
            write();
          }
        }
      } // handle_read_some

      void handle_write(const boost::system::error_code& error,
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
          write();
          if (!socket_read_in_progress_)
          {
            read_some();
          }
        }
      } // handle_write

      boost::asio::io_service& io_service_;
      boost::asio::io_service::strand strand_;      
      boost::asio::ip::tcp::socket socket_;
      ma::handler_storage<boost::system::error_code> wait_handler_;
      ma::handler_storage<boost::system::error_code> stop_handler_;
      boost::system::error_code error_;
      boost::system::error_code stop_error_;
      state_type state_;
      bool socket_write_in_progress_;
      bool socket_read_in_progress_;
      buffer_type buffer_;
      in_place_handler_allocator<512> write_allocator_;
      in_place_handler_allocator<256> read_allocator_;
    }; // class session

  } // namespace echo
} // namespace ma

#endif // MA_ECHO_SESSION_HPP
