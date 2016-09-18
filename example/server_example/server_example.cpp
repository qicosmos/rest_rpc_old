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

	using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;
	server_t server{ port, pool_size };

	//server.register_handler("add", client::add, [](auto conn, int r) { std::cout << r << std::endl; });
	//server.register_handler("sub_add", client::add, [&server](auto conn, int r) { server.pub("sub_add", r); });
	server.async_register_handler("add", client::add, [](auto conn, int r) { std::cout << r << std::endl; });
	server.async_register_handler("sub_add", client::add, [&server](auto conn, int r) { server.pub("sub_add", r); });

	

	server.start();
	std::getchar();
	server.stop();
	
	return 0;
}