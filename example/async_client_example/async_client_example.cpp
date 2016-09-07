#include <rest_rpc/rpc.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(foo, void(std::string, double));
}


int main()
{
	using client_type = timax::rpc::async_client<timax::rpc::msgpack_codec>;

	// create the client
	auto client = boost::make_shared<client_type>("127.0.0.1", "9000");

	// call an rpc
	client->call(client::add, 100, 200.0).then([](int r)
	{
		std::cout << r << std::endl;
	});

	client->sub(client::sub_add, [](int result) { std::cout << result << std::endl; });
	//client->call(client::foo, "hello world", 1);

	std::getchar();
	return 0;
}