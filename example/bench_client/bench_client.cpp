#include <rest_rpc/client.hpp>

namespace bench
{
	struct configure
	{
		std::string hostname;
		std::string port;

		META(hostname, port);
	};

	configure get_config()
	{
		std::ifstream in("bench_client.cfg");
		std::stringstream ss;
		ss << in.rdbuf();

		configure cfg = { "127.0.0.1", "9000" };
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

	TIMAX_DEFINE_PROTOCOL(add, int(int, int));

	std::atomic<uint64_t> count{ 0 };

	void bench_async(boost::asio::ip::tcp::endpoint const& endpoint)
	{
		using client_t = timax::rpc::async_client<timax::rpc::msgpack_codec>;

		auto client = std::make_shared<client_t>();

		std::thread{ []
		{
			while (true)
			{
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(1s);
				std::cout << count.load() << std::endl;
				count.store(0);
			}
		} }.detach();


		std::thread{
			[client, &endpoint]
		{
			int a = 0, b = 0;
			while (true)
			{
				client->call(endpoint, bench::add, a, b++).on_ok([](auto)
				{
					++count;
				});
			}

		} }.detach();
	}

	void bench_sync(boost::asio::ip::tcp::endpoint const& endpoint)
	{
		using client_t = timax::rpc::sync_client<timax::rpc::msgpack_codec>;

		std::thread{ []
		{
			while (true)
			{
				using namespace std::chrono_literals;
				std::this_thread::sleep_for(1s);
				std::cout << count.load() << std::endl;
				count.store(0);
			}
		} }.detach();

		std::thread{
			[endpoint]
		{
			client_t client;

			int a = 0, b = 0;
			while (true)
			{
				try
				{
					client.call(endpoint, bench::add, a, b++);
					++count;
				}
				catch (...)
				{
					std::cout << "Exception: " << std::endl;
					break;
				}
			}

		} }.detach();
	}
}

int main(int argc, char *argv[])
{
	// first of all, initialize log module
	timax::log::get().init("rest_rpc_client.lg");

	auto config = bench::get_config();
	auto endpoint = timax::rpc::get_tcp_endpoint(
		config.hostname, boost::lexical_cast<int16_t>(config.port));

	enum class client_style_t
	{
		UNKNOWN,
		SYCN,
		ASYCN
	};

	std::string client_style;
	client_style_t style = client_style_t::UNKNOWN;

	if (2 != argc)
	{
		std::cout << "Usage: " << "$ ./bench_server %s(sync or async)" << std::endl;
		return -1;
	}
	else
	{
		client_style = argv[1];
		if ("sync" == client_style)
		{
			style = client_style_t::SYCN;
		}
		else if ("async" == client_style)
		{
			style = client_style_t::ASYCN;
		}

		if (client_style_t::UNKNOWN == style)
		{
			std::cout << "Usage: " << "$ ./bench_server %s(sync or async)" << std::endl;
			return -1;
		}
	}

	switch (style)
	{
	case client_style_t::SYCN:
		bench::bench_sync(endpoint);
		break;
	case client_style_t::ASYCN:
		bench::bench_async(endpoint);
		break;
	default:
		return -1;
	}

	std::getchar();
	return 0;
}
