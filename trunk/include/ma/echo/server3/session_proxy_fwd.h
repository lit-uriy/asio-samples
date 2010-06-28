//
// Copyright (c) 2010 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_ECHO_SERVER3_SESSION_PROXY_FWD_H
#define MA_ECHO_SERVER3_SESSION_PROXY_FWD_H

#include <boost/smart_ptr.hpp>

namespace ma
{    
  namespace echo
  {
    namespace server3
    {    
      class session_proxy;
      typedef boost::shared_ptr<session_proxy> session_proxy_ptr;
      typedef boost::weak_ptr<session_proxy>   session_proxy_weak_ptr;

    } // namespace server3
  } // namespace echo
} // namespace ma

#endif // MA_ECHO_SERVER3_SESSION_PROXY_FWD_H
