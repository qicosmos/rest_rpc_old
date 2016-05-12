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

int fun1(const person& ps, int a)
{
	std::cout << ps.name <<" "<<a<< std::endl;
	return a;
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

TEST_CASE(rpc_qps, true)
{
	server s(9000, 1/*std::thread::hardware_concurrency()*/);
	s.register_handler("add", &add);;
	s.run();
	getchar();

	std::uint64_t last_succeed_count = 0;

	while (true)
	{
		auto curr_succeed_count = (std::uint64_t)g_succeed_count;
		std::cout << curr_succeed_count - last_succeed_count << std::endl;
		last_succeed_count = curr_succeed_count;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}
}