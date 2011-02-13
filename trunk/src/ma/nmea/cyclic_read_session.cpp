//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <stdexcept>
#include <boost/make_shared.hpp>
#include <ma/nmea/cyclic_read_session.hpp>

namespace ma
{
  namespace nmea
  { 
    cyclic_read_session::cyclic_read_session(boost::asio::io_service& io_service,
      const std::size_t read_buffer_size, const std::size_t frame_buffer_size,
      const std::string& frame_head, const std::string& frame_tail)
      : io_service_(io_service)        
      , strand_(io_service)
      , serial_port_(io_service)
      , read_handler_(io_service)        
      , stop_handler_(io_service)
      , frame_buffer_(frame_buffer_size)
      , state_(ready_to_start)
      , port_write_in_progress_(false)
      , port_read_in_progress_(false)                
      , read_buffer_(read_buffer_size)
      , frame_head_(frame_head)
      , frame_tail_(frame_tail)
    {
      if (frame_buffer_size < min_message_queue_size)
      {
        boost::throw_exception(
          std::invalid_argument("too small frame_buffer_size"));
      }
      if (read_buffer_size < max_message_size)
      {
        boost::throw_exception(
          std::invalid_argument("too small read_buffer_size"));
      }
      if (frame_head.length() > read_buffer_size)
      {
        boost::throw_exception(
          std::invalid_argument("too large frame_head"));
      }
      if (frame_tail.length() > read_buffer_size)
      {
        boost::throw_exception(
          std::invalid_argument("too large frame_tail"));
      }
    } // cyclic_read_session::cyclic_read_session    
        
    void cyclic_read_session::resest()
    {
      frame_buffer_.clear();        
      read_error_.clear();
      read_buffer_.consume(boost::asio::buffer_size(read_buffer_.data()));        
      state_ = ready_to_start;
    } // cyclic_read_session::resest

    boost::system::error_code cyclic_read_session::start()
    {              
      if (ready_to_start != state_)
      {          
        return session_error::invalid_state;          
      }      
      // Start handshake
      state_ = start_in_progress;
      // Start internal activity
      if (!port_read_in_progress_)
      {
        read_until_head();
      }
      // Handshake completed
      state_ = started;          
      // Signal successful handshake completion.
      return boost::system::error_code();      
    } // cyclic_read_session::start

    boost::optional<boost::system::error_code> cyclic_read_session::stop()
    {      
      if (stopped == state_ || stop_in_progress == state_)
      {          
        return session_error::invalid_state;
      }
      // Start shutdown
      state_ = stop_in_progress;
      // Do shutdown - abort inner operations
      serial_port_.close(stop_error_);         
      // Do shutdown - abort outer operations
      if (read_handler_.has_target())
      {
        read_handler_.post(read_result_type(session_error::operation_aborted, 0));
      }
      // Check for shutdown completion
      if (may_complete_stop())
      {        
        complete_stop();
        // Signal shutdown completion
        return stop_error_;              
      }
      return boost::optional<boost::system::error_code>();
    } // cyclic_read_session::stop

    boost::optional<boost::system::error_code> cyclic_read_session::read_some()
    {
      if (started != state_ || read_handler_.has_target())
      {          
        return session_error::invalid_state;
      }
      if (!frame_buffer_.empty())
      {                
        // Signal that we can safely fill input buffer from the frame_buffer_
        return boost::system::error_code();
      }
      if (read_error_)
      {
        boost::system::error_code error = read_error_;
        read_error_.clear();
        return error;        
      }      
      // If can't immediately complete then start waiting for completion
      // Start message constructing
      if (!port_read_in_progress_)
      {
        read_until_head();
      }        
      return boost::optional<boost::system::error_code>();
    } // cyclic_read_session::read

    bool cyclic_read_session::may_complete_stop() const
    {
      return !port_write_in_progress_ && !port_read_in_progress_;
    } // cyclic_read_session::may_complete_stop

