//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_NMEA_SESSION_SERVICE_HPP
#define MA_NMEA_SESSION_SERVICE_HPP

#include <boost/thread.hpp>
#include <boost/asio.hpp>

namespace ma
{ 
  namespace nmea
  {
    template <typename ActiveSession>
    class session_service : public boost::asio::io_service::service
    {
    private:
      typedef session_service<ActiveSession> this_type;            

    public:      
      typedef ActiveSession impl_type;
      typedef typename impl_type::pointer implementation_type;
      typedef typename impl_type::next_layer_type next_layer_type;
      typedef typename impl_type::lowest_layer_type lowest_layer_type;

      static boost::asio::io_service::id id;

      explicit session_service(boost::asio::io_service& io_service)
        : boost::asio::io_service::service(io_service)        
        , mutex_()
        , impl_list_(0)
      {
      }

      ~session_service()
      {
      }

      void shutdown_service()
      {
        boost::mutex::scoped_lock lock(mutex_);
        impl_type* impl = impl_list_;
        while (impl)
        {
          // Terminate all user-defined pending operations.
          close_for_destruction(*impl);
          impl = impl->next_;
        }
      }

      void register_impl(implementation_type& impl)
      { 
        // Insert impl into linked list of all implementations.
        boost::mutex::scoped_lock lock(mutex_);      
        impl->next_ = impl_list_;
        impl->prev_ = 0;
        if (impl_list_)
        {
          impl_list_->prev_ = impl.get();
        }
        impl_list_ = impl.get();      
      }

      void unregister_impl(implementation_type& impl)
      {
        // Remove impl from linked list of all implementations.
        boost::mutex::scoped_lock lock(mutex_);
        if (impl_list_ == impl.get())
          impl_list_ = impl->next_;
        if (impl->prev_)
          impl->prev_->next_ = impl->next_;
        if (impl->next_)
          impl->next_->prev_= impl->prev_;
      }

      void close(impl_type& impl, boost::system::error_code& error)
      {        
        // Terminate all user-defined pending operations.
        impl.close(error);
      }            

      void close_for_destruction(impl_type& impl)
      {        
        boost::system::error_code ignored_ec;
        close(impl, ignored_ec);
      }

      void close_for_destruction(implementation_type& impl)
      {                
        close_for_destruction(*impl);
      }

      void construct(implementation_type& impl)
      { 
        // Allocate memory and construct the new impl
        implementation_type new_impl(impl_type::create(this->get_io_service()));

        // Insert impl into linked list of all implementations.
        register_impl(new_impl);

        // Swap for copy
        new_impl.swap(impl);
      }

      template <typename Arg1>
      void construct(implementation_type& impl, Arg1 arg1)
      { 
        // Allocate memory and construct the new impl
        implementation_type new_impl(impl_type::create(this->get_io_service(), arg1));

        // Insert impl into linked list of all implementations.
        register_impl(new_impl);

        // Swap for copy
        new_impl.swap(impl);
      }

      template <typename Arg1, typename Arg2>
      void construct(implementation_type& impl, Arg1 arg1, Arg2 arg2)
      { 
        // Allocate memory and construct the new impl
        implementation_type new_impl(impl_type::create(this->get_io_service(), arg1, arg2));

        // Insert impl into linked list of all implementations.
        register_impl(new_impl);

        // Swap for copy
        new_impl.swap(impl);
      }

      template <typename Arg1, typename Arg2, typename Arg3>
      void construct(implementation_type& impl, Arg1 arg1, Arg2 arg2, Arg3 arg3)
      { 
        // Allocate memory and construct the new impl
        implementation_type new_impl(impl_type::create(this->get_io_service(), arg1, arg2, arg3));

        // Insert impl into linked list of all implementations.
        register_impl(new_impl);

        // Swap for copy
        new_impl.swap(impl);
      }

      template <typename Arg1, typename Arg2, typename Arg3, typename Arg4>
      void construct(implementation_type& impl, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4)
      { 
        // Allocate memory and construct the new impl
        implementation_type new_impl(impl_type::create(this->get_io_service(), arg1, arg2, arg3, arg4));

        // Insert impl into linked list of all implementations.
        register_impl(new_impl);

        // Swap for copy
        new_impl.swap(impl);
      }

      template <typename Arg1, typename Arg2, typename Arg3, typename Arg4, typename Arg5>
      void construct(implementation_type& impl, Arg1 arg1, Arg2 arg2, Arg3 arg3, Arg4 arg4, Arg5 arg5)
      { 
        // Allocate memory and construct the new impl
        implementation_type new_impl(impl_type::create(this->get_io_service(), arg1, arg2, arg3, arg4, arg5));

        // Insert impl into linked list of all implementations.
        register_impl(new_impl);

        // Swap for copy
        new_impl.swap(impl);
      }
      
      void destroy(implementation_type& impl)
      {
        // Remove impl from linked list of all implementations.
        unregister_impl(impl);

        // Terminate all user-defined pending operations.
        close_for_destruction(impl);
      }

      next_layer_type& next_layer(const implementation_type& impl) const
      {
        return impl->next_layer();
      }

      lowest_layer_type& lowest_layer(const implementation_type& impl) const
      {
        return impl->lowest_layer();
      }

      template <typename Handler>
      void async_handshake(implementation_type& impl, Handler handler)
      {        
        impl->async_handshake(handler);
      }

      template <typename Handler>
      void async_shutdown(implementation_type& impl, Handler handler)
      {
        impl->async_shutdown(handler);
      }
      
      template <typename Message, typename Handler>
      void async_write(implementation_type& impl, const Message& message, Handler handler)
      {        
        impl->async_write(message, handler);
      }

      template <typename Message, typename Handler>
      void async_read(implementation_type& impl, Message& message, Handler handler)
      {        
        impl->async_read(message, handler);
      }

      void close(implementation_type& impl, boost::system::error_code& error)
      {
        close(*impl, error);
      }
      
    private:      
      boost::mutex mutex_;
      impl_type* impl_list_;    
    }; // class session_service

    template <typename ActiveSession>
    boost::asio::io_service::id session_service<ActiveSession>::id;

  } // namespace nmea
} // namespace ma

#endif // MA_NMEA_SESSION_SERVICE_HPP