#include <rest_rpc/rpc.hpp>

namespace client
{
	int add(int a, int b)
	{
		return a + b;
	}

	int apply_add(std::function<int()> add_result, int rhs)
	{
		return add_result() + rhs;
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
	auto bind1_with_boost_placeholders = timax::bind(&client::foo::wtf, foo, _2, _1);
	//auto bind1_with_std_placeholders = timax::bind(&client::foo::wtf, foo,
	//	std::placeholders::_1, std::placeholders::_2);
	//auto bind2 = timax::bind(client::add, 1, _1);
	//auto bind3 = timax::bind(&client::foo::wtf, &foo);
	//
	//auto foo_ptr = std::make_shared<client::foo>();
	//auto bind4 = timax::bind(&client::foo::wtf, foo_ptr);
	//std::function<int()> bind_test= timax::bind(client::add, 1, 1);
	//auto bind_test1 = timax::bind(client::apply_add, bind_test, _1);

	bind1_with_boost_placeholders("boost::placeholders", 1);
	//bind1_with_std_placeholders(2, "std::placehodlers");
	//bind2(2);
	//bind3(3, "WTF");
	//bind4(4, "shared_ptr");
	//bind_recursive();

	//using make_bind_t = timax::make_bind_index_sequence_and_args_tuple<std::tuple<int, double, char, short>,
	//	void, decltype(_1), void, decltype(_2)>;
	//
	//using args_tuple_type = make_bind_t::args_tuple_type;
	//using index_sequence = make_bind_t::index_sequence_type;
	//
	//auto r = std::is_same<std::tuple<double, short>, args_tuple_type>::value;
	//r = std::is_same<std::index_sequence<0, 1>, index_sequence>::value;

	return 0;
}