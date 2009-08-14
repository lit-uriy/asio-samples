//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <tchar.h>
#include <windows.h>
#include <iostream>
#include <boost/smart_ptr.hpp>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <ma/handler_allocation.hpp>
#include <ma/echo/server.hpp>
#include <console_controller.hpp>

struct server_proxy_type;
typedef boost::shared_ptr<server_proxy_type> server_proxy_ptr;
typedef boost::function<void (void)> exception_handler;

struct server_proxy_type : private boost::noncopyable
{  
  enum state_type
  {
    ready_to_start,
    start_in_progress,
    started,
    stop_in_progress,
    stopped
  };

  boost::mutex mutex_;  
  ma::echo::server_ptr server_;
  state_type state_;
  bool stopped_by_program_exit_;
  boost::condition_variable state_changed_;  
  ma::in_place_handler_allocator<256> start_wait_allocator_;
  ma::in_place_handler_allocator<256> stop_allocator_;

  explicit server_proxy_type(boost::asio::io_service& io_service,
    boost::asio::io_service& session_io_service,
    ma::echo::server::settings settings)
    : server_(new ma::echo::server(io_service, session_io_service, settings))    
    , state_(ready_to_start)
    , stopped_by_program_exit_(false)
  {
  }

  ~server_proxy_type()
  {
  }
}; // server_proxy_type

const boost::posix_time::time_duration stop_timeout = boost::posix_time::seconds(60);

void start_server(const server_proxy_ptr&);

void wait_server(const server_proxy_ptr&);

void stop_server(const server_proxy_ptr&);

void run_io_service(boost::asio::io_service&, exception_handler);

void handle_work_exception(const server_proxy_ptr&);

void handle_program_exit(const server_proxy_ptr&);

void server_started(const server_proxy_ptr&,
  const boost::system::error_code&);

void server_has_to_stop(const server_proxy_ptr&,
  const boost::system::error_code&);

void server_stopped(const server_proxy_ptr&,
  const boost::system::error_code&);

