//
// Copyright (c) 2010 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_ECHO_SERVER3_SESSION_H
#define MA_ECHO_SERVER3_SESSION_H

#include <utility>
#include <boost/utility.hpp>
#include <boost/system/error_code.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/asio.hpp>
#include <ma/echo/server3/allocator.h>
#include <ma/echo/server3/session_handler_fwd.h>
#include <ma/handler_allocation.hpp>

namespace ma
{    
  namespace echo
  {
    namespace server3
    {
      class session 
        : private boost::noncopyable
        , public boost::enable_shared_from_this<session>
      {
      private:
        typedef session this_type;

      public:
        struct settings
        {              
          std::size_t buffer_size_;
          int socket_recv_buffer_size_;
          int socket_send_buffer_size_;
          bool no_delay_;

          explicit settings(std::size_t buffer_size,
            int socket_recv_buffer_size,
            int socket_send_buffer_size,
            bool no_delay);
        }; // struct settings

        explicit session(boost::asio::io_service& io_service, const settings& settings);
        ~session();

        void reset();
        boost::asio::ip::tcp::socket& session::socket();
        void async_start(const boost::shared_ptr<allocator>& operation_allocator,
          const boost::weak_ptr<session_start_handler>& handler);
        void async_stop(const boost::shared_ptr<allocator>& operation_allocator,
          const boost::weak_ptr<session_start_handler>& handler);
        void async_wait(const boost::shared_ptr<allocator>& operation_allocator,
          const boost::weak_ptr<session_start_handler>& handler);

      private:        
        enum state_type
        {
          ready_to_start,
          start_in_progress,
          started,
          stop_in_progress,
          stopped
        }; 
        
        typedef std::pair<boost::weak_ptr<session_stop_handler>, boost::shared_ptr<allocator> >  stop_handler_type;
        typedef std::pair<boost::weak_ptr<session_wait_handler>, boost::shared_ptr<allocator> >  wait_handler_type;
        
        void do_start(const boost::shared_ptr<allocator>& operation_allocator,
          const boost::weak_ptr<session_start_handler>& handler);
        void do_wait(const boost::shared_ptr<allocator>& operation_allocator,
          const boost::weak_ptr<session_wait_handler>& handler);
        void do_stop(const boost::shared_ptr<allocator>& operation_allocator,
          const boost::weak_ptr<session_stop_handler>& handler);
        bool may_complete_stop() const;
        void complete_stop();        
        void read_some();
        void write_some();
        void handle_read_some(const boost::system::error_code& error,
          const std::size_t bytes_transferred);
        void handle_write_some(const boost::system::error_code& error,
          const std::size_t bytes_transferred);

        boost::asio::io_service& io_service_;
        boost::asio::io_service::strand strand_;
        boost::asio::ip::tcp::socket socket_;
        stop_handler_type  stop_handler_;
        wait_handler_type  wait_handler_;
        boost::system::error_code error_;
        boost::system::error_code stop_error_;
        settings settings_;
        state_type state_;
        bool socket_write_in_progress_;
        bool socket_read_in_progress_;
        ma::cyclic_buffer buffer_;
        in_place_handler_allocator<640> write_allocator_;
        in_place_handler_allocator<256> read_allocator_;
      }; // class session 

    } // namespace server3
  } // namespace echo
} // namespace ma

#endif // MA_ECHO_SERVER3_SESSION_H
