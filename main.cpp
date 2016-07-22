#include <iostream>
#include <string>
#include <tuple>
#include <fstream>
#include <kapok/Kapok.hpp>
#include <boost/timer.hpp>
#include "router.hpp"
#include "test_router.hpp"
#include "server.hpp"
#include "base64.hpp"
#include "client_proxy/client_proxy.hpp"

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

struct messeger
{
	std::string translate(const std::string& orignal)
	{
		std::string temp = orignal;
		for (auto & c : temp) c = toupper(c);
		return temp;
	}

	bool upload(const std::string& filename, const std::string& content)
	{
		std::ofstream file(filename, ios::binary);
		if (!file.is_open())
			return false;

		auto decode_str = base64_decode(content);
		file << decode_str;
		file.close();
		return true;
	}
};

TEST_CASE(rpc_qps, true)
{
	messeger m;

	server s(9000, std::thread::hardware_concurrency()); //if you fill the last param, the server will remove timeout connections. default never timeout.
	s.register_handler("add", &add);;
	s.register_handler("translate", &messeger::translate, &m);
	s.register_handler("upload", &messeger::upload, &m);

	s.run();

	//client
	//boost::asio::io_service ios;
	//client_proxy client(ios);
	//client.connect("xxx.xxx...", "9000");
	//client.call(...);
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
