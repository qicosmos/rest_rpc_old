#include <rest_rpc/client.hpp>
#include <atomic>
namespace client
{
	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(madoka, void(int, int));
}

using sync_client = timax::rpc::sync_client<timax::rpc::msgpack_codec>;

std::atomic<bool> g_flag(false);
int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");

	sync_client client;
	client.connect("127.0.0.1", "9000");

//	std::thread thd([&client] {std::this_thread::sleep_for(std::chrono::seconds(3)); client.cancel_sub_topic(client::sub_add.name()); });

	try
	{
		client.sub(client::sub_add, [](int r)
		{
			std::cout << r << std::endl;
		});

		auto r = client.call(client::add, 1, 2);
		std::cout << r << std::endl;
	}
	catch (timax::rpc::exception const& e)
	{
		std::cout << e.get_error_message() << std::endl;
	}

//	thd.join();
	getchar();
	return 0;
}