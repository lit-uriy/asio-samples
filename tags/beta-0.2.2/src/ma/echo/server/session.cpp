//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <utility>
#include <ma/config.hpp>
#include <ma/custom_alloc_handler.hpp>
#include <ma/strand_wrapped_handler.hpp>
#include <ma/echo/server/error.hpp>
#include <ma/echo/server/session.hpp>

namespace ma
{    
  namespace echo
  {
    namespace server
    {
      namespace 
      {
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)      
        class io_handler_binder
        {
        private:
          typedef io_handler_binder this_type;
          this_type& operator=(const this_type&);

        public:
          typedef void result_type;
          typedef void (session::*function_type)(const boost::system::error_code&, const std::size_t);

          template <typename SessionPtr>
          io_handler_binder(function_type function, SessionPtr&& the_session)
            : function_(function)
            , session_(std::forward<SessionPtr>(the_session))
          {
          } 

  #if defined(_DEBUG)
          io_handler_binder(const this_type& other)
            : function_(other.function_)
            , session_(other.session_)
          {
          }
  #endif

          io_handler_binder(this_type&& other)
            : function_(other.function_)
            , session_(std::move(other.session_))
          {
          }

          void operator()(const boost::system::error_code& error, 
            const std::size_t bytes_transferred)
          {
            ((*session_).*function_)(error, bytes_transferred);
          }

        private:
          function_type function_;
          session_ptr session_;
        }; // class io_handler_binder

        class timer_handler_binder
        {
        private:
          typedef timer_handler_binder this_type;
          this_type& operator=(const this_type&);

        public:
          typedef void result_type;
          typedef void (session::*function_type)(const boost::system::error_code&);

          template <typename SessionPtr>
          timer_handler_binder(function_type function, SessionPtr&& the_session)
            : function_(function)
            , session_(std::forward<SessionPtr>(the_session))
          {
          } 

  #if defined(_DEBUG)
          timer_handler_binder(const this_type& other)
            : function_(other.function_)
            , session_(other.session_)
          {
          }
  #endif

          timer_handler_binder(this_type&& other)
            : function_(other.function_)
            , session_(std::move(other.session_))
          {
          }

          void operator()(const boost::system::error_code& error)
          {
            ((*session_).*function_)(error);
          }

        private:
          function_type function_;
          session_ptr session_;
        }; // class timer_handler_binder
#endif // defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)

      } // anonymous namespace

      session::session(boost::asio::io_service& io_service, const session_options& options)
        : socket_recv_buffer_size_(options.socket_recv_buffer_size())
        , socket_send_buffer_size_(options.socket_send_buffer_size())
        , no_delay_(options.no_delay())
        , read_timeout_(options.read_timeout())
        , socket_write_in_progress_(false)
        , socket_read_in_progress_(false)
        , read_timer_in_progress_(false)
        , socket_closed_for_stop_(false)
        , state_(ready_to_start)
        , io_service_(io_service)
        , strand_(io_service)
        , socket_(io_service)
        , read_timer_(io_service)
        , buffer_(options.buffer_size())
        , wait_handler_(io_service)
        , stop_handler_(io_service)                
      {          
      }      

      void session::reset()
      {
        boost::system::error_code ignored;
        socket_.close(ignored);

        socket_write_in_progress_ = false;
        socket_read_in_progress_  = false;
        read_timer_in_progress_   = false;
        socket_closed_for_stop_   = false;

        wait_error_.clear();
        stop_error_.clear();
        buffer_.reset();

        state_ = ready_to_start;        
      }
              
      boost::system::error_code session::start()
      {                
        if (ready_to_start != state_)
        {
          return server_error::invalid_state;
        }
        boost::system::error_code error = apply_socket_options();
        if (error)
        {
          boost::system::error_code ignored;
          socket_.close(ignored);
        }
        else 
        {
          state_ = started;
          read_some();
        }
        return error;
      }

