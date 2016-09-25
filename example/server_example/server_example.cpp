#include <rest_rpc/server.hpp>

uint16_t port = 9000;
size_t pool_size = std::thread::hardware_concurrency();

namespace client
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


template <size_t ... Is>
void print(std::index_sequence<Is...>)
{
	bool swallow[] = { (printf("%d\n", Is), true)... };
}

int main()
{
	timax::log::get().init("rest_rpc_server.lg");
	using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;
	server_t server{ port, pool_size };

	server.register_handler("add", client::add);
	server.register_handler("sub_add", client::add, [&server](auto conn, int r) { server.pub("sub_add", r); });
	
	server.async_register_handler("time_consuming", client::some_task_takes_a_lot_of_time, [](auto conn) { std::cout << "acomplished!" << std::endl; });

	server.start();
	std::getchar();
	server.stop();
	return 0;
}