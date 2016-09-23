#include <rest_rpc/rpc.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(sub_not_exist, double(int, std::string const&));
}

using tcp = boost::asio::ip::tcp;
using async_client_t = timax::rpc::async_client<timax::rpc::msgpack_codec>;

// create the client
auto asycn_client = std::make_shared<async_client_t>();

void async_client_rpc_example(tcp::endpoint const& endpoint)
{
	using namespace std::chrono_literals;

	// the interface is type safe and non-connect oriented designed
	asycn_client->call(endpoint, client::add, 1.0, 200.0f);

	// we can set some callbacks to process some specific eventsS
	asycn_client->call(endpoint, client::add, 1, 2).when_ok([](auto r) 
	{ 
		std::cout << r << std::endl; 
	}).when_error([](auto const& error)
	{
		std::cout << error.get_error_message() << std::endl;
	}).timeout(1min);

	// we can also use the asynchronized client in a synchronized way
	try
	{
		auto task = asycn_client->call(endpoint, client::add, 3, 5);
		auto const& result = task.get();
		std::cout << result << std::endl;
	}
	catch (timax::rpc::exception const& e)
	{
		std::cout << e.get_error_message() << std::endl;
	}
}

void async_client_sub_example(tcp::endpoint const& endpoint)
{
	// we can use the sub interface to keep track of some topics we are interested in
	asycn_client->sub(endpoint, client::sub_add, [](auto r)
	{
		std::cout << r << std::endl;
	}, // interface of dealing with error is also supplied;
		[](auto const& error) 
	{
		std::cout << error.get_error_message() << std::endl;
	});
}

int main()
{
	timax::log::get().init("async_client_example.lg");

	auto endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 9000);
	
	async_client_rpc_example(endpoint);
	async_client_sub_example(endpoint);

	std::getchar();
	return 0;
}