int _tmain(int argc, _TCHAR* argv[])
{ 
  boost::program_options::options_description options_description("Allowed options");
  options_description.add_options()
    ("help", "produce help message")
    ("port", boost::program_options::value<unsigned short>(), "set port")
    ("max_sessions", 
      boost::program_options::value<std::size_t>()->default_value(10000), 
      "set maximum number of active sessions")
    ("recycled_sessions", 
      boost::program_options::value<std::size_t>()->default_value(100), 
      "set maximum number of inactive sessions used for new sessions' acceptation")
    ("listen_backlog", 
      boost::program_options::value<int>()->default_value(6), 
      "set TCP listen backlog")
    ("buffer_size", 
      boost::program_options::value<std::size_t>()->default_value(1024));    

  int exit_code = EXIT_SUCCESS;
  try 
  {
    boost::program_options::variables_map options_values;  
    boost::program_options::store(
      boost::program_options::parse_command_line(argc, argv, options_description), 
      options_values);
    boost::program_options::notify(options_values);

    if (options_values.count("help"))
    {
      std::cout << options_description;
    }
    else if (!options_values.count("port"))
    {    
      exit_code = EXIT_FAILURE;
      std::cout << "Port not set.\n" << options_description;
    }
    else
    {        
      std::size_t cpu_count = boost::thread::hardware_concurrency();
      std::size_t session_thread_count = cpu_count ? cpu_count : 2;
      std::size_t session_manager_thread_count = 1;

      unsigned short listen_port = options_values["port"].as<unsigned short>();
      boost::asio::ip::tcp::endpoint listen_endpoint(boost::asio::ip::tcp::v4(), listen_port);

      ma::echo::server::settings server_settings(
        listen_endpoint,
        options_values["max_sessions"].as<std::size_t>(),
        options_values["recycled_sessions"].as<std::size_t>(),
        options_values["listen_backlog"].as<int>(),
        ma::echo::session::settings(options_values["buffer_size"].as<std::size_t>()));      

      std::cout << "Number of found CPU(s)             : " << cpu_count << '\n'               
                << "Number of session manager's threads: " << session_manager_thread_count << '\n'
                << "Number of sessions' threads        : " << session_thread_count << '\n' 
                << "Total number of work threads       : " << session_thread_count + session_manager_thread_count << '\n'
                << "Server listen port                 : " << listen_port << '\n'
                << "Maximum number of active sessions  : " << server_settings.max_sessions_ << '\n'
                << "Maximum number of recycled sessions: " << server_settings.recycled_sessions_ << '\n'
                << "TCP listen backlog                 : " << server_settings.listen_backlog_ << '\n';
      
      // Before server_io_service
      boost::asio::io_service session_io_service;
      // ... for the right destruction order
      boost::asio::io_service server_io_service;

      // Create server
      server_proxy_ptr server_proxy(
        new server_proxy_type(server_io_service, session_io_service, server_settings));
      
      std::cout << "Server is starting.\n";
      
      // Start the server
      boost::unique_lock<boost::mutex> server_proxy_lock(server_proxy->mutex_);    
      start_server(server_proxy);
      server_proxy_lock.unlock();
      
      // Setup console controller
      ma::console_controller console_controller(
        boost::bind(handle_program_exit, server_proxy));

      std::cout << "Press Ctrl+C (Ctrl+Break) to exit.\n";

      // Exception handler for automatic server aborting
      exception_handler work_exception_handler(
        boost::bind(handle_work_exception, server_proxy));

      // Create work for sessions' io_service to prevent threads' stop
      boost::asio::io_service::work session_work(session_io_service);
      // Create work for server's io_service to prevent threads' stop
      boost::asio::io_service::work server_work(server_io_service);    

      // Create work threads for session operations
      boost::thread_group work_threads;    
      for (std::size_t i = 0; i != session_thread_count; ++i)
      {
        work_threads.create_thread(
          boost::bind(run_io_service, boost::ref(session_io_service), 
            work_exception_handler));
      }
      // Create work threads for server operations
      for (std::size_t i = 0; i != session_manager_thread_count; ++i)
      {
        work_threads.create_thread(
          boost::bind(run_io_service, boost::ref(server_io_service), 
            work_exception_handler));      
      }

      // Wait until server stops
      server_proxy_lock.lock();
      while (server_proxy_type::stop_in_progress != server_proxy->state_ 
        && server_proxy_type::stopped != server_proxy->state_)
      {
        server_proxy->state_changed_.wait(server_proxy_lock);
      }
      if (server_proxy_type::stopped != server_proxy->state_)
      {
        if (!server_proxy->state_changed_.timed_wait(server_proxy_lock, stop_timeout))      
        {
          std::cout << "Server stop timeout expiration. Terminating server work.\n";
          exit_code = EXIT_FAILURE;
        }
        server_proxy->state_ = server_proxy_type::stopped;
      }
      if (!server_proxy->stopped_by_program_exit_)
      {
        exit_code = EXIT_FAILURE;
      }
      server_proxy_lock.unlock();
          
      server_io_service.stop();
      session_io_service.stop();

      std::cout << "Waiting until all of the work threads will stop.\n";
      work_threads.join_all();
      std::cout << "Work threads have stopped. Process will close.\n";    
    }
  }
  catch (const boost::program_options::error&)
  {
    exit_code = EXIT_FAILURE;
    std::cout << "Invalid options.\n" << options_description;      
  }  
  return exit_code;
}

void start_server(const server_proxy_ptr& server_proxy)
{
  server_proxy->server_->async_start
  (
    ma::make_custom_alloc_handler
    (
      server_proxy->start_wait_allocator_,
      boost::bind(server_started, server_proxy, _1)
    )
  );
  server_proxy->state_ = server_proxy_type::start_in_progress;
}

