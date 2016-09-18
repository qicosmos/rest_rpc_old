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
	auto client = std::make_shared<client_type>();

	// get the server endpoint 
	auto endpoint = timax::rpc::get_tcp_endpoint("127.0.0.1", 9000);
	
	// call an rpc
	client->call(endpoint, client::add, 100, 200.0).when_ok([](int r)
	{
		// process success
		std::cout << r << std::endl;
	}).when_error([](timax::rpc::exception const& error)
	{
		// process error
		if (error.get_error_code() == timax::rpc::error_code::FAIL)
			std::cout << error.get_error_message() << std::endl;
	});

	// call in synchronize style
	//auto task = client->call(endpoint, client::add, 400.0, 2);
	//auto result = task.get();
	//std::cout << result << std::endl;

	// call an error rpc
	//client->call(endpoint, client::foo, "I like to play Overwatch!", 1.0).when_error([](timax::rpc::exception const& error)
	//{
	//	// process error
	//	if (error.get_error_code() == timax::rpc::error_code::FAIL)
	//		std::cout << error.get_error_message() << std::endl;
	//});

	std::getchar();
	return 0;
}