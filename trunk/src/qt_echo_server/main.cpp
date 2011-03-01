//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <memory>
#include <cstddef>
#include <stdexcept>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/throw_exception.hpp>
#include <boost/function.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <QtCore/QLocale>
#include <QtCore/QTranslator>
#include <QtGui/QApplication>
#include <ma/echo/server/session_config.hpp>
#include <ma/echo/server/session_manager_config.hpp>
#include <ma/echo/server/qt/custommetatypes.h>
#include <ma/echo/server/qt/sessionmanagerwrapper.h>
#include <ma/echo/server/qt/mainform.h>

namespace server = ma::echo::server;

typedef boost::function<void (void)> exception_handler;

struct execution_config
{
  std::size_t session_manager_thread_count;
  std::size_t session_thread_count;
  boost::posix_time::time_duration stop_timeout;

  execution_config(std::size_t the_session_manager_thread_count,
    std::size_t the_session_thread_count, 
    const boost::posix_time::time_duration& the_stop_timeout)
    : session_manager_thread_count(the_session_manager_thread_count)
    , session_thread_count(the_session_thread_count)
    , stop_timeout(the_stop_timeout)
  {
    if (the_session_manager_thread_count < 1)
    {
      boost::throw_exception(std::invalid_argument("session_manager_thread_count must be >= 1"));
    }
    if (the_session_thread_count < 1)
    {
      boost::throw_exception(std::invalid_argument("session_thread_count must be >= 1"));
    }
  }
}; // struct execution_config

void run_io_service(boost::asio::io_service&, exception_handler);

execution_config create_execution_config();

server::session_config create_session_config();

server::session_manager_config create_session_manager_config(const server::session_config&);

namespace
{
  void installTranslation(QCoreApplication& application, const QString& path,
    const QString& localeName, const QString& baseFilename)
  {
    std::auto_ptr<QTranslator> translator(new QTranslator(&application));
    if (translator->load(baseFilename + localeName, path))
    {
      application.installTranslator(translator.get());
      translator.release();
    }
  }

  void setUpTranslation(QApplication& application)
  {
    QString translationPath(QString::fromUtf8(":/ma/echo/server/qt/translation/"));
    QString sysLocaleName(QLocale::system().name());    
    installTranslation(application, translationPath, sysLocaleName, QString::fromUtf8("qt_"));
    installTranslation(application, translationPath, sysLocaleName, QString::fromUtf8("qt_echo_server_"));
  }

} // namespace

int main(int argc, char* argv[])
{
  QApplication application(argc, argv);

#pragma warning(push)
#pragma warning(disable: 4127)
  Q_INIT_RESOURCE(qt_echo_server);
#pragma warning(pop)
  
  setUpTranslation(application);

  ma::echo::server::qt::registerCustomMetaTypes();

  //todo: temporary
  execution_config the_execution_config = create_execution_config();
  server::session_config the_session_config = create_session_config();
  server::session_manager_config the_session_manager_config = 
    create_session_manager_config(the_session_config);  
  boost::asio::io_service session_io_service(the_execution_config.session_thread_count);        
  boost::asio::io_service session_manager_io_service(the_execution_config.session_manager_thread_count);  
  server::qt::SessionManagerWrapper sessionManager(
    session_manager_io_service, session_io_service, the_session_manager_config);

  // Create and explicitly show main window
  server::qt::MainForm mainForm;
  mainForm.show();

  // Run application event loop
  return application.exec();
}

void run_io_service(boost::asio::io_service& io_service, 
  exception_handler the_exception_handler)
{
  try 
  {
    io_service.run();
  }
  catch (...)
  {
    the_exception_handler();
  }
}

execution_config create_execution_config()
{
  //todo  
  return execution_config(2, 4, boost::posix_time::seconds(60));
}

server::session_config create_session_config()
{      
  return server::session_config(4048, 
    boost::optional<int>(), boost::optional<int>(),
    boost::optional<bool>());
} // create_session_config

server::session_manager_config create_session_manager_config(
  const server::session_config& session_config)
{      
  using boost::asio::ip::tcp;
  return server::session_manager_config(tcp::endpoint(tcp::v4(), 7), 
    10000, 1024, 7, session_config);
} // create_session_manager_config
