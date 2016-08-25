#include <rest_rpc/client.hpp>
#include "unified_client.hpp"

namespace client
{
	struct person
	{
		int age;
		std::string name;

		META(age, name);
	};

	struct configure
	{
		std::string hostname;
		std::string port;

		META(hostname, port);
	};

	configure get_config()
	{
		std::ifstream in("client.cfg");
		std::stringstream ss;
		ss << in.rdbuf();

		configure cfg = { "127.0.0.1", "9000" }; //if can't find the config file, give the default value.
		DeSerializer dr;
		try
		{
			dr.Parse(ss.str());
			dr.Deserialize(cfg);
		}
		catch (const std::exception& e)
		{
			timax::SPD_LOG_ERROR(e.what());
		}

		return cfg;
	}
}

namespace client
{
	TIMAX_DEFINE_PROTOCOL(translate, std::string(std::string const&));
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PROTOCOL(binary_func, std::string(const char*, int));
	TIMAX_DEFINE_PROTOCOL(begin_upload, bool(const std::string&));
	TIMAX_DEFINE_PROTOCOL(upload, void(const char*, int));
	TIMAX_DEFINE_PROTOCOL(end_upload, void());
	TIMAX_DEFINE_PROTOCOL(cancel_upload, bool());
	TIMAX_DEFINE_PROTOCOL(is_subscriber_exsit, bool(const std::string&));
}

namespace sub_client
{
	TIMAX_DEFINE_SUB_PROTOCOL(sub_topic, std::string(std::string));
	TIMAX_DEFINE_PROTOCOL(add, int(int, int));
}

namespace pub_client
{
	TIMAX_DEFINE_PUB_PROTOCOL(add, int(int, int));
	TIMAX_DEFINE_PUB_PROTOCOL(transfer, void(const char*, int));
}

using sync_client = timax::rpc::sync_client;

