#include <iostream>
#include <string>
#include <tuple>
#include <kapok/Kapok.hpp>
#include <boost/timer.hpp>
#include "router.hpp"
#include "client_proxy.hpp"
#include "test_router.hpp"
#include "server.hpp"

struct person
{
	int age;
	std::string name;

	META(age, name);
};

void add(int a, int b)
{
//	std::cout << a + b << std::endl;
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
//	std::cout << ps.name << std::endl;
}

void fun1(const person& ps, int a)
{
	//std::cout << ps.name << std::endl;
}

struct messager
{
	void fun(int a)
	{
		std::cout << a << std::endl;
	}
};

TEST_CASE(asio)
{
	server s(9000, std::thread::hardware_concurrency());
	s.run();
	getchar();
}

TEST_CASE(example)
{
	using namespace std;
	
	person _person = { 20, "aa" };

	router& r = router::get();
	//设置handler
	r.register_handler("fun1", &fun1);

	r.register_handler("fun", &fun);
	r.register_handler("add", &add);
	r.register_handler("about", &hello);
	r.register_handler("foo", &foo);
	r.register_handler("test_one", &test_one);

	messager m;
	r.register_handler("msg", &messager::fun, &m);
	
	
	try
	{
		//发起请求
		client_proxy client(r);
		//client.call("about");
		//client.call("test_one", 2);
		client.call("msg", 1);
		person p = { 20, "aa" };
		const int len = 1;// 1000000;
		boost::timer timer;
		for (size_t i = 0; i < len; i++)
		{
			client.call("add", 1,2);
		}
		cout << timer.elapsed() << endl;
		client.call("fun1", p, 1);

		client.call("foo", "test", 1);
	}
	catch (std::runtime_error &error)
	{
		std::cerr << error.what() << std::endl;
	}
}

