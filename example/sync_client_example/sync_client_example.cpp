#include <rest_rpc/client.hpp>

namespace client
{
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
			timax::SPD_LOG_ERROR(e.what());
		}

		return cfg;
	}
}

namespace client
{
	TIMAX_DEFINE_PROTOCOL(translate, std::string(std::string const&));
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(binary_func, std::string(const char*, int));
}

using sync_client = timax::rpc::sync_client;

void test_translate(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		std::string result = client.call(with_tag(client::translate, 1), "test");

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_add(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		auto result = client.call(client::add, 1, 2);

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_sub(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		auto result = client.sub(client::add);

		while (true)
		{
			size_t len = client.recieve();
			auto result = client::add.parse_json(std::string(client.data(), len));
			std::cout << result << std::endl;
		}

		//client.pub(client::add, 1, 2);
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_pub(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		std::string str;
		cin >> str;
		while (str != "stop")
		{
			client.pub(client::add, 1, 2);
			cin >> str;
		}

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_binary(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		char buf[40] = {};
		std::string str = "it is test";
		strcpy(buf, str.c_str());
		auto result = client.call_binary(client::binary_func, buf, sizeof(buf));

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_performance(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);
		std::thread thd([&io_service] {io_service.run(); });

		while (true)
		{
			client.call("translate", "test");
		}

		getchar();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");
	auto cfg = client::get_config();
	
	//test_performance(cfg);
	test_translate(cfg);
	test_add(cfg);
	test_binary(cfg);
	getchar();

	return 0;
}