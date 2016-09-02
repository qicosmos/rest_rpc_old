#include <rest_rpc/client.hpp>

namespace client
{
	TIMAX_DEFINE_PROTOCOL(sub_add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(madoka, void(int, int));
}

using sync_client = timax::rpc::sync_client<timax::rpc::msgpack_codec>;

int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");

	sync_client client;
	client.connect("127.0.0.1", "9000");

	try
	{
		int lhs = 1, rhs = 2;

		while (true)
		{
			using namespace std;

			client.pub(client::sub_add, lhs, rhs);
			++rhs;

			std::this_thread::sleep_for(1s);
		}
	}
	catch (timax::rpc::client_exception const& e)
	{
		std::cout << e.what() << std::endl;
	}

	return 0;
}