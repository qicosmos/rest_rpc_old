#pragma once

namespace timax { namespace rpc { namespace detail 
{
	struct call_context
	{
		enum class status_t
		{
			established,
			processing,
			accomplished,
			aborted,
		};

		call_context(
			std::string const& name,
			std::vector<char> req)
			: status(status_t::established)
			, name(name)
			, req(std::move(req))
		{
			head.len = static_cast<uint32_t>(req.size() + name.length() + 1);
		}

		head_t& get_head()
		{
			return head;
		}

		std::vector<boost::asio::const_buffer> get_send_message() const
		{
			return
			{
				boost::asio::buffer(&head, sizeof(head_t)),
				boost::asio::buffer(name.c_str(), name.length() + 1),
				boost::asio::buffer(req)
			};
		}

		boost::asio::mutable_buffer get_recv_message(size_t size)
		{
			rep.resize(size);
			return boost::asio::buffer(rep);
		}

		status_t							status;
		//deadline_timer_t					timeout;	// 先不管超时
		head_t								head;
		std::string							name;
		std::vector<char>					req;		// request buffer
		std::vector<char>					rep;		// response buffer
		std::function<void()>				func;
		mutable	std::mutex					mutex;
		mutable std::condition_variable		cond;
	};

	using context_t = call_context;
	using context_ptr = boost::shared_ptr<context_t>;
	using lock_t = std::unique_lock<std::mutex>;

	class call_container
	{
	public:
		using call_map_t = std::map<uint32_t, context_ptr>;
		using call_list_t = std::list<std::pair<uint32_t, context_ptr>>;

		call_container()
			: call_id_(0)
		{
		}

		void add(context_ptr ctx)
		{
			auto call_id = call_id_.fetch_add(1);
			ctx->get_head().id = call_id;

			lock_t locker{ mutex_ };

			call_map_.emplace(call_id, ctx);
			call_list_.emplace_back(call_id, ctx);
		}

		auto get_lock() const
		{
			return lock_t{ mutex_ };
		}

		context_ptr top_call_context()
		{
			return call_list_.front().second;
		}

		void pop_top_call_context()
		{
			call_list_.pop_front();
		}

		context_ptr get_call_context(uint32_t call_id)
		{
			lock_t locker{ mutex_ };

			auto itr = call_map_.find(call_id);
			if (itr != call_map_.end())
				return itr->second;

			return nullptr;
		}

		void remove_call_context(uint32_t call_id)
		{
			lock_t locker{ mutex_ };

			auto itr = call_map_.find(call_id);
			if (itr != call_map_.end())
				call_map_.erase(itr);
		}

		template <typename Pred>
		auto wait_if_empty_and_if_not(Pred&& pred) const
		{
			lock_t locker{ mutex_ };
			cond_var_.wait(locker, [this, pred]() { return !call_map_.empty() && !pred; });
			return std::move(locker);
		}

		void notify() const
		{
			cond_var_.notify_one();
		}

	private:
		call_map_t							call_map_;
		call_list_t							call_list_;
		mutable std::mutex					mutex_;
		mutable	std::condition_variable		cond_var_;
		std::atomic<uint32_t>				call_id_;
	};

	class send_thread
	{
	public:
		explicit send_thread()
			: running_flag_(false)
		{
		}

		template <typename F>
		void start(F&& f)
		{
			running_flag_.store(true);

			thread_ = std::move(std::thread([f, this]() 
			{
				while (running_flag_.load())
				{
					f();
				}
			}));
		}

		void stop()
		{
			running_flag_.store(false);
		}

		void wait_until_exit()
		{
			if (thread_.joinable())
				thread_.join();
		}

		bool is_running() const
		{
			return running_flag_.load();
		}

	private:
		std::thread				thread_;
		std::atomic<bool>		running_flag_;
	};

	class sub_manager
	{
	public:
		using conn_ptr = boost::shared_ptr<tcp::socket>;
		struct conn_context
		{
			conn_context(conn_ptr ptr)
				: conn(ptr)
			{
			}

			conn_ptr			conn;
			head_t				head;
			std::vector<char>	buffer;
		};
		using conn_ctx_ptr = boost::shared_ptr<conn_context>;
		using channel_map_t = std::map<std::string, conn_ctx_ptr>;

		sub_manager(io_service_t& ios)
			: ios_(ios)
			, resolver_(ios_)
		{
		}

		bool has_topic(std::string const& top)
		{
			lock_t locker{ mutex_ };
			return channels_.find(top) == channels_.end();
		}

		void add_topic(std::string const& topic, conn_ptr ptr, std::string const& address, std::string const& port)
		{
			tcp::resolver::query q = { tcp::v4(), address, port };
			resolver_.async_resolve(q, boost::bind(&sub_manager::handle_resolve, this, topic,
				ptr, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void start_read(conn_ctx_ptr conn_ctx)
		{
			boost::asio::async_read(*conn_ctx->conn, boost::asio::buffer(&conn_ctx->head, sizeof(head_t)),
				boost::bind(&sub_manager::handle_read_head, this, conn_ctx, boost::asio::placeholders::error));
		}

		void handle_resolve(std::string const& topic, conn_ptr ptr, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				boost::asio::async_connect(*ptr, endpoint_iterator, boost::bind(&sub_manager::handle_connect,
					this, ptr, boost::asio::placeholders::error/*, boost::asio::placeholders::iterator*/));
			}
			// TODO handle error
		}

		void handle_connect(std::string const& topic, conn_ptr ptr, boost::system::error_code const& error)
		{
			if (!error)
			{
				auto conn_ctx = boost::make_shared<conn_context>(ptr);

				lock_t locker{ mutex_ };
				auto result = channels_.emplace(topic, conn_ctx);
				if(result.second)
					start_read(conn_ctx);
				locker.unlock();
			}
		}

		void handle_read_head(conn_ctx_ptr conn_ctx, boost::system::error_code const& error)
		{
			if (!error)
			{
				conn_ctx->buffer.resize(conn_ctx->head.len);

				boost::asio::async_read(*conn_ctx->conn, boost::asio::buffer(conn_ctx->buffer),
					boost::bind(&sub_manager::handle_read_body, this, conn_ctx, boost::asio::placeholders::error));
			}
		}

		void handle_read_body(conn_ctx_ptr conn_ctx, boost::system::error_code const& error)
		{

		}

	private:
		io_service_t&		ios_;
		tcp::resolver		resolver_;
		channel_map_t		channels_;
		std::mutex			mutex_;
	};
} } }