    void cyclic_read_session::complete_stop()
    {
      state_ = stopped;
    } // cyclic_read_session::complete_stop

    void cyclic_read_session::read_until_head()
    {                                 
      boost::asio::async_read_until(serial_port_, read_buffer_, frame_head_,
        strand_.wrap(make_custom_alloc_handler(read_allocator_, 
          boost::bind(&this_type::handle_read_head, shared_from_this(), 
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred))));                  
      port_read_in_progress_ = true;          
    } // cyclic_read_session::read_until_head

    void cyclic_read_session::read_until_tail()
    {                    
      boost::asio::async_read_until(serial_port_, read_buffer_, frame_tail_,
        strand_.wrap(make_custom_alloc_handler(read_allocator_, 
          boost::bind(&this_type::handle_read_tail, shared_from_this(), 
            boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred))));
      port_read_in_progress_ = true;
    } // cyclic_read_session::read_until_tail

    void cyclic_read_session::handle_read_head(const boost::system::error_code& error, 
      const std::size_t bytes_transferred)
    {         
      port_read_in_progress_ = false;
      // Check for pending session stop operation 
      if (stop_in_progress == state_)
      {          
        if (may_complete_stop())
        {
          complete_stop();
          post_stop_handler();
        }
        return;
      }
      if (error)
      {
        // Check for pending session read operation 
        if (read_handler_.has_target())
        {        
          read_handler_.post(read_result_type(error, 0));
          return;
        }
        // Store error for the next outer read operation.
        read_error_ = error;        
        return;
      }      
      // We do not need in-between-frame-garbage and frame's head
      read_buffer_.consume(bytes_transferred);                    
      read_until_tail();
    } // cyclic_read_session::handle_read_head

    void cyclic_read_session::handle_read_tail(const boost::system::error_code& error, 
      const std::size_t bytes_transferred)
    {                  
      port_read_in_progress_ = false;        
      // Check for pending session stop operation 
      if (stop_in_progress == state_)
      {
        if (may_complete_stop())
        {
          complete_stop();
          post_stop_handler();
        }
        return;
      }
      if (error)        
      {
        // Check for pending session read operation 
        if (read_handler_.has_target())
        {
          read_handler_.post(read_result_type(error, 0));
          return;
        }      
        // Store error for the next outer read operation.
        read_error_ = error;
        return;
      }
      typedef boost::asio::streambuf::const_buffers_type const_buffers_type;
      typedef boost::asio::buffers_iterator<const_buffers_type> buffers_iterator;
      const_buffers_type committed_buffers(read_buffer_.data());
      buffers_iterator data_begin(buffers_iterator::begin(committed_buffers));
      buffers_iterator data_end(data_begin + bytes_transferred - frame_tail_.length());        
      frame_ptr new_frame;
      if (frame_buffer_.full())
      {
        new_frame = frame_buffer_.front();
        new_frame->assign(data_begin, data_end);        
      }
      else
      {
        new_frame = boost::make_shared<frame>(data_begin, data_end);
      }          
      // Consume processed data
      read_buffer_.consume(bytes_transferred);
      // Continue inner operations loop.
      read_until_head();
      // Save read message into the cyclic read buffer            
      frame_buffer_.push_back(new_frame);      
      // If there is waiting read operation - complete it            
      if (read_handler_.has_target())
      {
        boost::system::error_code transfer_error;
        read_handler_base* the_handler = get_read_handler();        
        std::size_t frames_trasferred = the_handler->copy(frame_buffer_, transfer_error);
        frame_buffer_.erase_begin(frames_trasferred);
        read_handler_.post(read_result_type(transfer_error, frames_trasferred));        
      }      
    } // cyclic_read_session::handle_read_tail

    void cyclic_read_session::post_stop_handler()
    {
      if (stop_handler_.has_target()) 
      {
        // Signal shutdown completion
        stop_handler_.post(stop_error_);
      }
    } // cyclic_read_session::post_stop_handler
            
  } // namespace nmea
} // namespace ma
