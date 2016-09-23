#include <rest_rpc/server.hpp>

namespace bench
{
	int add(int a, int b)
	{
		return a + b;
	}

	void some_task_takes_a_lot_of_time(double, int)
	{
		using namespace std::chrono_literals;
		std::this_thread::sleep_for(5s);
	}
}

int main(int argc, char *argv[])
{
	using namespace std::chrono_literals;
	using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;

	if (2 != argc)
	{
		std::cout << "Usage: " << "$ ./bench_server %d(your port number)" << std::endl;
		return -1;
	}

	timax::log::get().init("bench_server.lg");

	std::atomic<uint64_t> success_count{ 0 };
	auto port = boost::lexical_cast<uint16_t>(argv[1]);
	auto pool_size = static_cast<size_t>(std::thread::hardware_concurrency());

	server_t server{ port, pool_size };
	server.register_handler("add", bench::add, 
		[&success_count](auto, auto)
	{
		++success_count;
	});

	server.register_handler("sub_add", bench::add, [&server, &success_count](auto, auto r)
	{
		++success_count;
		server.pub("sub_add", r, [&success_count]
		{
			++success_count;
		});
	});

	std::thread{ [&success_count]
	{
		while (true)
		{
			using namespace std::chrono_literals;

			std::cout << "QPS: " << success_count.load() << ".\n";
			success_count.store(0);
			std::this_thread::sleep_for(1s);
		}
		
	} }.detach();

	server.start();
	std::getchar();
	server.stop();
	return 0;
}
