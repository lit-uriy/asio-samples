//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/make_shared.hpp>
#include <ma/echo/server2/session.h>
#include <ma/echo/server2/session_proxy.h>

namespace ma
{    
  namespace echo
  {
    namespace server2
    {
      session_proxy::session_proxy(boost::asio::io_service& io_service,
        const session::settings& session_settings)
        : session_(boost::make_shared<session>(boost::ref(io_service), session_settings))
        , pending_operations_(0)
        , state_(ready_to_start)
      {
      } // session_proxy::session_proxy

      session_proxy::~session_proxy()
      {
      } // session_manager::session_proxy::~session_proxy

    } // namespace server2
  } // namespace echo
} // namespace ma