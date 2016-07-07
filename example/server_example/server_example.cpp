#include <rest_rpc/server.hpp>

namespace client
{
	int add(int a, int b)
	{
		return a + b;
	}

	struct messager
	{
		std::string translate(const std::string& orignal)
		{
			std::string temp = orignal;
			for (auto & c : temp) c = toupper(c);
			return temp;
			return orignal;
		}

		void binary_func(const char* data, int len, std::string& result)
		{
			std::string s = data;
			std::cout << s << std::endl;
			result = "ok";
		}
	};
	struct configure
	{
		int port;
		size_t thread_num;
		bool nodelay;

		META(port, thread_num, nodelay);
	};

	configure get_config()
	{
		std::ifstream in("server.cfg");
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

int main()
{
	using timax::rpc::server;
	using client::messager;
	using client::configure;

	messager m;
	timax::log::get().init("rest_rpc_server.lg");
	auto cfg = client::get_config();
	int port = 9000; 
	int thread_num = std::thread::hardware_concurrency();
	if (cfg.port != 0)
	{
		port = cfg.port;
		thread_num = cfg.thread_num;
	}

	server s(port, thread_num); //if you fill the last param, the server will remove timeout connections. default never timeout.
	s.register_handler("add", &client::add);;
	s.register_handler("translate", &messager::translate, &m);

	s.register_binary_handler("binary_func", &messager::binary_func, &m);//note:the function type is fixed, only recieve binary data.

	s.run();

	getchar();

	//for test performance.
	std::uint64_t last_succeed_count = 0;

	while (true)
	{
		auto curr_succeed_count = (std::uint64_t)timax::rpc::g_succeed_count;
		std::cout << curr_succeed_count - last_succeed_count << std::endl;
		last_succeed_count = curr_succeed_count;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	return 0;
}