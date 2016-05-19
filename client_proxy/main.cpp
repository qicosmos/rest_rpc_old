#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <kapok/Kapok.hpp>
#include "client_proxy.hpp"
#include "base64.hpp"


//result要么是基本类型，要么是结构体；当请求成功时，code为0, 如果请求是无返回类型的，则result为空; 
//如果是有返回值的，则result为返回值。response_msg会序列化为一个标准的json串，回发给客户端。 
//网络消息的格式是length+body，由4个字节的长度信息（用来指示包体的长度）加包体组成。 
template<typename T>
struct response_msg
{
	int code;
	T result; //json格式字符串，基本类型或者是结构体.
	META(code, result);
};

enum result_code
{
	OK = 0,
	FAIL = 1,
	EXCEPTION = 2,

};

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
		DeSerializer dr;
		client_proxy client(io_service);
		client.connect("192.168.2.154", "9000");
		person p = { 20, "aa" };
		//auto str = client.make_json("fun1", p, 1);
		//client.call(str);
		
		std::string result = client.call("fun1", p, 1);
		dr.Parse(result);

		response_msg<int> response = {};
		dr.Deserialize(response);
		if (response.code == result_code::OK)
		{
			std::cout << response.result << std::endl;
		}
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

template<typename T>
void handle_result(const char* result)
{
	DeSerializer dr;
	dr.Parse(result);
	Document& doc = dr.GetDocument();
	doc.Parse(result);
	if (doc["code"].GetInt() == result_code::OK)
	{
		response_msg<T> response = {};
		dr.Deserialize(response);
		std::cout << response.result << std::endl;
	}
	else
	{
		//maybe exception, output the exception message.
		std::cout << doc["result"].GetString() << std::endl;
	}
}

void test_async_client()
{
	try
	{
		boost::asio::io_service io_service;
		client_proxy client(io_service);
		client.async_connect("127.0.0.1", "9000", [&client] (boost::system::error_code& ec)
		{
			if (ec)
			{
				std::cout << "connect error." << std::endl;
				return;
			}

			client.async_call("add", [&client](boost::system::error_code ec, std::string result)
			{
				if (ec)
				{
					std::cout << "call error." << std::endl;
					return;
				}

				handle_result<int>(result.c_str());
			},1, 2);

		});

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_spawn_client()
{
	try
	{
		boost::asio::io_service io_service;
		boost::asio::spawn(io_service, [&io_service] (boost::asio::yield_context yield)
		{
			client_proxy client(io_service);
			client.async_connect("127.0.0.1", "9000", yield);
			//auto str = client.make_json("fun1", p, 1);
			//client.call(str);

			std::string result = client.async_call("add", yield, 1,2);
			handle_result<int>(result.c_str());
		});
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_performance()
{
	try
	{
		boost::asio::io_service io_service;
		
		client_proxy client(io_service);
		client.connect("192.168.2.154", "9000");

		auto str = client.make_json("add", 1, 2);
		std::thread thd([&io_service] {io_service.run(); });
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		while (true)
		{
			client.call(str);
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_translate()
{
	try
	{
		boost::asio::io_service io_service;
		client_proxy client(io_service);
		client.connect("127.0.0.1", "9000");
		
		std::string result = client.call("translate", "test");
		handle_result<std::string>(result.c_str());
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_upload()
{
	try
	{
		boost::asio::io_service io_service;
		client_proxy client(io_service);
		client.connect("127.0.0.1", "9000");

		std::ifstream file("client_proxy.sln", ios::binary);
		if (!file.is_open())
			return;

		std::stringstream ss;
		ss << file.rdbuf();
		auto content = base64_encode(ss.str().c_str(), ss.str().length());
		std::string result = client.call("upload", "test", content);
		handle_result<bool>(result.c_str());
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

int main()
{
	//test_performance();
	//test_client();
	test_upload();
	test_translate();
	test_async_client();
	test_spawn_client();
	return 0;
}