void test_translate(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		std::string result = client.call(with_tag(client::translate, 1), "test");

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_add(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		auto result = client.call(client::add, 1, 2);

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_sub_file(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);
		
		int total = 0;
		auto r = client.sub(sub_client::sub_topic, "transfer");

		size_t len = client.recieve();
		int total_size = *(int*)(client.data()); 
		std::cout << total_size << std::endl;

		std::ofstream output_file("tempfile", std::ios_base::binary);
		while (total_size)
		{
			size_t len = client.recieve();
			total += len;
			total_size -= len;
			output_file.write(client.data(), len);
		}

		output_file.close();
		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_pub_file(const client::configure& cfg)
{
	boost::asio::io_service io_service;
	sync_client client{ io_service };
	client.connect(cfg.hostname, cfg.port);
	std::thread thd([&io_service] {io_service.run(); });

	try
	{
		bool r = client.call(client::is_subscriber_exsit, pub_client::transfer.name());
		if (!r)
		{
			std::cout << "the topic has no subscriber" << std::endl;
			thd.join();
			return;
		}

		std::ifstream stream("D:/OReilly.Docker.Cookbook.pdf", std::ios::ios_base::binary | std::ios::ios_base::in);
		if (!stream)
		{
			std::cout << "the file is not exist" << std::endl;
			thd.join();
			return;
		}

		stream.seekg(0, ios_base::end);
		auto total = (int)stream.tellg();
		std::cout << total << std::endl;
		stream.seekg(ios_base::beg);

		//char sizebuf[4];
		//memcpy(sizebuf, &total, sizeof(int));
		//int rr = *reinterpret_cast<int*>(sizebuf);
		client.call_binary(pub_client::transfer, (char*)(&total), sizeof(int)); //tell the subscriber total size

		const int size = 4096;
		char buf[size];
		while (stream)
		{
			stream.read(buf, size);
			int real_size = static_cast<int>(stream.gcount());
			client.call_binary(pub_client::transfer, buf, real_size);
		}
		stream.close();

		getchar();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}

	thd.join();
	getchar();
}

void test_sub(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		auto r = client.sub(sub_client::sub_topic, "add");

		while (true)
		{
			size_t len = client.recieve();
			auto result = sub_client::add.parse_json(std::string(client.data(), len));
			std::cout << result << std::endl;
		}

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_pub(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		bool r = client.call(client::is_subscriber_exsit, pub_client::add.name());
		if (!r)
		{
			std::cout << "the topic has no subscriber" << std::endl;
			return;
		}

		std::string str;
		cin >> str;
		while (str != "stop")
		{
			client.pub(pub_client::add, 1, 2);
			cin >> str;
		}

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_binary(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);

		char buf[40] = {};
		std::string str = "it is test";
		strcpy(buf, str.c_str());
		auto result = client.call_binary(client::binary_func, buf, sizeof(buf));

		io_service.run();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

void test_performance(const client::configure& cfg)
{
	try
	{
		boost::asio::io_service io_service;
		sync_client client{ io_service };
		client.connect(cfg.hostname, cfg.port);
		std::thread thd([&io_service] {io_service.run(); });

		while (true)
		{
			client.call("translate", "test");
		}

		getchar();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}
}

template <typename F>
class scope_guard
{
public:
	explicit scope_guard(F && f) : func_(std::move(f)), dismiss_(false) {}
	explicit scope_guard(const F& f) : func_(f), dismiss_(false) {}

	~scope_guard()
	{
		if (!dismiss_)
			func_();
	}

	scope_guard(scope_guard && rhs) : func_(std::move(rhs.func_)), dismiss_(rhs.dismiss_) { rhs.dismiss(); }

	void dismiss()
	{
		dismiss_ = true;
	}

private:
	F func_;
	bool dismiss_;

	scope_guard();
	scope_guard(const scope_guard&);
	scope_guard& operator=(const scope_guard&);
};

template <typename F>
scope_guard<typename std::decay<F>::type> make_guard(F && f)
{
	return scope_guard<typename std::decay<F>::type>(std::forward<F>(f));
}

void func(sync_client& client, std::ifstream& stream)
{
	//auto guard = make_guard([&client, &stream] {
	//	//stream.close();
	//	bool r = client.call(client::cancel_upload);
	//});

	const int size = 4096;
	char buf[size];
	while (stream)
	{
		stream.read(buf, size);
		int real_size = static_cast<int>(stream.gcount());
		client.call_binary(client::upload, buf, real_size);
		throw std::exception();
	}
}

void test_upload_file(const client::configure& cfg)
{
	boost::asio::io_service io_service;
	sync_client client{ io_service };
	client.connect(cfg.hostname, cfg.port);
	std::thread thd([&io_service] {io_service.run(); });

	try
	{
		bool r = client.call(client::begin_upload, "test file");
		if (!r)
			return;

		std::ifstream stream("D:/OReilly.Docker.Cookbook.pdf", std::ios::ios_base::binary | std::ios::ios_base::in);
		if (!stream.is_open())
			return;

		auto guard = make_guard([&client, &stream] {
			stream.close();
			client.call(client::cancel_upload);
			client.disconnect();
		});
		
		const int size = 4096;
		char buf[size];
		while (stream)
		{
			stream.read(buf, size);
			int real_size = static_cast<int>(stream.gcount());
			client.call_binary(client::upload, buf, real_size);
		}

		client.call(client::end_upload);

		guard.dismiss();
		stream.close();
		
		getchar();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}

	thd.join();
}

void read_file()
{
	std::ifstream stream("D:/OReilly.Docker.Cookbook.pdf", std::ifstream::binary);
	if (!stream)
		return;

	const int size = 8192;
	char buf[8192] = {};
	while (stream)
	{
		stream.read(buf, size);
		std::cout << stream.gcount() << std::endl;
	}

	if (!stream)
		return;
}

int main(void)
{
	timax::log::get().init("rest_rpc_client.lg");
	auto cfg = client::get_config();

	timax::rpc::unified_client uclient;
	uclient.connect("127.0.0.1", "9000");
	uclient.call("add", 1, 2);
	//uclient.call(client::begin_upload.name(), "tempfile");
	std::string s;
	std::cin >> s;

	if (s == "sub")
		test_sub(cfg);
	else if (s == "pub")
		test_pub(cfg);

	return 0;

//	read_file();
	test_upload_file(cfg);
	//test_performance(cfg);
	test_translate(cfg);
	test_add(cfg);
	test_binary(cfg);
	getchar();

	return 0;
}