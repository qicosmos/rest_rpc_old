#include <rest_rpc/rpc.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(cnn, bool(std::string const&, std::string const&, int));
}

void test_atomic()
{
	std::atomic<int> call_id = 0;
	auto result = call_id.fetch_add(1);
}

int main()
{
	test_atomic();

	using client_type = timax::rpc::async_client<timax::rpc::msgpack_codec>;

	auto client = boost::make_shared<client_type>("127.0.0.1", "9000");

	auto task = client->call(client::add, 1, 2l).then([](int r)
	{
		std::cout << r << std::endl;
	});

	client->call(client::cnn, "", std::string{ "sdfsdfsdf" });

	task.wait();

	std::getchar();

	return 0;
}