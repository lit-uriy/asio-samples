//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_ECHO_SERVER_SESSION_MANAGER_CONFIG_HPP
#define MA_ECHO_SERVER_SESSION_MANAGER_CONFIG_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <cstddef>
#include <boost/asio.hpp>
#include <ma/echo/server/session_config.hpp>
#include <ma/echo/server/session_manager_config_fwd.hpp>

namespace ma
{    
  namespace echo
  {    
    namespace server
    {                
      struct session_manager_config
      { 
        int         listen_backlog;
        std::size_t max_session_count;
        std::size_t recycled_session_count;
        boost::asio::ip::tcp::endpoint accepting_endpoint;                    
        session_config managed_session_config;

        session_manager_config(
          const boost::asio::ip::tcp::endpoint& the_accepting_endpoint,
          std::size_t the_max_session_count, 
          std::size_t the_recycled_session_count,
          int the_listen_backlog, 
          const session_config& the_managed_session_config);
      }; // struct session_manager_config
        
    } // namespace server
  } // namespace echo
} // namespace ma

#endif // MA_ECHO_SERVER_SESSION_MANAGER_CONFIG_HPP
