#include <rest_rpc/rpc.hpp>

namespace client
{
	int add(int a, int b)
	{
		return a + b;
	}

	struct foo
	{
		double wtf(int a, std::string const& b) const
		{
			return a * static_cast<double>(b.size());
		}
	};
}

int main()
{
	//using namespace std::placeholders;
	client::foo foo;
	auto bind1 = timax::bind(&client::foo::wtf, foo, std::placeholders::_1, std::placeholders::_2);
	auto bind2 = timax::bind(client::add);
	auto bind3 = timax::bind(&client::foo::wtf, &foo);

	bind1(1, "sdfsdfsdf");
	bind2(1, 2);
	bind3(1, "WTF");

	return 0;
}