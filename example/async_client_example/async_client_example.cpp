#include <rest_rpc/rpc.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
}

void test_atomic()
{
	std::atomic<int> call_id = 0;
	auto result = call_id.fetch_add(1);
}

int main()
{
	test_atomic();

	using client_type = timax::rpc::detail::async_client<timax::rpc::msgpack_codec>;

	auto client = boost::make_shared<client_type>("127.0.0.1", "9000");

	auto task = client->call(client::add, 1, 2).then([](int r)
	{
		std::cout << r << std::endl;
	});

	task.wait();

	std::getchar();

	return 0;
}