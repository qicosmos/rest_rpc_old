#include <rest_rpc/rpc.hpp>
#include <iostream>

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

namespace client
{
	struct person
	{
		int age;
		std::string name;

		META(age, name);
	};

	TIMAX_DEFINE_PROTOCOL(translate, std::string(std::string const&));
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(binary_func, std::string(const char*, int));
}

int main(void)
{
	using async_client_t = timax::rpc::async_client;

	// init log
	timax::log::get().init("rest_rpc_client.lg");
	
	// init from config
	configure cfg = get_config();
	if (cfg.hostname.empty())
	{
		cfg = { "127.0.0.1", "9000" };
	}

	// create client
	boost::asio::io_service io_service;
	auto work = std::make_unique<boost::asio::io_service::work>(io_service);
	auto client = boost::make_shared<async_client_t>(
		io_service, cfg.hostname, cfg.port);

	// io thread
	std::thread io_thread{ [&io_service] { io_service.run(); } };

	// test add 
	auto task = client->call(client::add, 4, 6);
	//task.cancel();
	auto result = task.get();

	// notify and wait for exit
	work.reset();
	if (io_thread.joinable())
		io_thread.join();

	std::cout << result << std::endl;

	return 0;
}
