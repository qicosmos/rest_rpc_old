#include <string>
#include "server.hpp"


int add(int a, int b)
{
	return a + b;
}

struct messager
{
	std::string translate(const std::string& orignal)
	{
		std::string temp = orignal;
		for (auto & c : temp) c = toupper(c);
		return temp;
	}

	void func(const char* data, int len, std::string& result)
	{
		std::string s = data;
		std::cout << s << std::endl;
		result = "ok";
	}
};

int main()
{
	messager m;
	log::get().init("rest_rpc_server.lg");
	server s(9000, std::thread::hardware_concurrency()); //if you fill the last param, the server will remove timeout connections. default never timeout.
	s.register_handler("add", &add);;
	s.register_handler("translate", &messager::translate, &m);

	s.register_binary_handler("binary_func", &messager::func, &m);//note:the function type is fixed, only recieve binary data.

	s.run();

	getchar();

	//for test performance.
	//std::uint64_t last_succeed_count = 0;

	//while (true)
	//{
	//	auto curr_succeed_count = (std::uint64_t)g_succeed_count;
	//	std::cout << curr_succeed_count - last_succeed_count << std::endl;
	//	last_succeed_count = curr_succeed_count;
	//	std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	//}

	return 0;
}