#include <iostream>
#include "client_proxy.hpp"

struct person
{
	int age;
	std::string name;

	META(age, name);
};

void test_client()
{
	try
	{
		boost::asio::io_service io_service;

		client_proxy client(io_service, "127.0.0.1", "9000");
		person p = { 20, "aa" };
		auto str = client.make_json("fun1", p, 1);
		client.call(str);
		//client.call("fun1", p, 1);
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

int main()
{
	test_client();
	return 0;
}