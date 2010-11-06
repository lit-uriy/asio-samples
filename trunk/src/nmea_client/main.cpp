//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <tchar.h>
#include <windows.h>
#include <cstdlib>
#include <locale>
#include <iostream>
#include <utility>
#include <vector>
#include <boost/make_shared.hpp>
#include <boost/ref.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>
#include <boost/lexical_cast.hpp>
#include <ma/codecvt_cast.hpp>
#include <ma/handler_allocation.hpp>
#include <ma/nmea/frame.hpp>
#include <ma/nmea/cyclic_read_session.hpp>
#include <ma/console_controller.hpp>

typedef std::codecvt<wchar_t, char, mbstate_t> wcodecvt_type;
typedef ma::nmea::cyclic_read_session        session;
typedef ma::nmea::cyclic_read_session_ptr    session_ptr;
typedef ma::nmea::frame_ptr                  frame_ptr;
typedef ma::in_place_handler_allocator<128>  handler_allocator_type;
typedef std::vector<frame_ptr>               frame_buffer_type;
typedef boost::shared_ptr<frame_buffer_type> frame_buffer_ptr;

void handle_start(const session_ptr& the_session, handler_allocator_type& the_allocator, 
  const frame_buffer_ptr& frame_buffer, const boost::system::error_code& error);

void handle_stop(const boost::system::error_code& error);

void handle_read(const session_ptr& the_session, handler_allocator_type& the_allocator, 
  const frame_buffer_ptr& frame_buffer, const boost::system::error_code& error, 
  std::size_t frames_transferred);

void handle_console_close(const session_ptr&);

void print_frames(const frame_buffer_type& frames, std::size_t size);

void print_usage(int argc, _TCHAR* argv[]);

int _tmain(int argc, _TCHAR* argv[])
{
  try
  {  
    std::locale console_locale("Russian_Russia.866");
    std::locale sys_locale("");
    std::wcout.imbue(console_locale);
    std::wcerr.imbue(console_locale);

    if (2 > argc || 4 < argc)
    {
      print_usage(argc, argv);
      return EXIT_FAILURE;
    }

    std::size_t cpu_num = boost::thread::hardware_concurrency();
    std::size_t concurrent_num = 2 > cpu_num ? 2 : cpu_num;
    std::size_t thread_num = 2;

    std::wcout << L"Number of found CPUs             : " << cpu_num        << std::endl
               << L"Number of concurrent work threads: " << concurrent_num << std::endl
               << L"Total number of work threads     : " << thread_num     << std::endl;

    std::wstring device_name(argv[1]);
    std::size_t read_buffer_size = std::max<std::size_t>(1024, session::min_read_buffer_size);
    std::size_t message_queue_size = std::max<std::size_t>(64, session::min_message_queue_size);

    if (argc > 2)
    {
      try 
      {      
        read_buffer_size = boost::lexical_cast<std::size_t>(argv[2]);
        if (3 < argc)
        {
          message_queue_size = boost::lexical_cast<std::size_t>(argv[3]);
        }
      }
      catch (const boost::bad_lexical_cast& e)
      {
        std::cerr << L"Invalid parameter value/format: " << e.what() << std::endl;
        print_usage(argc, argv);
        return EXIT_FAILURE;
      }
      catch (const std::exception& e)
      {    
        std::cerr << "Unexpected error during parameters parsing: " << e.what() << std::endl;
        print_usage(argc, argv);
        return EXIT_FAILURE;
      } 
    } // if (argc > 2)

    std::wcout << L"NMEA 0183 device serial port: " << device_name        << std::endl
               << L"Read buffer size (bytes)    : " << read_buffer_size   << std::endl
               << L"Read buffer size (messages) : " << message_queue_size << std::endl;  

    const wcodecvt_type& wcodecvt(std::use_facet<wcodecvt_type>(sys_locale));
    std::string ansi_device_name(ma::codecvt_cast::out(device_name, wcodecvt));
    handler_allocator_type the_allocator;
    frame_buffer_ptr the_frame_buffer(boost::make_shared<frame_buffer_type>(message_queue_size));
            
    // An io_service for the thread pool (for the executors... Java Executors API? Apache MINA :)
    boost::asio::io_service session_io_service(concurrent_num);   
    session_ptr the_session(boost::make_shared<session>(boost::ref(session_io_service), 
      read_buffer_size, message_queue_size, "$", "\x0a"));

    // Prepare the lower layer - open the serial port  
    boost::system::error_code error;
    the_session->serial_port().open(ansi_device_name, error);
    if (error)
    {
      std::wstring error_message = ma::codecvt_cast::in(error.message(), wcodecvt);
      std::wcerr << L"Failed to open serial port: " << error_message << std::endl;
      return EXIT_FAILURE;
    }

    // Start session (not actually, because there are no work threads yet)
    the_session->async_start(ma::make_custom_alloc_handler(the_allocator, 
      boost::bind(&handle_start, the_session, boost::ref(the_allocator), the_frame_buffer, _1)));    

    // Setup console controller
    ma::console_controller console_controller(boost::bind(&handle_console_close, the_session));        
    std::cout << "Press Ctrl+C (Ctrl+Break) to exit...\n";

    // Create work threads
    boost::thread_group work_threads;
    for (std::size_t i = 0; i != thread_num; ++i)
    {
      work_threads.create_thread(boost::bind(&boost::asio::io_service::run, &session_io_service));
    }
    work_threads.join_all();

    std::cout << "Work threads are stopped.\n";      
    return EXIT_SUCCESS;
  }
  catch (const std::exception& e)
  {
    std::cerr << "Unexpected error: " << e.what() << std::endl;    
  }
  return EXIT_FAILURE;
}

