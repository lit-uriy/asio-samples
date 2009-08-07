//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_NMEA_CYCLIC_READ_SESSION_HPP
#define MA_NMEA_CYCLIC_READ_SESSION_HPP

#include <stdexcept>
#include <string>
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
  namespace nmea
  { 
    class cyclic_read_session;
    typedef std::string message_type;
    typedef boost::shared_ptr<message_type> message_ptr;    
    typedef boost::shared_ptr<cyclic_read_session> cyclic_read_session_ptr;

    class cyclic_read_session 
      : private boost::noncopyable
      , public boost::enable_shared_from_this<cyclic_read_session>
    {
    private:
      typedef cyclic_read_session this_type;
      typedef boost::tuple<message_ptr, boost::system::error_code> read_arg_type;
      BOOST_STATIC_CONSTANT(std::size_t, max_message_size = 512);           

    public:
      BOOST_STATIC_CONSTANT(std::size_t, min_read_buffer_size = max_message_size);
      BOOST_STATIC_CONSTANT(std::size_t, min_message_queue_size = 1);

      explicit cyclic_read_session(
        boost::asio::io_service& io_service,
        const std::size_t read_buffer_size,
        const std::size_t message_queue_size,
        const std::string& frame_head,
        const std::string& frame_tail)
        : frame_head_(frame_head)
        , frame_tail_(frame_tail)                  
        , io_service_(io_service)
        , strand_(io_service)
        , serial_port_(io_service)
        , read_handler_(io_service)        
        , stop_handler_(io_service)
        , start_done_(false)
        , stop_done_(false)        
        , write_in_progress_(false)
        , read_in_progress_(false)
        , read_buffer_(read_buffer_size)
        , message_queue_(message_queue_size)        
      {
        if (message_queue_size < min_message_queue_size)
        {
          boost::throw_exception(std::runtime_error("too small message_queue_size"));
        }
        if (max_message_size > read_buffer_size)
        {
          boost::throw_exception(std::runtime_error("too small read_buffer_size"));
        }
        if (frame_head.length() > read_buffer_size)
        {
          boost::throw_exception(std::runtime_error("too large frame_head"));
        }
        if (frame_tail.length() > read_buffer_size)
        {
          boost::throw_exception(std::runtime_error("too large frame_tail"));
        }
      }

      ~cyclic_read_session()
      {          
      }      

      boost::asio::serial_port& serial_port()
      {
        return serial_port_;
      }
      
      void resest()
      {
        message_queue_.clear();        
        read_error_ = boost::system::error_code();
        read_buffer_.consume(
          boost::asio::buffer_size(read_buffer_.data()));        
        start_done_ = false;
        stop_done_ = false;
      }

      template <typename Handler>
      void async_start(Handler handler)
      {        
        strand_.dispatch
        (
          make_context_alloc_handler
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
      }

      template <typename Handler>
      void async_stop(Handler handler)
      {
        strand_.dispatch
        (
          make_context_alloc_handler
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
      }
      
      template <typename Handler>
      void async_write(const message_ptr& message, Handler handler)
      {                
        strand_.dispatch
        (
          make_context_alloc_handler
          (
            handler,
            boost::bind
            (
              &this_type::do_write<Handler>, 
              shared_from_this(),
              message, 
              boost::make_tuple(handler)              
            )
          )
        );
      }

      template <typename Handler>
      void async_read(message_ptr& message, Handler handler)
      {                
        strand_.dispatch
        (
          make_context_alloc_handler
          (
            handler,
            boost::bind
            (
              &this_type::do_read<Handler>, 
              shared_from_this(),
              boost::ref(message), 
              boost::make_tuple(handler)              
            )
          )
        );
      }        
    
    private:        
      template <typename Handler>
      void do_start(boost::tuple<Handler> handler)
      {
        if (stop_done_ || stop_handler_.has_target())
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
        else if (start_done_)
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
          // Start handshake: start internal activity.
          if (!read_in_progress_)
          {
            read_until_head();
          }

          // Complete handshake immediately
          start_done_ = true;

          // Signal successful handshake completion.
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
        if (stop_done_)
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
        else if (stop_handler_.has_target())
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
          // Start shutdown

          // Do shutdown - abort inner operations
          serial_port_.close(stop_error_);
          
          // Check for shutdown continuation
          if (read_handler_.has_target()
            || write_in_progress_ 
            || read_in_progress_)
          {
            read_handler_.cancel();
            // Wait for others operations' completion
            stop_handler_.store(
              boost::asio::error::operation_aborted,
              boost::get<0>(handler));
          }        
          else
          {
            stop_done_ = true;
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
        }        
      } // do_stop
      
      template <typename Handler>
      void do_write(const message_ptr& message, boost::tuple<Handler> handler)
      {  
        if (stop_done_ || stop_handler_.has_target())
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
        else if (!start_done_ || write_in_progress_)
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
          boost::asio::async_write
          (
            serial_port_, 
            boost::asio::buffer(*message), 
            strand_.wrap
            (
              make_custom_alloc_handler
              (
                write_allocator_, 
                boost::bind
                (
                  &this_type::handle_write<handler>, 
                  shared_from_this(),                
                  boost::asio::placeholders::error,                   
                  message,
                  handler
                )
              )
            )
          );
          write_in_progress_ = true;
        }
      } // do_write

      template <typename Handler>
      void handle_write(const boost::system::error_code& error,         
        const message_ptr&, boost::tuple<Handler> handler)
      {         
        write_in_progress_ = false;        
        io_service_.post
        (
          boost::asio::detail::bind_handler
          (
            boost::get<0>(handler), 
            error
          )
        );
        if (stop_handler_.has_target() && !read_in_progress_)
        {
          stop_done_ = true;
          // Signal shutdown completion
          stop_handler_.post(stop_error_);
        }
      } 

      template <typename Handler>
      void do_read(message_ptr& message, boost::tuple<Handler> handler)
      {
        if (stop_done_ || stop_handler_.has_target())
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
        else if (!start_done_ || read_handler_.has_target())
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
        else if (!message_queue_.empty())
        {
          message = message_queue_.front();
          message_queue_.pop_front();
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              boost::system::error_code()
            )
          );
        }
        else if (read_error_)
        {            
          message = message_ptr();
          io_service_.post
          (
            boost::asio::detail::bind_handler
            (
              boost::get<0>(handler), 
              read_error_
            )
          );
          read_error_= boost::system::error_code();          
        }
        else
        {          
          // If can't immediately complete then start waiting for completion
          // Start message constructing
          if (!read_in_progress_)
          {
            read_until_head();
          }

          // Wait for the ready message
          read_handler_.store
          (
            read_arg_type
            (
              message_ptr(), 
              boost::asio::error::operation_aborted
            ),          
            make_context_alloc_handler
            (
              boost::get<0>(handler), 
              boost::bind
              (
                &this_type::handle_read<Handler>, 
                _1,
                boost::ref(message), 
                handler
              )
            )          
          );
        }        
      } // do_read
      
      template <typename Handler>
      static void handle_read(const read_arg_type& arg, 
        message_ptr& target, boost::tuple<Handler> handler)
      {
        target = boost::get<0>(arg);
        boost::get<0>(handler)(boost::get<1>(arg));
      }

      void read_until_head()
      {                                 
        boost::asio::async_read_until
        (
          serial_port_, read_buffer_, frame_head_,
          strand_.wrap
          (
            make_custom_alloc_handler
            (
              read_allocator_, 
              boost::bind
              (
                &this_type::handle_read_head, 
                shared_from_this(), 
                boost::asio::placeholders::error, 
                boost::asio::placeholders::bytes_transferred
              )
            )
          )
        );                  
        read_in_progress_ = true;          
      } // read_until_head

      void read_until_tail()
      {                    
        boost::asio::async_read_until
        (
          serial_port_, read_buffer_, frame_tail_,
          strand_.wrap
          (
            make_custom_alloc_handler
            (
              read_allocator_, 
              boost::bind
              (
                &this_type::handle_read_tail, 
                shared_from_this(), 
                boost::asio::placeholders::error, 
                boost::asio::placeholders::bytes_transferred
              )
            )
          )
        );
        read_in_progress_ = true;
      } // read_until_tail

      void handle_read_head(const boost::system::error_code& error, 
        const std::size_t bytes_transferred)
      {         
        read_in_progress_ = false;        
        if (stop_handler_.has_target())
        {
          read_handler_.cancel();
          if (!write_in_progress_)
          {
            stop_done_ = true;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else if (!error)
        {
          // We do not need in-between-frame-garbage and frame's head
          read_buffer_.consume(bytes_transferred);                    
          read_until_tail();
        }
        else if (read_handler_.has_target())
        {
          read_handler_.post(read_arg_type(message_ptr(), error));
        }
        else
        {
          // Store error for the next outer read operation.
          read_error_ = error;          
        }
      } // handle_read_head

      void handle_read_tail(const boost::system::error_code& error, 
        const std::size_t bytes_transferred)
      {                  
        read_in_progress_ = false;        
        if (stop_handler_.has_target())
        {
          read_handler_.cancel();
          if (!write_in_progress_)
          {
            stop_done_ = true;
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
        else if (!error)        
        {                   
          typedef boost::asio::streambuf::const_buffers_type const_buffers_type;
          typedef boost::asio::buffers_iterator<const_buffers_type> buffers_iterator;
          const_buffers_type committed_buffers(read_buffer_.data());
          buffers_iterator data_begin(buffers_iterator::begin(committed_buffers));
          buffers_iterator data_end(data_begin + bytes_transferred - frame_tail_.length());
          bool newly_allocated = !message_queue_.full() || read_handler_.has_target();
          message_ptr new_message;
          if (newly_allocated)
          {
            new_message.reset(new message_type(data_begin, data_end));
          }
          else
          {
            new_message = message_queue_.front();
            new_message->assign(data_begin, data_end);
          }          
          // Consume processed data
          read_buffer_.consume(bytes_transferred);
          // Continue inner operations loop.
          read_until_head();
          // If there is waiting read operation - complete it            
          if (read_handler_.has_target())
          {              
            read_handler_.post(read_arg_type(new_message, error));
          } 
          else
          {
            // else push ready message into the cyclic read buffer            
            message_queue_.push_back(new_message);              
          }          
        }
        else if (read_handler_.has_target())
        {
          read_handler_.post(read_arg_type(message_ptr(), error));
        }
        else
        {
          // Store error for the next outer read operation.
          read_error_ = error;          
        }
      } // handle_read_tail
                
      std::string frame_head_;
      std::string frame_tail_;                    
      boost::asio::io_service& io_service_;
      boost::asio::io_service::strand strand_;
      boost::asio::serial_port serial_port_;               
      ma::handler_storage<read_arg_type> read_handler_;      
      ma::handler_storage<boost::system::error_code> stop_handler_;      
      bool start_done_;
      bool stop_done_;      
      bool write_in_progress_;
      bool read_in_progress_;
      boost::asio::streambuf read_buffer_;        
      handler_allocator write_allocator_;
      handler_allocator read_allocator_;
      boost::system::error_code read_error_;
      boost::system::error_code stop_error_;
      boost::circular_buffer<message_ptr> message_queue_;      
    }; // class cyclic_read_session 

  } // namespace nmea
} // namespace ma

#endif // MA_NMEA_CYCLIC_READ_SESSION_HPP