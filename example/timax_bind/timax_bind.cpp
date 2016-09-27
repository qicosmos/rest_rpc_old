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
	auto bind1_with_boost_placeholders = timax::bind(&client::foo::wtf, foo, _1, _2);
	auto bind1_with_std_placeholders = timax::bind(&client::foo::wtf, foo,
		std::placeholders::_1, std::placeholders::_2);
	auto bind2 = timax::bind(client::add);
	auto bind3 = timax::bind(&client::foo::wtf, &foo);
	
	auto foo_ptr = std::make_shared<client::foo>();
	auto bind4 = timax::bind(&client::foo::wtf, foo_ptr);

	bind1_with_boost_placeholders(1, "boost::placeholders");
	bind1_with_std_placeholders(2, "std::placehodlers");
	bind2(1, 2);
	bind3(3, "WTF");
	bind4(4, "shared_ptr");
	return 0;
}