void wait_server(const server_proxy_ptr& server_proxy)
{
  server_proxy->server_->async_wait
  (
    ma::make_custom_alloc_handler
    (
      server_proxy->start_wait_allocator_,
      boost::bind(server_has_to_stop, server_proxy, _1)
    )
  );
}

void stop_server(const server_proxy_ptr& server_proxy)
{
  server_proxy->server_->async_stop
  (
    ma::make_custom_alloc_handler
    (
      server_proxy->stop_allocator_,
      boost::bind(server_stopped, server_proxy, _1)
    )
  );
  server_proxy->state_ = server_proxy_type::stop_in_progress;
}

void run_io_service(boost::asio::io_service& io_service, 
  exception_handler io_service_exception_handler)
{
  try 
  {
    io_service.run();
  }
  catch (...)
  {
    io_service_exception_handler();
  }
}

void handle_work_exception(const server_proxy_ptr& server_proxy)
{
  boost::unique_lock<boost::mutex> server_proxy_lock(server_proxy->mutex_);  
  server_proxy->state_ = server_proxy_type::stopped;  
  std::cout << "Terminating server work due to unexpected exception.\n";
  server_proxy_lock.unlock();
  server_proxy->state_changed_.notify_one();      
}

void handle_program_exit(const server_proxy_ptr& server_proxy)
{
  boost::unique_lock<boost::mutex> server_proxy_lock(server_proxy->mutex_);
  std::cout << "Program exit request detected.\n";
  if (server_proxy_type::stopped == server_proxy->state_)
  {    
    std::cout << "Server has already stopped.\n";
  }
  else if (server_proxy_type::stop_in_progress == server_proxy->state_)
  {    
    server_proxy->state_ = server_proxy_type::stopped;
    std::cout << "Server is already stopping. Terminating server work.\n";
    server_proxy_lock.unlock();
    server_proxy->state_changed_.notify_one();       
  }
  else
  {    
    // Start server stop
    stop_server(server_proxy);
    server_proxy->stopped_by_program_exit_ = true;
    std::cout << "Server is stopping. Press Ctrl+C (Ctrl+Break) to terminate server work.\n";
    server_proxy_lock.unlock();    
    server_proxy->state_changed_.notify_one();
  }  
}

void server_started(const server_proxy_ptr& server_proxy,
  const boost::system::error_code& error)
{   
  boost::unique_lock<boost::mutex> server_proxy_lock(server_proxy->mutex_);
  if (server_proxy_type::start_in_progress == server_proxy->state_)
  {   
    if (error)
    {       
      server_proxy->state_ = server_proxy_type::stopped;
      std::cout << "Server can't start due to error.\n";
      server_proxy_lock.unlock();
      server_proxy->state_changed_.notify_one();
    }
    else
    {
      server_proxy->state_ = server_proxy_type::started;
      wait_server(server_proxy);
      std::cout << "Server has started.\n";
    }
  }
}

void server_has_to_stop(const server_proxy_ptr& server_proxy,
  const boost::system::error_code&)
{
  boost::unique_lock<boost::mutex> server_proxy_lock(server_proxy->mutex_);
  if (server_proxy_type::started == server_proxy->state_)
  {
    stop_server(server_proxy);
    std::cout << "Server can't continue work due to error. Server is stopping.\n";
    server_proxy_lock.unlock();
    server_proxy->state_changed_.notify_one();    
  }
}

void server_stopped(const server_proxy_ptr& server_proxy,
  const boost::system::error_code&)
{
  boost::unique_lock<boost::mutex> server_proxy_lock(server_proxy->mutex_);
  if (server_proxy_type::stop_in_progress == server_proxy->state_)
  {      
    server_proxy->state_ = server_proxy_type::stopped;
    std::cout << "Server has stopped.\n";    
    server_proxy_lock.unlock();
    server_proxy->state_changed_.notify_one();
  }
}