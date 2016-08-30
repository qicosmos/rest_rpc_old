#pragma once

namespace timax { namespace rpc 
{
	template<typename Decode>
	class server : private boost::noncopyable, public std::enable_shared_from_this<server<Decode>>
	{
		using connection_t = connection<Decode>;
		using connection_ptr = std::shared_ptr<connection_t>;

		template <typename Ret>
		struct wrap_after
		{
			template <typename AF>
			static void apply(AF af, connection_ptr conn, Ret r)
			{
				af(conn, std::forward<Ret>(r));
				Decode codec;
				auto buf = codec.pack(r);
				conn->response(buf.data(), buf.size());
			}
		};

		template <>
		struct wrap_after<void>
		{
			template <typename AF>
			static void apply(AF&& af, connection_ptr conn)
			{
				af(conn);
				conn->read_head();
			}
		};

	public:
		server(short port, size_t size, size_t timeout_milli = 0) : io_service_pool_(size), timeout_milli_(timeout_milli),
			acceptor_(io_service_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
		{
			register_handler(SUB_TOPIC, &server::sub, this, [this](auto conn, std::string const& topic)
			{
				if (!topic.empty())
				{
					std::unique_lock<std::mutex> lock(mtx_);
					conn_map_.emplace(topic, conn);
				}
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

		// test ...... 
		template <typename F, typename AF>
		void register_handler(std::string const& name, F&& f, AF&& af)
		{
			using return_type = typename function_traits<std::remove_reference_t<F>>::return_type;
			auto after_wrapper = wrap_after_function<return_type>(std::forward<AF>(af));
			register_handler_impl(name, std::forward<F>(f), std::move(after_wrapper));
		}

		template <typename F>
		void register_handler(std::string const& name, F&& f)
		{
			using return_type = typename function_traits<std::remove_reference_t<F>>::return_type;
			auto after_wrapper = wrap_after_function<return_type>();
			register_handler_impl(name, std::forward<F>(f), std::move(after_wrapper));
		}

		template <typename AF, typename T, typename Ret, typename ... Args>
		void register_handler(std::string const& name, Ret(T::*f)(Args...), T* ptr, AF&& af)
		{
			auto after_wrapper = wrap_after_function<Ret>(std::forward<AF>(af));
			register_handler_impl(name, f, ptr, std::move(after_wrapper));
		}

		template <typename T, typename Ret, typename ... Args>
		void register_handler(std::string const& name, Ret(T::*f)(Args...), T* ptr)
		{
			auto after_wrapper = wrap_after_function<Ret>();
			register_handler_impl(name, f, ptr, std::move(after_wrapper));
		}
		// test ......
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
		void default_after(std::shared_ptr<connection_t> conn, std::add_lvalue_reference_t<std::add_const_t<R>> r)
		{
			Decode codec;
			auto buf = codec.pack(r);
			conn->response(buf.data(), buf.size());
		}

		void default_after(std::shared_ptr<connection_t> conn)
		{
			conn->read_head();
		}

		/////////////////////////////////////////////////////////////////////////////////////////////
		// wrap after function
		template <typename Ret, typename AF>
		auto wrap_after_function(AF&& af)
		{
			return wrap_after_function_impl<Ret>(af,
				std::conditional_t<std::is_void<Ret>::value,
				std::true_type, std::false_type>{});
		}

		template <typename Ret, typename AF>
		auto wrap_after_function_impl(AF&& af, std::true_type)
			-> std::function<void(std::shared_ptr<connection_t>)>
		{
			return [af](std::shared_ptr<connection_t> conn)
			{
				wrap_after<Ret>::apply(af, conn);
			};
		}

		template <typename Ret, typename AF>
		auto wrap_after_function_impl(AF&& af, std::false_type)
			-> std::function<void(std::shared_ptr<connection_t>, std::add_lvalue_reference_t<std::add_const_t<Ret>>)>
		{
			return [af](std::shared_ptr<connection_t> conn, std::add_lvalue_reference_t<std::add_const_t<Ret>> r)
			{
				wrap_after<Ret>::apply(af, conn, r);
			};
		}

		template <typename Ret>
		auto wrap_after_function()
		{
			return wrap_after_function_impl<Ret>(
				std::conditional_t<std::is_void<Ret>::value,
				std::true_type, std::false_type>{});
		}

		template <typename Ret>
		auto wrap_after_function_impl(std::true_type)
			-> std::function<void(std::shared_ptr<connection_t>)>
		{
			return [this](std::shared_ptr<connection_t> conn)
			{
				default_after(conn);
			}
		}

		template <typename Ret>
		auto wrap_after_function_impl(std::false_type)
			-> std::function<void(std::shared_ptr<connection_t>, std::add_lvalue_reference_t<std::add_const_t<Ret>>)>
		{
			return [this](std::shared_ptr<connection_t> conn, std::add_lvalue_reference_t<std::add_const_t<Ret>> r)
			{
				default_after<Ret>(conn, r);
			};
		}

		template <typename F, typename AF>
		void register_handler_impl(std::string const& name, F&& f, AF&& af)
		{
			router<Decode>::get().register_handler(name, std::forward<F>(f), std::forward<AF>(af));
		}

		template <typename AF, typename T, typename Ret, typename ... Args>
		void register_handler_impl(std::string const& name, Ret(T::*f)(Args...), T* ptr, AF&& af)
		{
			router<Decode>::get().register_handler(name, f, ptr, std::forward<AF>(af));
		}

		/////////////////////////////////////////////////////////////////////////////////////////////

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
		std::shared_ptr<std::thread> thd_;
		std::size_t timeout_milli_;

		std::multimap<std::string, std::weak_ptr<connection_t>> conn_map_;
		std::mutex mtx_;
		ThreadPool pool_;
	};

} }


//template<typename Function>
//void register_handler(std::string const & name, const Function& f)
//{
//	router<Decode>::get().register_handler(name, f);
//}
//
//template<typename Function>
//std::enable_if_t<!std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, const std::function<void(std::shared_ptr<connection_t>, typename function_traits<Function>::return_type)>& af)
//{
//	if (af == nullptr)
//	{
//		using return_type = typename function_traits<Function>::return_type;
//		std::function<void(std::shared_ptr<connection_t> conn, return_type)> fn = std::bind(&server<Decode>::default_after<return_type>, this, std::placeholders::_1, std::placeholders::_2);
//		router<Decode>::get().register_handler(name, f, fn);
//	}
//	else
//	{
//		router<Decode>::get().register_handler(name, f, af);
//	}
//}
//
//template<typename Function>
//std::enable_if_t<std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, const std::function<void(std::shared_ptr<connection_t>)>& af)
//{
//	if (af == nullptr)
//	{
//		std::function<void(std::shared_ptr<connection_t> conn)> fn = std::bind(&server<Decode>::default_after, this, std::placeholders::_1);
//		router<Decode>::get().register_handler(name, f, fn);
//	}
//	else
//	{
//		router<Decode>::get().register_handler(name, f, af);
//	}
//}
//
//template<typename Function, typename Self>
//std::enable_if_t<!std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, Self* self, const std::function<void(std::shared_ptr<connection_t>, typename function_traits<Function>::return_type)>& af)
//{
//	if (af == nullptr)
//	{
//		using return_type = typename function_traits<Function>::return_type;
//		std::function<void(std::shared_ptr<connection_t> conn, return_type)> fn = std::bind(&server<Decode>::default_after<return_type>, this, std::placeholders::_1, std::placeholders::_2);
//		router<Decode>::get().register_handler(name, f, self, fn);	
//	}
//	else
//	{
//		router<Decode>::get().register_handler(name, f, self, af);
//	}
//}
//
//template<typename Function, typename Self>
//std::enable_if_t<std::is_void<typename function_traits<Function>::return_type>::value> register_handler(std::string const & name, const Function& f, Self* self, const std::function<void(std::shared_ptr<connection_t>)>& af)
//{
//	if (af == nullptr)
//	{
//		std::function<void(std::shared_ptr<connection_t> conn)> fn = std::bind(&server<Decode>::default_after, this, std::placeholders::_1);
//		router<Decode>::get().register_handler(name, f, self, fn);
//	}
//	else
//	{
//		router<Decode>::get().register_handler(name, f, self, af);
//	}
//}