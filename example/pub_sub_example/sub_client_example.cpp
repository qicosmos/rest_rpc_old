#include <rest_rpc/client.hpp>
#include <atomic>
namespace client
{
	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(madoka, void(int, int));
}

using sync_client = timax::rpc::sync_client<timax::rpc::msgpack_codec>;

std::atomic<bool> g_flag(false);
int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");

	boost::asio::io_service io;
	sync_client client{ io };
	client.connect("127.0.0.1", "9000");

	std::thread thd([] {std::this_thread::sleep_for(std::chrono::seconds(3)); g_flag = true; });

	try
	{
		client.sub(client::sub_add, [](int r)
		{
			std::cout << r << std::endl;
		},
			[] { return g_flag.load(); }
		);
	}
	catch (timax::rpc::client_exception const& e)
	{
		std::cout << e.what() << std::endl;
	}

	thd.join();
	return 0;
}