      boost::optional<boost::system::error_code> session::stop()
      {        
        if (stopped == state_ || stop_in_progress == state_)
        {          
          return boost::system::error_code(server_error::invalid_state);
        }        
        // Start shutdown
        state_ = stop_in_progress;
        // Do shutdown - abort outer operations        
        if (wait_handler_.has_target())
        {
          wait_handler_.post(server_error::operation_aborted);
        }
        // Do shutdown - flush socket's write_some buffer
        if (!socket_write_in_progress_)
        {
          socket_.shutdown(protocol_type::socket::shutdown_send, stop_error_);
        }
        // Check for shutdown continuation          
        if (may_complete_stop())
        {
          complete_stop();
          return stop_error_;
        }
        return boost::optional<boost::system::error_code>();
      }

      boost::optional<boost::system::error_code> session::wait()
      {                
        if (started != state_ || wait_handler_.has_target())
        {
          return boost::system::error_code(server_error::invalid_state);
        }
        if (wait_error_)
        {
          return wait_error_;
        }
        return boost::optional<boost::system::error_code>();
      }

      boost::system::error_code session::apply_socket_options()
      {
        typedef protocol_type::socket socket_type;
        boost::system::error_code error;
        if (socket_recv_buffer_size_)
        {
          socket_.set_option(socket_type::receive_buffer_size(*socket_recv_buffer_size_), error);
          if (error)
          {
            return error;
          }
        }        
        if (socket_recv_buffer_size_)
        {
          socket_.set_option(socket_type::send_buffer_size(*socket_recv_buffer_size_), error);
          if (error)
          {
            return error;
          }
        }
        if (no_delay_)
        {
          socket_.set_option(protocol_type::no_delay(*no_delay_), error);
        }
        return error;
      }

      bool session::may_continue_read() const
      {
        return !wait_error_ && !socket_read_in_progress_ && !read_timer_in_progress_;
      }
      
      bool session::may_continue_write() const
      {
        return !wait_error_ && !socket_write_in_progress_;
      }

      bool session::may_complete_stop() const
      {
        return !socket_write_in_progress_ && !socket_read_in_progress_ && !read_timer_in_progress_;
      }

      void session::complete_stop()
      {
        boost::system::error_code error;
        close_socket_for_stop(error);
        if (!stop_error_)
        {
          stop_error_ = error;
        }        
        state_ = stopped;  
      }

      void session::read_some()
      {
        cyclic_buffer::mutable_buffers_type buffers(buffer_.prepared());
        std::size_t buffers_size = std::distance(
          boost::asio::buffers_begin(buffers), boost::asio::buffers_end(buffers));

        if (buffers_size)
        {
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
          socket_.async_read_some(buffers, MA_STRAND_WRAP(strand_, 
            make_custom_alloc_handler(read_allocator_,
              io_handler_binder(&this_type::handle_read_some, shared_from_this()))));
#else
          socket_.async_read_some(buffers, MA_STRAND_WRAP(strand_, 
            make_custom_alloc_handler(read_allocator_,
              boost::bind(&this_type::handle_read_some, shared_from_this(), 
                boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred))));
#endif
          socket_read_in_progress_ = true;

          if (read_timeout_)
          {
            read_timer_.expires_from_now(*read_timeout_);
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
            read_timer_.async_wait(MA_STRAND_WRAP(strand_, 
              make_custom_alloc_handler(read_timeout_allocator_,
                timer_handler_binder(&this_type::handle_read_timeout, shared_from_this()))));
#else
            read_timer_.async_wait(MA_STRAND_WRAP(strand_, 
              make_custom_alloc_handler(read_timeout_allocator_,
                boost::bind(&this_type::handle_read_timeout, shared_from_this(), boost::asio::placeholders::error))));
#endif
            read_timer_in_progress_ = true;
          }
        }        
      }

