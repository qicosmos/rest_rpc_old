#include <rest_rpc/rpc.hpp>
#include <iostream>
#include <atomic>

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

	auto client = boost::make_shared<client_type>();

	auto task = client->call(client::add, 1, 2).then([](int r) -> double 
	{
		if (r == 3)
			return 1.0;
		return 0.0;
	}).then([](double f) 
	{ 
		std::cout << f << std::endl; 
	});

	return 0;
}