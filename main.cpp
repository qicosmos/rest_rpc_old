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
};

int main()
{
	messager m;
	log::get().init("rest_rpc_server.lg");
	server s(9000, std::thread::hardware_concurrency()); //if you fill the last param, the server will remove timeout connections. default never timeout.
	s.register_handler("add", &add);;
	s.register_handler("translate", &messager::translate, &m);

	s.run();

	getchar();
	return 0;
}