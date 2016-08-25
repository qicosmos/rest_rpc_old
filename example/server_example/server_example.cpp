#include <rest_rpc/server.hpp>

namespace client
{
	struct messenger
	{
		std::string translate(const std::string& orignal)
		{
			std::string temp = orignal;
			for (auto & c : temp) c = toupper(c);
			return temp;
			return orignal;
		}

		void binary_func(const char* data, size_t len)
		{
			std::string s = data;
			std::cout << s << std::endl;
		}
	};
	struct configure
	{
		int port;
		int thread_num;
		bool nodelay;

		META(port, thread_num, nodelay);
	};

	configure get_config()
	{
		std::ifstream in("server.cfg");
		std::stringstream ss;
		ss << in.rdbuf();

		configure cfg = {};

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

//this demo is not thread safe, if you want thread safe, use threadpool, manager holds many uploader object with filename.
class file_manager
{
	enum status
	{
		init,
		begin,
		uploading,
		end
	};
public:
	bool begin_upload(const std::string& filename)
	{
		assert(status_ == status::init || status_ == status::end);
		output_file_.open(filename, std::ios_base::binary);
		if (!output_file_) 
		{
			return false;
		}
		status_ = status::begin;
		filename_ = filename;
		return true;
	}

	void upload(const char* data, size_t size)
	{
		assert(status_ == status::begin|| status_== status::uploading);
		
		output_file_.write(data, size);

		status_ = status::uploading;
	}

	bool cancel_upload()
	{
		output_file_.close();
		status_ = status::init;
		int ret = std::remove(filename_.c_str());
		return ret == 0;
	}

	void end_upload()
	{
		assert(status_ == status::uploading);
		output_file_.close();
		status_ = status::end;
		//return true;
	}

private:
	std::ofstream output_file_;
	status status_ = status::init;
	std::string filename_;
};

int add(int a, int b)
{
	return a + b;
}

void after_add(std::shared_ptr<timax::rpc::connection> sp, int r)
{
	//encode
	//auto tp = std::make_tuple(0, r);
	msgpack::sbuffer sbuf;
	msgpack::pack(sbuf, "error");

	sp->response(sbuf.data(), sbuf.size(), timax::rpc::result_code::FAIL);
}

int main()
{
	using namespace timax::rpc;
	using timax::rpc::server;
	using client::messenger;
	using client::configure;

	timax::log::get().init("rest_rpc_server.lg");
	auto cfg = client::get_config();
	int port = 9000; 
	int thread_num = std::thread::hardware_concurrency();
	if (cfg.port != 0)
	{
		port = cfg.port;
		thread_num = cfg.thread_num;
	}

	auto sp = std::make_shared<server<msgpack_decode>>(port, thread_num);
	//server s(port, thread_num); //if you fill the last param, the server will remove timeout connections. default never timeout.
	
	messenger m;
	sp->register_handler("translate", &messenger::translate, &m, nullptr);
	
	file_manager fm;
	sp->register_handler("add", &add, &after_add);
	sp->register_handler("begin_upload", &file_manager::begin_upload, &fm, nullptr);
	/*sp->register_handler1("add", &client::add, [&s](int r) {});
	sp->register_handler1("test", &client::test,&after);*/

	sp->run();

	getchar();

	//for test performance.
	std::uint64_t last_succeed_count = 0;

	while (true)
	{
		auto curr_succeed_count = (std::uint64_t)timax::rpc::g_succeed_count;
		std::cout << curr_succeed_count - last_succeed_count << std::endl;
		last_succeed_count = curr_succeed_count;
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	return 0;
}