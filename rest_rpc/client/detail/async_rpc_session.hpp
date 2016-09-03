#pragma once

#include "../../forward.hpp"
#include "connection.hpp"

namespace timax { namespace rpc { namespace detail 
{
	struct rpc_context
	{
		enum class status_t
		{
			established,
			processing,
			accomplished,
			aborted,
		};

		rpc_context(
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

		auto get_recv_message(size_t size)
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

	class rpc_call_manager
	{
	public:
		using context_t = rpc_context;
		using context_ptr = boost::shared_ptr<context_t>;
		using call_map_t = std::map<uint32_t, context_ptr>;
		using call_list_t = std::list<context_ptr>;

	public:
		rpc_call_manager()
			: call_id_(0)
		{
		}

		void push_call(context_ptr ctx)
		{
			auto call_id = ++call_id_;
			ctx->get_head().id = call_id;
			call_map_.emplace(call_id, ctx);
			call_list_.push_back(ctx);
		}

		context_ptr pop_call()
		{
			auto to_call = call_list_.front();
			call_list_.pop_front();
			return to_call;
		}

		context_ptr get_call(uint32_t call_id)
		{
			auto itr = call_map_.find(call_id);
			if (call_map_.end() != itr)
				return itr->second;
			return nullptr;
		}

		void remove_call(uint32_t call_id)
		{
			auto itr = call_map_.find(call_id);
			if (call_map_.end() != itr)
				call_map_.erase(itr);
		}

		bool call_empty() const
		{
			return call_list_.empty();
		}

	private:
		call_map_t							call_map_;
		call_list_t							call_list_;
		uint32_t							call_id_;
	};

	class rpc_session
	{
	public:
		using context_ptr = rpc_call_manager::context_ptr;
	public:
		rpc_session(
			io_service_t& ios,
			std::string const& address,
			std::string const& port)
			: ios_(ios)
			, connection_(ios, address, port, [this] { start_rpc_service(); })
			, running_flag_(false)
		{
		}

		~rpc_session()
		{
			stop();
		}

		void call(context_ptr ctx)
		{
			lock_t locker{ mutex_ };
			calls_.push_call(ctx);
		}

	private:
		void stop()
		{
			running_flag_.store(false);
			cond_var_.notify_one();
			if (thread_.joinable())
				thread_.join();
		}

		void start_rpc_service()
		{
			recv_head();
			running_flag_.store(true);
			thread_ = std::move(std::thread{ [this] { send_thread(); } });
		}

		void send_thread()
		{
			while (running_flag_.load())
			{
				lock_t locker{ mutex_ };
				cond_var_.wait(locker, [this] { return !calls_.call_empty() || !running_flag_.load(); });

				if (!running_flag_.load())
					return;

				auto to_call = calls_.pop_call();
				call_impl(to_call);
			}
		}

		void call_impl(context_ptr ctx)
		{
			async_write(connection_.socket(), ctx->get_send_message(), 
				boost::bind(&rpc_session::handle_send, this, boost::asio::placeholders::error));
		}

		void recv_head()
		{
			async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)),
				boost::bind(&rpc_session::handle_recv_head, this, boost::asio::placeholders::error));
		}

		void recv_body()
		{
			auto call_id = head_.id;
			lock_t locker{ mutex_ };
			auto call_ctx = calls_.get_call(call_id);
			locker.unlock();
			if (nullptr != call_ctx && head_.len > 0)
			{
				async_read(connection_.socket(), call_ctx->get_recv_message(head_.len), boost::bind(&rpc_session::handle_recv_body,
					this, call_id, call_ctx, boost::asio::placeholders::error));
			}
		}

	private:  // handlers
		void handle_send(boost::system::error_code const& error)
		{
			if (error)
			{
				SPD_LOG_ERROR("boost::asio::async_write error: {}. Stopping write thread", error.message());
			}
		}

		void handle_recv_head(boost::system::error_code const& error)
		{
			if (!error)
			{
				recv_body();
			}
		}

		void handle_recv_body(uint32_t call_id, context_ptr ctx, boost::system::error_code const& error)
		{
			if (!error)
			{
				if (ctx->func)
					ctx->func();

				lock_t locker{ mutex_ };
				calls_.remove_call(call_id);
			}
		}

	private:
		io_service_t&						ios_;
		async_connection					connection_;
		rpc_call_manager					calls_;	
		std::thread							thread_;
		std::atomic<bool>					running_flag_;
		head_t								head_;
		mutable std::mutex					mutex_;
		mutable std::condition_variable		cond_var_;
	};
} } }