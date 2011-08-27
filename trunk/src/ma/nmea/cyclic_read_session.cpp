//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <stdexcept>
#include <boost/make_shared.hpp>
#include <ma/nmea/cyclic_read_session.hpp>

namespace ma {

namespace nmea { 

cyclic_read_session::cyclic_read_session(boost::asio::io_service& io_service,
    const std::size_t read_buffer_size, const std::size_t frame_buffer_size,
    const std::string& frame_head, const std::string& frame_tail)
  : io_service_(io_service)
  , strand_(io_service)
  , serial_port_(io_service)
  , external_read_handler_(io_service)
  , external_stop_handler_(io_service)
  , frame_buffer_(frame_buffer_size)
  , port_write_in_progress_(false)
  , port_read_in_progress_(false)             
  , external_state_(external_state::ready)
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
}
  
void cyclic_read_session::resest()
{
  boost::system::error_code ignored;
  serial_port_.close(ignored);

  frame_buffer_.clear();        
  read_error_.clear();

  read_buffer_.consume(boost::asio::buffer_size(read_buffer_.data()));

  external_state_ = external_state::ready;
}

boost::system::error_code cyclic_read_session::do_start_external_start()
{
  if (external_state::ready != external_state_)
  {
    return session_error::invalid_state;          
  }
  
  external_state_ = external_state::work;

  // Start internal activity
  if (!port_read_in_progress_)
  {
    read_until_head();
  }
  
  // Signal successful handshake completion.
  return boost::system::error_code();
}

boost::optional<boost::system::error_code> 
cyclic_read_session::do_start_external_stop()
{      
  if ((external_state::stopped == external_state_) 
      || (external_state::stop == external_state_))
  {          
    return boost::optional<boost::system::error_code>(
        session_error::invalid_state);
  }

  // Start shutdown
  external_state_ = external_state::stop;

  // Do shutdown - abort outer operations
  if (external_read_handler_.has_target())
  {
    external_read_handler_.post(read_result_type(session_error::operation_aborted, 0));
  }

  // Do shutdown - abort inner operations
  serial_port_.close(stop_error_);

  // Check for shutdown completion
  if (may_complete_stop())
  {        
    complete_stop();
    // Signal shutdown completion
    return stop_error_;              
  }

  return boost::optional<boost::system::error_code>();
}

boost::optional<boost::system::error_code> 
cyclic_read_session::do_start_external_read_some()
{
  if ((external_state::work != external_state_) 
      || (external_read_handler_.has_target()))
  {          
    return boost::optional<boost::system::error_code>(
        session_error::invalid_state);
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

  // If can't immediately complete then do_start_external_start waiting for completion
  // Start message constructing
  if (!port_read_in_progress_)
  {
    read_until_head();
  }
  return boost::optional<boost::system::error_code>();
}

bool cyclic_read_session::may_complete_stop() const
{
  return !port_write_in_progress_ && !port_read_in_progress_;
}

void cyclic_read_session::complete_stop()
{
  external_state_ = external_state::stopped;
}

void cyclic_read_session::read_until_head()
{                                 
  boost::asio::async_read_until(serial_port_, read_buffer_, frame_head_, 
      MA_STRAND_WRAP(strand_, make_custom_alloc_handler(read_allocator_, 
          boost::bind(&this_type::handle_read_head, shared_from_this(), 
              boost::asio::placeholders::error, 
              boost::asio::placeholders::bytes_transferred))));

  port_read_in_progress_ = true;          
}

void cyclic_read_session::read_until_tail()
{                    
  boost::asio::async_read_until(serial_port_, read_buffer_, frame_tail_,
      MA_STRAND_WRAP(strand_, make_custom_alloc_handler(read_allocator_, 
          boost::bind(&this_type::handle_read_tail, shared_from_this(), 
              boost::asio::placeholders::error, 
              boost::asio::placeholders::bytes_transferred))));

  port_read_in_progress_ = true;
}

void cyclic_read_session::handle_read_head(
    const boost::system::error_code& error, 
    const std::size_t bytes_transferred)
{         
  port_read_in_progress_ = false;

  // Check for pending session do_start_external_stop operation 
  if (external_state::stop == external_state_)
  {          
    if (may_complete_stop())
    {
      complete_stop();
      post_external_stop_handler();
    }
    return;
  }

  if (error)
  {
    // Check for pending session read operation 
    if (external_read_handler_.has_target())
    {        
      external_read_handler_.post(read_result_type(error, 0));
      return;
    }

    // Store error for the next outer read operation.
    read_error_ = error;        
    return;
  } 

  // We do not need in-between-frame-garbage and frame's head
  read_buffer_.consume(bytes_transferred);                    
  read_until_tail();
}

void cyclic_read_session::handle_read_tail(
    const boost::system::error_code& error, 
    const std::size_t bytes_transferred)
{                  
  port_read_in_progress_ = false;

  // Check for pending session do_start_external_stop operation 
  if (external_state::stop == external_state_)
  {
    if (may_complete_stop())
    {
      complete_stop();
      post_external_stop_handler();
    }
    return;
  }

  if (error)        
  {
    // Check for pending session read operation 
    if (external_read_handler_.has_target())
    {
      external_read_handler_.post(read_result_type(error, 0));
      return;
    }

    // Store error for the next outer read operation.
    read_error_ = error;
    return;
  }

  typedef boost::asio::streambuf::const_buffers_type        const_buffers_type;
  typedef boost::asio::buffers_iterator<const_buffers_type> buffers_iterator;
  // Extract frame from buffer to distinct memory area
  const_buffers_type committed_buffers(read_buffer_.data());
  buffers_iterator data_begin(buffers_iterator::begin(committed_buffers));
  buffers_iterator data_end(
      data_begin + bytes_transferred - frame_tail_.length());

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

  // Save ready frame into the cyclic read buffer
  frame_buffer_.push_back(new_frame);

  // If there is waiting read operation - complete it            
  if (external_read_handler_.has_target())
  {        
    external_read_handler_base* handler = get_external_read_handler();
    read_result_type copy_result = handler->copy(frame_buffer_);
    frame_buffer_.erase_begin(copy_result.get<1>());
    external_read_handler_.post(copy_result);
  }
}

void cyclic_read_session::post_external_stop_handler()
{
  if (external_stop_handler_.has_target()) 
  {
    // Signal shutdown completion
    external_stop_handler_.post(stop_error_);
  }
}
            
} // namespace nmea
} // namespace ma
