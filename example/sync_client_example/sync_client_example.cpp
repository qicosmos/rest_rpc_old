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
}

using sync_client = timax::rpc::sync_client<timax::rpc::msgpack_codec>;

namespace client
{
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(madoka, void(int, int));
}

int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");
	auto cfg = client::get_config();

	auto endpoint = timax::rpc::get_tcp_endpoint(cfg.hostname,
		boost::lexical_cast<uint16_t>(cfg.port));

	sync_client client;
	
	//while (true)
	//{
	//	client.call(endpoint, client::add, 1, 2);
	//}

	try
	{
		auto result = client.call(endpoint, client::add, 1, 2);
		assert(result == 3);

		//client.call(endpoint, client::madoka, 2.0, 8);
	}
	catch (timax::rpc::exception const& e)
	{
		std::cout << e.get_error_message() << std::endl;
	}

	std::getchar();
	return 0;
}