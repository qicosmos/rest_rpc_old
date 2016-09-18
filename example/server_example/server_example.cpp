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

int main()
{
	using server_t = timax::rpc::server<timax::rpc::msgpack_codec>;
	server_t server{ port, pool_size };

	server.register_handler("add", client::add, [](auto conn, int r) { std::cout << r << std::endl; });
	server.register_handler("sub_add", client::add, [&server](auto conn, int r) { server.pub("sub_add", r); });
	//server.async_register_handler("add", client::add, [](auto conn, int r) { std::cout << r << std::endl; });
	//server.async_register_handler("foo", client::foo, [](auto conn) { conn->close(); });

	server.start();
	std::getchar();
	server.stop();

	//timax::function_traits<foo const&>::tuple_type;
	
	return 0;
}