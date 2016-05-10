#include <iostream>
#include <string>
#include <tuple>
#include <kapok/Kapok.hpp>
#include <boost/timer.hpp>
#include "router.hpp"
#include "test_router.hpp"
#include "server.hpp"

struct person
{
	int age;
	std::string name;

	META(age, name);
};

int add(int a, int b)
{
	return a + b;
}
void hello()
{
	std::cout << "hello" << std::endl;
}

void test_one(int d)
{
	std::cout << d << std::endl;
}

void foo(std::string b, int a)
{
	std::cout << b << std::endl;
}

void fun(const person& ps)
{
	std::cout << ps.name << std::endl;
}

void fun1(const person& ps, int a)
{
	std::cout << ps.name <<" "<<a<< std::endl;
}

TEST_CASE(asio_test_server, false)
{
	server s(9000, std::thread::hardware_concurrency());
	s.register_handler("fun1", &fun1);
	s.register_handler("fun", &fun);
	s.register_handler("add", &add);
	s.register_handler("about", &hello);
	s.register_handler("foo", &foo);
	s.register_handler("test_one", &test_one);

	s.run();
	getchar();
}

TEST_CASE(asio_test, false)
{
	server s(9000, std::thread::hardware_concurrency());
	s.run();
	getchar();
}
//
//template<typename F, typename Self, int ... Indexes, typename ... Args>
//static auto call_member_helper(const F& f, Self* self, index_sequence<Indexes...>, const std::tuple<Args...>& tup)
//{
//	return (*self.*f)(std::get<Indexes>(tup)...);
//}
//
//template<typename F, typename Self, typename ... Args>
//static typename std::enable_if<is_void<decltype(&Self::declval<F>())>>::value>::type call_member(const F& f, Self* self, const std::tuple<Args...>& tp)
//{
//	call_member_helper(f, self, typename make_index_sequence<sizeof... (Args)>::type(), tp);
//}
//
////std::result_of<decltype(&C::Func)(C, char, int&)>::type
//template<typename F, typename Self, typename ... Args>
//static typename std::enable_if<!is_void<decltype(&Self::declval<F>())>::value>::type call_member(const F& f, Self* self, const std::tuple<Args...>& tp)
//{
//	auto r = call_member_helper(f, self, typename make_index_sequence<sizeof... (Args)>::type(), tp);
//	std::cout << r << std::endl;
//}

struct messager
{
	void foo(int a)
	{
		std::cout << a << std::endl;
	}

	int fun(int a, int b)
	{
		return a + b;
	}
};

TEST_CASE(test_traits, false)
{

}