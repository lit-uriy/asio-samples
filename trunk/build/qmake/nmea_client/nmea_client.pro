QT       -= core gui
TARGET   =  nmea_client
CONFIG   += console
CONFIG   -= app_bundle
TEMPLATE =  app

linux-g++ {
  QMAKE_CXXFLAGS += -std=c++0x
}

SOURCES  += ../../../src/ma/console_controller.cpp \
            ../../../src/nmea_client/main.cpp \
            ../../../src/ma/nmea/error.cpp \
            ../../../src/ma/nmea/cyclic_read_session.cpp

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
            ../../../include/ma/codecvt_cast.hpp \
            ../../../include/ma/console_controller.hpp \
            ../../../include/ma/nmea/frame.hpp \
            ../../../include/ma/nmea/error.hpp \
            ../../../include/ma/config.hpp \
            ../../../include/ma/type_traits.hpp \
            ../../../include/ma/nmea/cyclic_read_session_fwd.hpp \
            ../../../include/ma/nmea/cyclic_read_session.hpp

win32 {
  LIBS      += -L../../../../boost_1_46_0/lib/x86
}

unix {
  LIBS      += /usr/local/lib/libboost_thread.a \
               /usr/local/lib/libboost_system.a \
               /usr/local/lib/libboost_date_time.a \
}

win32 {
  INCLUDEPATH += ../../../../boost_1_46_0
}

unix {
  INCLUDEPATH += /usr/local/include
}

INCLUDEPATH += ../../../include

win32 {
  DEFINES   += WIN32_LEAN_AND_MEAN _UNICODE UNICODE
}
