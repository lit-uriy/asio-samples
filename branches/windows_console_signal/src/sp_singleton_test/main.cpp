//
// Copyright (c) 2010-2013 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#if defined(WIN32)
#include <tchar.h>
#include <windows.h>
#endif

#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <vector>
#include <boost/assert.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/noncopyable.hpp>
#include <ma/shared_ptr_factory.hpp>
#include <ma/detail/sp_singleton.hpp>

namespace ma {
namespace test {

namespace sp_singleton_construction {

void run_test();

} // namespace sp_singleton_construction

namespace sp_singleton_destruction {

void run_test();

} // namespace sp_singleton_destruction

} // namespace test
} // namespace ma

#if defined(WIN32)
int _tmain(int /*argc*/, _TCHAR* /*argv*/[])
#else
int main(int /*argc*/, char* /*argv*/[])
#endif
{
  try
  {
    ma::test::sp_singleton_construction::run_test();
    //ma::test::sp_singleton_destruction::run_test();
    return EXIT_SUCCESS;
  }
  catch (const std::exception& e)
  {
    std::cerr << "Unexpected exception: " << e.what() << std::endl;
  }
  catch (...)
  {
    std::cerr << "Unknown exception" << std::endl;
  }
  return EXIT_FAILURE;
}

namespace ma {
namespace test {

namespace sp_singleton_construction {

class foo;
typedef boost::shared_ptr<foo> foo_ptr;

class foo : private boost::noncopyable
{
private:
  typedef foo this_type;

public:
  static foo_ptr get_nullable_instance();
  static foo_ptr get_instance();

  int data() const;

protected:
  typedef detail::sp_singleton<this_type>::instance_guard instance_guard_type;

  foo(const instance_guard_type&, int);
  ~foo();

private:
  instance_guard_type instance_guard_;
  int data_;
}; //class test_singleton

void run_test()
{
  std::vector<int> data;  
  for (std::size_t i = 0; i != 2; ++i)
  {
    foo_ptr foo0 = foo::get_nullable_instance();
    BOOST_ASSERT_MSG(!foo0, "Instance has to not exist");
    const foo_ptr foo1 = foo::get_instance();
    const foo_ptr foo2 = foo::get_instance();
    BOOST_ASSERT_MSG(foo1->data() == foo2->data(), "Instances are different");
    foo0 = foo::get_nullable_instance();
    BOOST_ASSERT_MSG(foo0, "Instance has to exist");  
    BOOST_ASSERT_MSG(foo0->data() == foo2->data(), "Instances are different");
    data.push_back(foo0->data());
  }
  BOOST_ASSERT_MSG(data[0] != data[1], "Instances has to be different");
}

foo_ptr foo::get_nullable_instance()
{
  return detail::sp_singleton<foo>::get_nullable_instance();
}

foo_ptr foo::get_instance()
{
  class factory
  {  
  public:
    foo_ptr operator()(const instance_guard_type& instance_guard)
    {
      typedef ma::shared_ptr_factory_helper<foo> helper;
      static int data = 0;
      return boost::make_shared<helper>(instance_guard, data++);
    }
  };
  return detail::sp_singleton<foo>::get_instance(factory());
}

int foo::data() const
{
  return data_;
}

foo::foo(const instance_guard_type& instance_guard, int data)
  : instance_guard_(instance_guard)
  , data_(data)
{
}

foo::~foo()
{
}

} // namespace sp_singleton_construction

namespace sp_singleton_destruction {

class foo;
typedef boost::shared_ptr<foo> foo_ptr;

class foo : private boost::noncopyable
{
private:
  typedef foo this_type;

public:
  static foo_ptr get_instance();

  int data() const;

protected:
  typedef detail::sp_singleton<this_type>::instance_guard instance_guard_type;

  foo(const instance_guard_type&, int);
  ~foo();

private:
  instance_guard_type instance_guard_;
  int data_;
}; //class test_singleton

void run_test()
{
  foo_ptr foo1 = foo::get_instance();
  foo_ptr foo2 = foo::get_instance();
  BOOST_ASSERT_MSG(foo1->data() == foo2->data(), "Instances are different");  
}

foo_ptr foo::get_instance()
{
  class factory
  {
  public:
    foo_ptr operator()(const instance_guard_type& instance_guard)
    {
      typedef ma::shared_ptr_factory_helper<foo> helper;
      static int data = 0;
      return boost::make_shared<helper>(instance_guard, data++);
    }
  };
  return detail::sp_singleton<foo>::get_instance(factory());
}

int foo::data() const
{
  return data_;
}

foo::foo(const instance_guard_type& instance_guard, int data)
  : instance_guard_(instance_guard)
  , data_(data)
{
}

foo::~foo()
{
}

} // namespace sp_singleton_destruction

} // namespace test
} // namespace ma
