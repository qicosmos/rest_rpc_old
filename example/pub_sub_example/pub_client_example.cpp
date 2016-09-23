#include <rest_rpc/client.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
}

using async_client_t = timax::rpc::async_client<timax::rpc::msgpack_codec>;

int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");

	auto endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 9000);
	auto async_client = std::make_shared<async_client_t>();

	try
	{
		int lhs = 1, rhs = 2;

		while (true)
		{
			using namespace std::chrono_literals;
			async_client->call(endpoint, client::sub_add, lhs, rhs++);
			std::this_thread::sleep_for(1s);
		}
	}
	catch (timax::rpc::exception const& e)
	{
		std::cout << e.get_error_message() << std::endl;
	}

	return 0;
}