void handle_console_close(const session_ptr& session)
{
  std::cout << "User console close detected. Begin stop the session...\n";
  session->async_stop(boost::bind(&handle_stop, _1));  
}

void handle_start(const session_ptr& the_session, handler_allocator_type& the_allocator, 
  const frame_buffer_ptr& frame_buffer, const boost::system::error_code& error)                  
{  
  if (error)
  {
    std::cout << "Start unsuccessful. The error is: " << error.message() << '\n'; 
    return;
  }  
  std::cout << "Session started successfully. Begin read...\n";      
  the_session->async_read_some(frame_buffer->begin(), frame_buffer->end(), ma::make_custom_alloc_handler(the_allocator, 
    boost::bind(&handle_read, the_session, boost::ref(the_allocator), frame_buffer, _1, _2)));
}

void handle_stop(const boost::system::error_code& error)
{   
  if (error)
  {
    std::cout << "The session stop was unsuccessful. The error is: " << error.message() << '\n';
    return;
  }
  std::cout << "The session stop was successful.\n";  
}

void handle_read(const session_ptr& the_session, handler_allocator_type& the_allocator, 
  const frame_buffer_ptr& frame_buffer, const boost::system::error_code& error, 
  std::size_t frames_transferred)
{  
  print_frames(*frame_buffer, frames_transferred);
  if (boost::asio::error::eof == error)
  {
    std::cout << "Input stream was closed. But it\'s a serial port so begin read operation again...\n";
    the_session->async_read_some(frame_buffer->begin(), frame_buffer->end(), ma::make_custom_alloc_handler(the_allocator, 
      boost::bind(&handle_read, the_session, boost::ref(the_allocator), frame_buffer, _1, _2)));
    return;
  }
  if (error)  
  {      
    std::cout << "Read unsuccessful. Begin the session stop...\n";
    the_session->async_stop(ma::make_custom_alloc_handler(the_allocator, boost::bind(&handle_stop, _1)));
    return;
  }
  the_session->async_read_some(frame_buffer->begin(), frame_buffer->end(), ma::make_custom_alloc_handler(the_allocator, 
    boost::bind(&handle_read, the_session, boost::ref(the_allocator), frame_buffer, _1, _2)));
}

void print_frames(const frame_buffer_type& frames, std::size_t size)
{  
  for (frame_buffer_type::const_iterator iterator = frames.begin(); size; ++iterator, --size)
  {
    std::cout << *(*iterator) << '\n';
  }    
}

void print_usage(int argc, _TCHAR* argv[])
{
  std::wstring file_name;
  if (argc > 0)
  {
    boost::filesystem::wpath app_path(argv[0]);
    file_name = app_path.leaf();
  }
  else
  {
    file_name = L"nmea_client.exe";
  }
  std::wcout << L"Usage: \"" << file_name << L"\" <com_port> [<read_buffer_size> [<message_queue_size>] ]" << std::endl;  
}