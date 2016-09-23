#include <rest_rpc/client.hpp>

namespace client
{
	//TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
	//TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	//TIMAX_DEFINE_PROTOCOL(madoka, void(int, int));
	TIMAX_DEFINE_SUB_PROTOCOL(sub_add, int);
}

using async_client_t = timax::rpc::async_client<timax::rpc::msgpack_codec>;

std::atomic<bool> g_flag(false);
int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");

	auto endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 9000);
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