#pragma once

namespace timax { namespace rpc 
{
	template<typename Decode>
	class server : private boost::noncopyable, public std::enable_shared_from_this<server<Decode>>
	{
	public:
		using connection_t = connection<Decode>;
		using connection_ptr = std::shared_ptr<connection_t>;

	public:
		server(short port, size_t size, size_t timeout_milli = 0) : io_service_pool_(size), timeout_milli_(timeout_milli),
			acceptor_(io_service_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
		{
			register_handler(SUB_TOPIC, &server::sub, this, [this](auto conn, std::string const& topic)
			{
				if (!topic.empty())
				{
					std::unique_lock<std::mutex> lock(mtx_);
					conn_map_.emplace(topic, sub_connection{ false, conn });
				}
			});

			register_handler(SUB_CONFIRM, &server::sub, this, [this](auto conn, std::string const& topic)
			{
				if (!topic.empty())
				{
					std::unique_lock<std::mutex> lock(mtx_);
					auto range = conn_map_.equal_range(topic);

					for (auto it = range.first; it != range.second; ++it)
					{
						auto ptr = it->second.wp.lock();
						if (!ptr || ptr.get() == conn.get())
						{
							it->second.has_confirm = true;
							break;
						}
					}
				}
			});

			register_handler(HEART_BEAT, [] {});
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

		template<typename Result>
		void pub(const std::string& topic, Result&& result)
		{
			auto r = codec_type().pack(std::forward<Result>(result));
			pub(topic, r.data(), r.size());
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
				if (!it->second.has_confirm)
					continue;

				auto ptr = it->second.wp.lock();
				if(ptr)
					ptr->response(share_data.get(), size);
				//pool_.post([ptr, share_data, size] { ptr->response(share_data.get(), size); });
			}
		}

		void set_disconnect_handler(const std::function<void(connection_t*)>& handler)
		{
			handle_disconnect_ = handler;
		}

		void remove_sub_conn(connection_t* conn)
		{
			if (handle_disconnect_)
				handle_disconnect_(conn);

			std::unique_lock<std::mutex> lock(mtx_);
			for (auto it = conn_map_.cbegin(); it != conn_map_.end();)
			{
				auto ptr = it->second.wp.lock();
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

	private:
		void do_accept()
		{
			auto new_connection = std::make_shared<connection_t>(
				this->shared_from_this(), io_service_pool_.get_io_service(), timeout_milli_);
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

		//std::string sub_confirm(const std::string& topic)
		//{
		//	std::unique_lock<std::mutex> lock(mtx_);
		//	auto it = conn_map_.find(topic);
		//	if (it == conn_map_.end())
		//		return "";

		//	it->second.has_confirm = true;
		//	
		//	return topic;
		//}

		bool is_subscriber_exsit(const std::string& topic)
		{
			std::unique_lock<std::mutex> lock(mtx_);
			return conn_map_.find(topic) != conn_map_.end();
		}

		template<typename R>
		void default_after(connection_ptr conn, std::add_lvalue_reference_t<std::add_const_t<R>> r)
		{
			Decode codec;
			auto buf = codec.pack(r);
			conn->response(buf.data(), buf.size());
		}

		void default_after(connection_ptr conn)
		{
			conn->read_head();
		}

		template <typename Ret, typename AF>
		void wrap_default_after(AF&& af, connection_ptr conn, std::add_lvalue_reference_t<std::add_const_t<Ret>> ret)
		{
			af(conn, ret);
			default_after<Ret>(conn, ret);
		}

		template <typename AF>
		void wrap_default_after(AF&& af, connection_ptr conn)
		{
			af(conn);
			default_after(conn);
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
			-> std::function<void(connection_ptr)>
		{
			return [this, af](connection_ptr conn)
			{
				wrap_default_after(af, conn);
			};
		}

		template <typename Ret, typename AF>
		auto wrap_after_function_impl(AF&& af, std::false_type)
			-> std::function<void(connection_ptr, std::add_lvalue_reference_t<std::add_const_t<Ret>>)>
		{
			return [this, af](connection_ptr conn, std::add_lvalue_reference_t<std::add_const_t<Ret>> r)
			{
				wrap_default_after<Ret>(af, conn, r);
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
			return [this](connection_ptr conn)
			{
				default_after(conn);
			};
		}

		template <typename Ret>
		auto wrap_after_function_impl(std::false_type)
			-> std::function<void(std::shared_ptr<connection_t>, std::add_lvalue_reference_t<std::add_const_t<Ret>>)>
		{
			return [this](connection_ptr conn, std::add_lvalue_reference_t<std::add_const_t<Ret>> r)
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

		void callback(connection_ptr conn, const char* data, size_t size)
		{
			auto& _router = router<Decode>::get();
			_router.route(conn, data, size);
		}

		struct sub_connection
		{
			bool has_confirm;
			std::weak_ptr<connection_t> wp;
		};

		friend class connection<Decode>;
		io_service_pool io_service_pool_;
		tcp::acceptor acceptor_;
		std::shared_ptr<std::thread> thd_;
		std::size_t timeout_milli_;

		std::function<void(connection_t*)> handle_disconnect_;

		std::multimap<std::string, sub_connection> conn_map_;
		std::mutex mtx_;
		//ThreadPool pool_;
	};

} }
