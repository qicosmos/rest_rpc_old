#pragma once

namespace timax { namespace rpc 
{
	template<typename Decode>
	class server : private boost::noncopyable, public std::enable_shared_from_this<server<Decode>>
	{
		using connection_t = connection<Decode>;
		using connection_ptr = std::shared_ptr<connection_t>;
	public:
		server(short port, size_t size, size_t timeout_milli = 0) : io_service_pool_(size), timeout_milli_(timeout_milli),
			acceptor_(io_service_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
		{
			register_handler(SUB_TOPIC, &server::sub, this, [this](auto conn, const std::string& topic) 
			{
				if (!topic.empty())
				{
					conn_map_.emplace(topic, conn);
				}

				default_after(conn, topic);
			});
		}

		~server()
		{
			io_service_pool_.stop();
			thd_->join();
		}

		void run()
		{
			do_accept();
			thd_ = std::make_shared<std::thread>([this] {io_service_pool_.run(); });
		}

		template<typename Function>
		void register_handler(std::string const & name, const Function& f)
		{
			router<Decode>::get().register_handler(name, f);
		}

		template<typename Function>
		std::enable_if_t<!std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, const std::function<void(std::shared_ptr<connection_t>, typename function_traits<Function>::return_type)>& af)
		{
			if (af == nullptr)
			{
				using return_type = typename function_traits<Function>::return_type;
				std::function<void(std::shared_ptr<connection_t> conn, return_type)> fn = std::bind(&server<Decode>::default_after<return_type>, this, std::placeholders::_1, std::placeholders::_2);
				router<Decode>::get().register_handler(name, f, fn);
			}
			else
			{
				router<Decode>::get().register_handler(name, f, af);
			}
		}

		template<typename Function>
		std::enable_if_t<std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, const std::function<void(std::shared_ptr<connection_t>)>& af)
		{
			if (af == nullptr)
			{
				std::function<void(std::shared_ptr<connection_t> conn)> fn = std::bind(&server<Decode>::default_after, this, std::placeholders::_1);
				router<Decode>::get().register_handler(name, f, fn);
			}
			else
			{
				router<Decode>::get().register_handler(name, f, af);
			}
		}

		template<typename Function, typename Self>
		std::enable_if_t<!std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, Self* self, const std::function<void(std::shared_ptr<connection_t>, typename function_traits<Function>::return_type)>& af)
		{
			if (af == nullptr)
			{
				using return_type = typename function_traits<Function>::return_type;
				std::function<void(std::shared_ptr<connection_t> conn, return_type)> fn = std::bind(&server<Decode>::default_after<return_type>, this, std::placeholders::_1, std::placeholders::_2);
				router<Decode>::get().register_handler(name, f, self, fn);	
			}
			else
			{
				router<Decode>::get().register_handler(name, f, self, af);
			}
		}

		template<typename Function, typename Self>
		std::enable_if_t<std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, Self* self, const std::function<void(std::shared_ptr<connection_t>)>& af)
		{
			if (af == nullptr)
			{
				std::function<void(std::shared_ptr<connection_t> conn)> fn = std::bind(&server<Decode>::default_after, this, std::placeholders::_1);
				router<Decode>::get().register_handler(name, f, self, fn);
			}
			else
			{
				router<Decode>::get().register_handler(name, f, self, af);
			}
		}

		void remove_handler(std::string const& name)
		{
			router<Decode>::get().remove_handler(name);
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

			for (auto it = range.first; it != range.second; ++it)
			{
				auto ptr = it->second.lock();
				if(ptr)
					ptr->response(share_data.get(), size);
				//pool_.post([ptr, share_data, size] { ptr->response(share_data.get(), size); });
			}
		}

		void remove_sub_conn(connection<Decode>* conn)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			for (auto it = conn_map_.cbegin(); it != conn_map_.end();)
			{
				auto ptr = it->second.lock();
				if (!ptr||ptr.get()==conn)
				{
					it = conn_map_.erase(it);
				}
				else
				{
					++it;
				}
			}
		}

	private:
		void do_accept()
		{
			connection_ptr new_connection
			{
				new connection_t
				{ 
					this->shared_from_this(), 
					io_service_pool_.get_io_service(), 
					timeout_milli_ 
				}
			};

			acceptor_.async_accept(new_connection->socket(), [this, new_connection](boost::system::error_code ec)
			{
				if (ec)
				{
					//todo log
				}
				else
				{
					new_connection->start();
				}

				do_accept();
			});
		}

	private:
		std::string sub(const std::string& topic)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			if (!router<Decode>::get().has_handler(topic))
				return "";

			return topic;
		}

		bool is_subscriber_exsit(const std::string& topic)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			return conn_map_.find(topic) != conn_map_.end();
		}

		template<typename R>
		void default_after(std::shared_ptr<connection_t> conn, R r)
		{
			Decode codec;
			auto buf = codec.pack(r);
			conn->response(buf.data(), buf.size());
		}

		void default_after(std::shared_ptr<connection_t> conn)
		{
			conn->read_head();
		}

		void callback(std::shared_ptr<connection_t> conn, const char* data, size_t size)
		{
			auto& _router = router<Decode>::get();
			_router.route(conn, data, size);
		}

		//this callback from router, tell the server which connection sub the topic and the result of handler
		void callback1(const std::string& topic, const char* result, std::shared_ptr<connection_t> conn, int16_t ftype, bool has_error = false)
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

		friend class connection<Decode>;
		io_service_pool io_service_pool_;
		tcp::acceptor acceptor_;
		std::shared_ptr<connection_t> conn_;
		std::shared_ptr<std::thread> thd_;
		std::size_t timeout_milli_;

		std::multimap<std::string, std::weak_ptr<connection_t>> conn_map_;
		std::mutex mtx_;
		ThreadPool pool_;
	};

} }