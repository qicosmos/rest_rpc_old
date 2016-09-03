#pragma once

// develop
#include "../forward.hpp"
// develop

namespace timax { namespace rpc { namespace detail 
{
	struct sub_context
	{
		using conn_ptr = boost::shared_ptr<tcp::socket>;

		sub_context(conn_ptr ptr, std::string topic, std::function<void(char const*, size_t)> f)
			: conn(ptr)
			, topic(std::move(topic))
			, func(std::move(f))
		{
		}

		void apply()
		{
			if (func)
				func(buffer.data(), buffer.size());
		}

		void resize()
		{
			buffer.resize(head.len);
		}

		conn_ptr			conn;
		head_t				head;
		std::string			topic;
		std::vector<char>	buffer;
		std::function<void(char const*, size_t)> func;
	};

	using sub_context_t = sub_context;
	using sub_context_ptr = boost::shared_ptr<sub_context_t>;

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
		using channel_map_t = std::map<std::string, sub_context_ptr>;

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

		void add_topic(sub_context_ptr ctx, std::string const& address, std::string const& port)
		{
			tcp::resolver::query q = { tcp::v4(), address, port };
			resolver_.async_resolve(q, boost::bind(&sub_manager::handle_resolve, this,
				ctx, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void sub_topic()

		void start_read(sub_context_ptr ctx)
		{
			boost::asio::async_read(*ctx->conn, boost::asio::buffer(&ctx->head, sizeof(head_t)),
				boost::bind(&sub_manager::handle_read_head, this, ctx, boost::asio::placeholders::error));
		}

		void handle_resolve(sub_context_ptr ctx, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				boost::asio::async_connect(*ctx->conn, endpoint_iterator, boost::bind(&sub_manager::handle_connect,
					this, ctx, boost::asio::placeholders::error/*, boost::asio::placeholders::iterator*/));
			}
			// TODO handle error
		}

		void handle_connect(sub_context_ptr ctx, boost::system::error_code const& error)
		{
			if (!error)
			{
				lock_t locker{ mutex_ };
				auto result = channels_.emplace(ctx->topic, ctx);
				if(result.second)
					start_read(ctx);
				locker.unlock();
			}
		}

		void handle_read_head(sub_context_ptr ctx, boost::system::error_code const& error)
		{
			if (!error)
			{
				ctx->resize();

				boost::asio::async_read(*ctx->conn, boost::asio::buffer(ctx->buffer),
					boost::bind(&sub_manager::handle_read_body, this, ctx, boost::asio::placeholders::error));
			}
		}

		void handle_read_body(sub_context_ptr ctx, boost::system::error_code const& error)
		{

		}

	private:
		io_service_t&		ios_;
		tcp::resolver		resolver_;
		channel_map_t		channels_;
		std::mutex			mutex_;
	};
} } }