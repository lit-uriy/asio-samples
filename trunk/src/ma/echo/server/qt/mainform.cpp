/*
TRANSLATOR ma::echo::server::qt::MainForm
*/

//
// Copyright (c) 2010-2011 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <QTextCursor>
#include <ma/echo/server/error.hpp>
#include <ma/echo/server/session_config.hpp>
#include <ma/echo/server/session_manager_config.hpp>
#include <ma/echo/server/qt/service.h>
#include <ma/echo/server/qt/signal_connect_error.h>
#include <ma/echo/server/qt/mainform.h>

namespace ma
{    
namespace echo
{
namespace server
{    
namespace qt 
{
namespace
{
  execution_config createExecutionConfig()
  {
    return execution_config(2, 2);
  }

  session_config createSessionConfig()
  {
    return session_config(4096);
  }

  session_manager_config createSessionManagerConfig(const session_config& sessionConfig)
  {
    using boost::asio::ip::tcp;

    unsigned short port = 7;
    std::size_t maxSessions = 1000;
    std::size_t recycledSessions = 100;
    int listenBacklog = 6;
    return session_manager_config(tcp::endpoint(tcp::v4(), port),
      maxSessions, recycledSessions, listenBacklog, sessionConfig);
  }
} // anonymous namespace

  MainForm::MainForm(Service& echoService, QWidget* parent, Qt::WFlags flags)
    : QDialog(parent, flags | Qt::WindowMinMaxButtonsHint)
    , echoService_(echoService)
  {
    ui_.setupUi(this);

    checkConnect(QObject::connect(&echoService, 
      SIGNAL(exceptionHappened()), 
      SLOT(on_echoService_exceptionHappened())));
    checkConnect(QObject::connect(&echoService, 
      SIGNAL(startCompleted(const boost::system::error_code&)), 
      SLOT(on_echoService_startCompleted(const boost::system::error_code&))));
    checkConnect(QObject::connect(&echoService, 
      SIGNAL(stopCompleted(const boost::system::error_code&)), 
      SLOT(on_echoService_stopCompleted(const boost::system::error_code&))));
    checkConnect(QObject::connect(&echoService, 
      SIGNAL(workCompleted(const boost::system::error_code&)), 
      SLOT(on_echoService_workCompleted(const boost::system::error_code&))));
    //todo: setup initial internal states    
    //todo: setup initial widgets' states
  }

  MainForm::~MainForm()
  {
  }

  void MainForm::on_startButton_clicked()
  {    
    //todo: read and validate configuration
    execution_config executionConfig = createExecutionConfig();    
    session_manager_config sessionManagerConfig = createSessionManagerConfig(createSessionConfig());
    
    echoService_.asyncStart(executionConfig, sessionManagerConfig);
    writeLog(QString::fromUtf8("Starting echo service..."));
  }

  void MainForm::on_stopButton_clicked()
  {        
    echoService_.asyncStop();
    writeLog(QString::fromUtf8("Stopping echo service..."));
  }

  void MainForm::on_terminateButton_clicked()
  {
    writeLog(QString::fromUtf8("Terminating echo service..."));    
    echoService_.terminate();
    writeLog(QString::fromUtf8("Echo service terminated"));
  }

  void MainForm::on_echoService_startCompleted(const boost::system::error_code& error)
  {    
    if (error)
    {
      writeLog(QString::fromUtf8("Echo service start completed with error"));
    }
    else 
    {
      writeLog(QString::fromUtf8("Echo service start completed successfully"));
    }        
  }
          
  void MainForm::on_echoService_stopCompleted(const boost::system::error_code& error)
  {    
    if (error)
    {
      writeLog(QString::fromUtf8("Echo service stop completed with error"));
    }
    else 
    {
      writeLog(QString::fromUtf8("Echo service stop completed successfully"));
    }    
  }

  void MainForm::on_echoService_workCompleted(const boost::system::error_code& error)
  {
    bool stopCause = server_error::operation_aborted == error;    
    if (!stopCause && error)
    {
      writeLog(QString::fromUtf8("Echo service work completed with error"));
    }
    else 
    {
      writeLog(QString::fromUtf8("Echo service work completed successfully"));
    }    
    if (!stopCause)
    {
      echoService_.asyncStop(); 
      writeLog(QString::fromUtf8("Stopping echo service..."));
    }
  }

  void MainForm::on_echoService_exceptionHappened()
  {
    writeLog(QString::fromUtf8("Unexpected error during echo service work. Terminating echo service..."));
    echoService_.terminate(); 
    writeLog(QString::fromUtf8("Echo service terminated"));
  }

  void MainForm::writeLog(const QString& message)
  {
    ui_.logTextEdit->appendPlainText(message);
    ui_.logTextEdit->moveCursor(QTextCursor::End);
  }

} // namespace qt
} // namespace server
} // namespace echo
} // namespace ma
