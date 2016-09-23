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

std::atomic<bool> g_flag(false);
int main(void)
{
	timax::log::get().init("sub_client.lg");

	auto config = client::get_config();

	auto endpoint = timax::rpc::get_tcp_endpoint(config.hostname, 
		boost::lexical_cast<uint16_t>(config.port));

	auto async_client = std::make_shared<async_client_t>();

	try
	{
		async_client->sub(endpoint, client::sub_add, 
			[](int r) { std::cout << r << std::endl; },
			[](auto const& e) { std::cout << e.get_error_message() << std::endl; }
		);
	}
	catch (timax::rpc::exception const& e)
	{
		std::cout << e.get_error_message() << std::endl;
	}

	getchar();
	return 0;
}