#pragma once

namespace timax { namespace rpc 
{
	template <typename CodecPolicy>
	struct rpc_context
	{
		using codec_policy = CodecPolicy;
		using success_function_t = std::function<void(char const*, size_t)>;
		using on_error_function_t = std::function<void(exception const&)>;

		rpc_context(
			io_service_t& ios,
			tcp::endpoint const& endpoint,
			std::string const& name,
			std::vector<char>&& request)
			: timer(ios)
			, timeout(duration_t::max())
			, endpoint(endpoint)
			, name(name)
			, req(std::move(request))
			, is_over(false)
		{
			head =
			{
				0, 0, 0,
				static_cast<uint32_t>(req.size() + name.length() + 1)
			};
		}

		explicit rpc_context(io_service_t& ios)
			: timer(ios)
			, timeout(duration_t::max())
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

			is_over = true;

			if (nullptr != barrier)
				barrier->notify();
		}

		void error(error_code errcode, char const* message = nullptr)
		{
			err.set_code(errcode);
			if (error_code::FAIL == errcode)
			{
				codec_policy cp{};
				auto error_message = cp.template unpack<std::string>(rep.data(), rep.size());
				err.set_message(std::move(error_message));
			}
			else
			{
				if (nullptr != message)
				{
					err.set_message(message);
				}
			}

			if (on_error)
				on_error(err);

			is_over = true;

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

		steady_timer_t						timer;
		steady_timer_t::duration			timeout;
		tcp::endpoint						endpoint;
		std::string							name;
		head_t								head;
		std::vector<char>					req;		// request buffer
		std::vector<char>					rep;		// response buffer
		exception							err;
		success_function_t					on_ok;
		on_error_function_t					on_error;
		bool								is_over;
		std::unique_ptr<result_barrier>		barrier;
	};

	template <typename CodecPolicy>
	class rpc_call_container
	{
	public:
		using codec_policy = CodecPolicy;
		using context_t = rpc_context<codec_policy>;
		using context_ptr = std::shared_ptr<context_t>;
		using call_map_t = std::map<uint32_t, context_ptr>;
		using call_list_t = std::list<context_ptr>;

	public:
		explicit rpc_call_container(size_t max_size = MAX_QUEUE_SIZE)
			: call_id_(0)
			, max_size_(max_size)
		{
		}

		bool push_call(context_ptr& ctx)
		{
			if (call_map_.size() >= max_size_)
				return false;

			push_call_response(ctx);
			call_list_.push_back(ctx);
			return true;
		}

		void push_call_response(context_ptr& ctx)
		{
			if (ctx->req.size() > 0)
			{
				auto call_id = ++call_id_;
				ctx->get_head().id = call_id;
				call_map_.emplace(call_id, ctx);
			}
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
			{
				context_ptr ctx = itr->second;
				call_map_.erase(itr);
				return ctx;
			}
			return nullptr;
		}

		void remove_call_from_map(uint32_t call_id)
		{
			auto itr = call_map_.find(call_id);
			if(call_map_.end() != itr)
				call_map_.erase(itr);
		}

		void task_calls_from_map(call_map_t& call_map)
		{
			call_map = std::move(call_map_);
		}

		size_t get_call_list_size() const
		{
			return call_list_.size();
		}

		size_t get_call_map_size() const
		{
			return call_map_.size();
		}

	private:
		call_map_t				call_map_;
		call_list_t				call_list_;
		uint32_t				call_id_;
		size_t					max_size_;
	};
} }