#include <rest_rpc/client.hpp>

namespace client
{
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

	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
}

using async_client_t = timax::rpc::async_client<timax::rpc::msgpack_codec>;

int main(void)
{
	timax::log::get().init("pub_client.lg");

	auto config = client::get_config();

	auto endpoint = timax::rpc::get_tcp_endpoint(config.hostname,
		boost::lexical_cast<uint16_t>(config.port));

	auto async_client = std::make_shared<async_client_t>();

	try
	{
		int lhs = 1, rhs = 2;

		while (true)
		{
			using namespace std::chrono_literals;
			async_client->call(endpoint, client::sub_add, lhs, rhs++);
			std::this_thread::sleep_for(1s);
		}
	}
	catch (timax::rpc::exception const& e)
	{
		std::cout << e.get_error_message() << std::endl;
	}

	return 0;
}