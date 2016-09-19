#include <rest_rpc/server.hpp>

uint16_t port = 9000;
size_t pool_size = std::thread::hardware_concurrency();

namespace client
{
	int add(int a, int b)
	{
		return a + b;
	}

	void foo(double, int)
	{
		std::cout << foo << std::endl;
	}
}

struct foo
{
	void operator ()(int, double) const
	{

	}
};

template <size_t ... Is>
void print(std::index_sequence<Is...>)
{
	bool swallow[] = { (printf("%d\n", Is), true)... };
}

int main()
{
	//timax::function_traits<foo const&>::tuple_type;

	//auto func = timax::bind(client::add, std::placeholders::_1, std::placeholders::_2);
	//func(1, 2);
	//using result_type = timax::function_traits<decltype(func)>::result_type;
	//auto r = result_type{};
	timax::log::get().init("rest_rpc_server.lg");
	using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;
	server_t server{ port, pool_size };

	server.register_handler("add", client::add, [](auto conn, int r) {});
	server.register_handler("sub_add", client::add, [&server](auto conn, int r) { server.pub("sub_add", r); });
	//server.async_register_handler("add", client::add, [](auto conn, int r) { std::cout << r << std::endl; });
	//server.async_register_handler("sub_add", client::add, [&server](auto conn, int r) { server.pub("sub_add", r); });

	

	server.start();
	std::getchar();

	// for test
	std::uint64_t last_succeed_count = 0;
	while (true)
	{
		auto curr_succeed_count = (std::uint64_t)timax::rpc::g_succeed_count;
		std::cout << curr_succeed_count - last_succeed_count << std::endl;
		last_succeed_count = curr_succeed_count;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	} // for test

	server.stop();
	return 0;
}