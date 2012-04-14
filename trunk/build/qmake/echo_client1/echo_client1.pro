#
# Copyright (c) 2010-2012 Marat Abrarov (abrarov@mail.ru)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

TEMPLATE =  app
QT       -= core gui
TARGET   =  echo_client1
CONFIG   += console thread
CONFIG   -= app_bundle

HEADERS  += ../../../include/ma/handler_storage_service.hpp \
            ../../../include/ma/handler_storage.hpp \
            ../../../include/ma/handler_invoke_helpers.hpp \
            ../../../include/ma/bind_asio_handler.hpp \
            ../../../include/ma/context_alloc_handler.hpp \
            ../../../include/ma/context_wrapped_handler.hpp \
            ../../../include/ma/custom_alloc_handler.hpp \
            ../../../include/ma/strand_wrapped_handler.hpp \
            ../../../include/ma/handler_allocator.hpp \
            ../../../include/ma/handler_alloc_helpers.hpp \
            ../../../include/ma/console_controller.hpp \
            ../../../include/ma/config.hpp \
            ../../../include/ma/type_traits.hpp \
            ../../../include/ma/echo/client1/session_options_fwd.hpp \
            ../../../include/ma/echo/client1/session_options.hpp \
            ../../../include/ma/echo/client1/session_fwd.hpp \
            ../../../include/ma/echo/client1/session.hpp \
            ../../../include/ma/echo/client1/error.hpp

SOURCES  += ../../../src/ma/console_controller.cpp \
            ../../../src/echo_client1/main.cpp \
            ../../../src/ma/echo/client1/error.cpp \
            ../../../src/ma/echo/client1/session.cpp

win32:INCLUDEPATH += ../../../../boost_1_49_0
INCLUDEPATH       += ../../../include

win32:LIBS += -L../../../../boost_1_49_0/lib/x86
unix:LIBS  += -lboost_thread \
              -lboost_system \
              -lboost_date_time \
              -lboost_program_options

win32:DEFINES += WIN32_LEAN_AND_MEAN _UNICODE UNICODE

linux-g++ | linux-g++-64 {
  QMAKE_CXXFLAGS += -std=c++0x -Wstrict-aliasing
}
