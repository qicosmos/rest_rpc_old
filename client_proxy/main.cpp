#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <kapok/Kapok.hpp>
//#include "client_proxy.hpp"
//#include "client_base.hpp"
#include "async_client.hpp"
#include "base64.hpp"
#include "../common.h"

struct person
{
	int age;
	std::string name;

	META(age, name);
};

struct configure
{
	std::string hostname;
	std::string port;

	META(hostname, port);
};

configure get_config()
{
	//congfigure cfg1 = { "127.0.0.1", 9000 };
	//Serializer sr;
	//sr.Serialize(cfg1);

	//const char* buf = sr.GetString();
	//std::ofstream myfile("server.cfg");
	//myfile << buf;
	//myfile.close();

	std::ifstream in("client.cfg");
	std::stringstream ss;
	ss << in.rdbuf();

	configure cfg = {};
	DeSerializer dr;
	try
	{
		dr.Parse(ss.str());
		dr.Deserialize(cfg);
	}
	catch (const std::exception& e)
	{
		SPD_LOG_ERROR(e.what());
	}

	return cfg;
}

namespace client
{
	TIMAX_DEFINE_PROTOCOL(translate, std::string(std::string const&));
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(binary_func, std::string(const char*, int));
}

//void test_translate(const configure& cfg)
//{
//	try
//	{
//		boost::asio::io_service io_service;
//		timax::client_proxy client{ io_service };
//		client.connect(cfg.hostname, cfg.port);
//	
//		std::string result = client.call(with_tag(client::translate, 1), "test");
//		
//		io_service.run();
//	}
//	catch (const std::exception& e)
//	{
//		std::cerr << "Exception: " << e.what() << std::endl;
//	}
//}
//
//void test_add(const configure& cfg)
//{
//	try
//	{
//		boost::asio::io_service io_service;
//		timax::client_proxy client{ io_service };
//		client.connect(cfg.hostname, cfg.port);
//	
//		auto result = client.call(client::add, 1, 2);
//	
//		io_service.run();
//	}
//	catch (const std::exception& e)
//	{
//		std::cerr << "Exception: " << e.what() << std::endl;
//	}
//}
//
//void test_sub(const configure& cfg)
//{
//	try
//	{
//		boost::asio::io_service io_service;
//		timax::client_proxy client{ io_service };
//		client.connect(cfg.hostname, cfg.port);
//
//		auto result = client.sub(client::add);
//
//		while (true)
//		{
//			size_t len = client.recieve();
//			auto result = client::add.parse_json(std::string(client.data(), len));
//			std::cout << result << std::endl;
//		}
//		
//		//client.pub(client::add, 1, 2);
//		io_service.run();
//	}
//	catch (const std::exception& e)
//	{
//		std::cerr << "Exception: " << e.what() << std::endl;
//	}
//}
//
//void test_pub(const configure& cfg)
//{
//	try
//	{
//		boost::asio::io_service io_service;
//		timax::client_proxy client{ io_service };
//		client.connect(cfg.hostname, cfg.port);
//
//		std::string str;
//		cin >> str;
//		while (str!="stop")
//		{
//			client.pub(client::add, 1, 2);
//			cin >> str;
//		}
//
//		io_service.run();
//	}
//	catch (const std::exception& e)
//	{
//		std::cerr << "Exception: " << e.what() << std::endl;
//	}
//}
//
//void test_binary(const configure& cfg)
//{
//	try
//	{
//		boost::asio::io_service io_service;
//		timax::client_proxy client{ io_service };
//		client.connect(cfg.hostname, cfg.port);
//
//		char buf[40] = {};
//		std::string str = "it is test";
//		strcpy(buf, str.c_str());
//		auto result = client.call_binary(client::binary_func, buf, sizeof(buf));
//
//		io_service.run();
//	}
//	catch (const std::exception& e)
//	{
//		std::cerr << "Exception: " << e.what() << std::endl;
//	}
//}
//
//
//void test_performance(const configure& cfg)
//{
//	try
//	{
//		boost::asio::io_service io_service;
//		timax::client_proxy client{ io_service };
//		client.connect(cfg.hostname, cfg.port);
//		std::thread thd([&io_service] {io_service.run(); });
//
//		while (true)
//		{
//			//client.call(str);
//			client.call("translate", "test");
//		}
//
//		getchar();
//	}
//	catch (const std::exception& e)
//	{
//		std::cerr << "Exception: " << e.what() << std::endl;
//	}
//}

int main(void)
{
	using async_client_t = timax::rpc::thread_safe_async_client;
	
	log::get().init("rest_rpc_client.lg");
	configure cfg = get_config();
	if (cfg.hostname.empty())
	{
		cfg = { "127.0.0.1", "9000" };
	}
	boost::asio::io_service io_service;
	auto work = std::make_unique<boost::asio::io_service::work>(io_service);
	auto client = boost::make_shared<async_client_t>(
		io_service, cfg.hostname, cfg.port);

	std::thread io_thread{ [&io_service] { io_service.run(); } };

	// test add 
	auto task = client->call(client::add, 4, 6);
	auto result = task.get();

	work.reset();
	if (io_thread.joinable())
		io_thread.join();

	return 0;
}

//if (cfg.is_sub)
//	test_sub(cfg);
//else
//	test_pub(cfg);
//test_performance(cfg);
//test_translate(cfg);
//test_add(cfg);
//test_binary(cfg);
//getchar();