      void session::write_some()
      {
        cyclic_buffer::const_buffers_type buffers(buffer_.data());
        std::size_t buffers_size = std::distance(
          boost::asio::buffers_begin(buffers), boost::asio::buffers_end(buffers));

        if (buffers_size)
        {
#if defined(MA_HAS_RVALUE_REFS) && defined(MA_BOOST_BIND_HAS_NO_MOVE_CONTRUCTOR)
          socket_.async_write_some(buffers, MA_STRAND_WRAP(strand_, 
            make_custom_alloc_handler(write_allocator_,
              io_handler_binder(&this_type::handle_write_some, shared_from_this()))));
#else
          socket_.async_write_some(buffers, MA_STRAND_WRAP(strand_,
            make_custom_alloc_handler(write_allocator_,
              boost::bind(&this_type::handle_write_some, shared_from_this(), 
                boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred))));
#endif
          socket_write_in_progress_ = true;
        }   
      }

      void session::handle_read_some(const boost::system::error_code& error, std::size_t bytes_transferred)
      {        
        socket_read_in_progress_ = false;
        // Check for pending session stop operation 
        if (stop_in_progress == state_)
        {          
          if (read_timer_in_progress_)
          {
            read_timer_.cancel();
          }
          complete_deferred_stop();          
          return;
        }
        // Normal control flow
        if (read_timer_in_progress_)
        {
          if (!read_timer_.cancel())
          {
            // We are late
            return;
          }
        }
        if (error)
        {
          add_run_error(error);
          return;
        }        
        buffer_.consume(bytes_transferred);
        if (may_continue_read())
        {
          read_some();
        }
        if (may_continue_write())
        {
          write_some();
        }
      }

      void session::handle_write_some(const boost::system::error_code& error, std::size_t bytes_transferred)
      {
        socket_write_in_progress_ = false;
        // Check for pending session manager stop operation 
        if (stop_in_progress == state_)
        {
          socket_.shutdown(protocol_type::socket::shutdown_send, stop_error_);
          complete_deferred_stop();
          return;
        }
        if (error)
        {
          add_run_error(error);
          return;
        }        
        buffer_.commit(bytes_transferred);
        if (may_continue_write())
        {
          write_some();
        }
        if (may_continue_read())
        {
          read_some();
        }        
      }

      void session::handle_read_timeout(const boost::system::error_code& error)
      {
        read_timer_in_progress_ = false;
        // Check for pending session stop operation 
        if (stop_in_progress == state_)
        {
          if (!error && socket_read_in_progress_)
          {
            // Timer was normally fired during stop
            boost::system::error_code ignored;
            close_socket_for_stop(ignored);            
          }
          complete_deferred_stop();
          return;
        }        
        if (boost::asio::error::operation_aborted == error)
        {
          // Normal timer cancellation
          if (may_continue_read())
          {
            read_some();
          }
          return;
        }
        if (error)
        {
          // Abnormal timer behaivour
          add_run_error(error);
          return;
        }
        // Timer was normally fired 
        boost::system::error_code ignored;
        close_socket_for_stop(ignored);
        add_run_error(server_error::read_timeout);
      }

      void session::close_socket_for_stop(boost::system::error_code& error)
      {
        boost::system::error_code local_error;
        if (!socket_closed_for_stop_)
        {
          socket_.close(local_error);
          socket_closed_for_stop_ = true;
        }
        error = local_error;
      }

      void session::add_run_error(const boost::system::error_code& error)
      {
        if (!wait_error_)
        {
          wait_error_ = error;
          if (wait_handler_.has_target())
          {
            wait_handler_.post(wait_error_);
          }
        }
      }

      void session::complete_deferred_stop()
      {
        if (may_complete_stop())
        {
          complete_stop();
          if (stop_handler_.has_target()) 
          {
            // Signal shutdown completion
            stop_handler_.post(stop_error_);
          }
        }
      }      
        
    } // namespace server
  } // namespace echo
} // namespace ma