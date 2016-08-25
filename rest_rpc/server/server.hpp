#pragma once
#include <thread_pool.hpp>

namespace timax { namespace rpc 
{
	class server : private boost::noncopyable, public std::enable_shared_from_this<server>
	{
	public:
		server(short port, size_t size, size_t timeout_milli = 0) : io_service_pool_(size), timeout_milli_(timeout_milli),
			acceptor_(io_service_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
		{
			register_handler(SUB_TOPIC, &server::sub, this, [](std::shared_ptr<connection> ptr, bool r) {});
			//connection::set_callback(std::bind(&server::callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
			//register_handler("is_subscriber_exsit", &server::is_subscriber_exsit, this);
			//router::get().set_callback(std::bind(&server::callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
			//router::get().set_callback_pub_binary(std::bind(&server::pub, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
			do_accept();
		}

		~server()
		{
			io_service_pool_.stop();
			thd_->join();
		}

		void run()
		{
			thd_ = std::make_shared<std::thread>([this] {io_service_pool_.run(); });
		}

		template<typename Function>
		void register_handler(std::string const & name, const Function& f)
		{
			router::get().register_handler(name, f);
		}

		template<typename Function>
		std::enable_if_t<!std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, const std::function<void(std::shared_ptr<connection>, typename function_traits<Function>::return_type)>& af)
		{
			if (af == nullptr)
			{
				//af = default; response
			}

			//std::function<void(typename function_traits<Function>::return_type)> fn = std::bind(af, shared_from_this(), std::placeholders::_1);
			router::get().register_handler(name, f, af);
		}

		template<typename Function>
		std::enable_if_t<std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, const std::function<void(std::shared_ptr<connection>)>& af)
		{
			if (af == nullptr)
			{
				//af = default;
			}

			//std::function<void()> fn = std::bind(af, shared_from_this());
			router::get().register_handler(name, f, af);
		}

		template<typename Function, typename Self>
		std::enable_if_t<!std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, Self* self, const std::function<void(std::shared_ptr<connection>, typename function_traits<Function>::return_type)>& af)
		{
			if (af == nullptr)
			{
				//af = default; response
			}

			//std::function<void(typename function_traits<Function>::return_type)> fn = std::bind(af, shared_from_this(), std::placeholders::_1);
			router::get().register_handler(name, f, self, af);
		}

		template<typename Function, typename Self>
		std::enable_if_t<std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, Self* self, const std::function<void(std::shared_ptr<connection>)>& af)
		{
			if (af == nullptr)
			{
				//af = default;
			}

			//std::function<void()> fn = std::bind(af, shared_from_this());
			router::get().register_handler(name, f, self, af);
		}

		void remove_handler(std::string const& name)
		{
			router::get().remove_handler(name);
		}

	private:
		void do_accept()
		{
			conn_.reset(new connection(shared_from_this(), io_service_pool_.get_io_service(), timeout_milli_));  //how to pass server to the connection?
			acceptor_.async_accept(conn_->socket(), [this](boost::system::error_code ec)
			{
				if (ec)
				{
					//todo log
				}
				else
				{
					conn_->start();
				}

				do_accept();
			});
		}

	private:
		bool sub(const std::string& topic)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			if (!router::get().has_handler(topic))
				return false;

			conn_map_.emplace(topic, conn_);

			return true;
		}

		bool is_subscriber_exsit(const std::string& topic)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			return conn_map_.find(topic) != conn_map_.end();
		}

		void pub(const std::string& topic, const char* data, size_t size)
		{
			decltype(conn_map_.equal_range(topic)) temp;
			std::unique_lock<std::mutex> lock(mtx_);

			auto range = conn_map_.equal_range(topic);
			if (range.first == range.second)
				return;

			temp = range;
			lock.unlock();

			std::shared_ptr<char> share_data(new char[size], [](char*p) {delete p; });
			memcpy(share_data.get(), data, size);

			for (auto it = range.first; it != range.second;)
			{
				auto ptr = it->second.lock();
				if (!ptr)
					it = conn_map_.erase(it);
				else
				{
					pool_.post([ptr, share_data, size] { ptr->response(share_data.get(), size); });
					
					++it;
				}
			}

			lock.lock(); //clear invalid connection

			for (auto it = conn_map_.cbegin(); it != conn_map_.end();)
			{
				auto ptr = it->second.lock();
				if (!ptr)
				{
					it = conn_map_.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

		void callback(std::shared_ptr<connection> conn, const char* data, size_t size)
		{
			router& _router = router::get();
			_router.route(conn, data, size);
		}

		//this callback from router, tell the server which connection sub the topic and the result of handler
		void callback1(const std::string& topic, const char* result, std::shared_ptr<connection> conn, int16_t ftype, bool has_error = false)
		{
			if (has_error)
			{
				SPD_LOG_ERROR(result);
				return;
			}

			framework_type type = (framework_type)ftype;
			if (type == framework_type::DEFAULT)
			{
				conn->response(result, strlen(result));
				return;
			}
			else if (type == framework_type::SUB)
			{
				rapidjson::Document doc;
				doc.Parse(result);
				auto handler_name = doc[RESULT].GetString();
				std::unique_lock<std::mutex> lock(mtx_);
				conn_map_.emplace(handler_name, conn);
				lock.unlock();
				conn->response(result, strlen(result));
			}
			else if (type == framework_type::PUB)
			{
				pub(topic, result, strlen(result));
				conn->read_head();
			}
		}

		friend class connection;
		io_service_pool io_service_pool_;
		tcp::acceptor acceptor_;
		std::shared_ptr<connection> conn_;
		std::shared_ptr<std::thread> thd_;
		std::size_t timeout_milli_;

		std::multimap<std::string, std::weak_ptr<connection>> conn_map_;
		std::mutex mtx_;
		ThreadPool pool_;
	};

} }