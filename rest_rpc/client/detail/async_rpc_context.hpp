#pragma once

namespace timax { namespace rpc 
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

		using success_function_t = std::function<void(char const*, size_t)>;
		using error_function_t = std::function<void(error_code, char const*, size_t)>;

		rpc_context(
			bool is_void_return,
			std::string const& name,
			std::vector<char>&& request)
			: status(status_t::established)
			, is_void(is_void_return)
			, name(name)
			, req(std::move(request))
		{
			head =
			{
				0, 0, 0,
				static_cast<uint32_t>(req.size() + name.length() + 1)
			};
		}

		rpc_context()
			: status(status_t::established)
			, is_void(true)
		{
			std::memset(&head, 0, sizeof(head_t));
		}

		head_t& get_head()
		{
			return head;
		}

		std::vector<boost::asio::const_buffer> get_send_message() const
		{
			if (head.len > 0)
			{
				return
				{
					boost::asio::buffer(&head, sizeof(head_t)),
					boost::asio::buffer(name.c_str(), name.length() + 1),
					boost::asio::buffer(req)
				};
			}

			return{ boost::asio::buffer(&head, sizeof(head_t)) };
		}

		auto get_recv_message(size_t size)
		{
			rep.resize(size);
			return boost::asio::buffer(rep);
		}

		void ok()
		{
			if (on_ok)
				on_ok(rep.data(), rep.size());

			if (nullptr != barrier)
				barrier->notify();
		}

		void error(error_code errcode)
		{
			if (on_error)
			{
				on_error(errcode, rep.data(), rep.size());
			}

			if (nullptr != barrier)
				barrier->notify();
		}

		void create_barrier()
		{
			if (nullptr == barrier)
				barrier.reset(new result_barrier{});
		}

		void wait()
		{
			if(nullptr != barrier)
				barrier->wait();
		}

		bool complete() const
		{
			return nullptr != barrier && barrier->complete();
		}

		status_t							status;
		//deadline_timer_t					timeout;	// 先不管超时
		head_t								head;
		bool								is_void;
		std::string							name;
		std::vector<char>					req;		// request buffer
		std::vector<char>					rep;		// response buffer
		success_function_t					on_ok;
		error_function_t					on_error;
		std::unique_ptr<result_barrier>		barrier;
	};

	class rpc_call_container
	{
	public:
		using context_t = rpc_context;
		using context_ptr = boost::shared_ptr<context_t>;
		using call_map_t = std::map<uint32_t, context_ptr>;
		using call_list_t = std::list<context_ptr>;

	public:
		rpc_call_container()
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

		void push_void_call(context_ptr ctx)
		{
			call_list_.push_back(ctx);
		}

		void task_calls_from_list(call_list_t& to_calls)
		{
			to_calls = std::move(call_list_);
		}

		bool call_list_empty() const
		{
			return call_list_.empty();
		}

		context_ptr get_call_from_map(uint32_t call_id)
		{
			auto itr = call_map_.find(call_id);
			if (call_map_.end() != itr)
				return itr->second;
			return nullptr;
		}

		void remove_call_from_map(uint32_t call_id)
		{
			auto itr = call_map_.find(call_id);
			if (call_map_.end() != itr)
				call_map_.erase(itr);
		}

		void task_calls_from_map(call_map_t& call_map)
		{
			call_map = std::move(call_map_);
		}

	private:
		call_map_t				call_map_;
		call_list_t				call_list_;
		uint32_t				call_id_;
	};
} }