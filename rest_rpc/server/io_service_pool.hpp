#pragma once

namespace timax { namespace rpc 
{
	template <typename CodecPolicy>
	struct response_context
	{
		using codec_policy = CodecPolicy;
		using message_t = typename codec_policy::buffer_type;
		using post_func_t = std::function<void()>;

		response_context(head_t head, message_t message, post_func_t post_func = nullptr)
			: head_(head)
			, message_(std::move(message))
			, post_func_(post_func)
		{
			head_.len = static_cast<uint32_t>(message_.size());
		}

		void apply_post_func() const
		{
			if (post_func_)
				post_func_();
		}

		auto get_message() const
			-> std::vector<boost::asio::const_buffer>
		{
			if (message_.empty())
				return{ boost::asio::buffer(&head_, sizeof(head_t)) };

			return{ boost::asio::buffer(&head_, sizeof(head_t)), boost::asio::buffer(message_) };
		}

		head_t			head_;
		message_t		message_;
		post_func_t		post_func_;
	};

	template <typename CodecPolicy>
	class ios_wrapper
	{
	public:
		using codec_policy = CodecPolicy;
		using connection_t = connection<codec_policy>;
		using connection_ptr = std::shared_ptr<connection_t>;
		using context_t = response_context<codec_policy>;
		using context_ptr = std::shared_ptr<context_t>;
		using context_container_t = std::list<std::pair<connection_ptr, context_ptr>>;
		using ios_work_ptr = std::unique_ptr<io_service_t::work>;

	public:

		ios_wrapper()
			: ios_()
			, ios_work_(std::make_unique<io_service_t::work>(ios_))
			, write_in_progress_(false)
		{
		}

		void start()
		{
			thread_ = std::move(std::thread{ boost::bind(&io_service_t::run, &ios_) });
		}

		void stop()
		{
			ios_work_.reset();
			if (!ios_.stopped())
				ios_.stop();
		}

		void wait()
		{
			if (thread_.joinable())
				thread_.join();
		}

		io_service_t& get_ios() noexcept
		{
			return ios_;
		}

		void response(connection_ptr& conn, context_ptr& ctx)
		{
			lock_t lock{ mutex_ };
			if (!write_in_progress_)
			{
				write_in_progress_ = true;
				lock.unlock();
				response_progress_entry(conn, ctx);
			}
			else
			{
				delay_messages_.emplace_back(conn, ctx);
			}
		}

	private:
		void response_progress_entry(connection_ptr& conn, context_ptr& ctx)
		{
			async_write(conn->socket(), ctx->get_message(), boost::bind(&ios_wrapper::handle_response_entry,
				this, conn, ctx, boost::asio::placeholders::error));
		}

		void response_progress()
		{
			lock_t lock{ mutex_ };
			if (delay_messages_.empty())
			{
				write_in_progress_ = false;
				return;
			}
			else
			{
				context_container_t delay_messages = std::move(delay_messages_);
				lock.unlock();
				response_progress(std::move(delay_messages));
			}
		}

		void response_progress(context_container_t delay_message)
		{
			connection_ptr conn; context_ptr ctx;
			std::tie(conn, ctx) = std::move(delay_message.front());
			delay_message.pop_front();
			async_write(conn->socket(), ctx->get_message(), boost::bind(&ios_wrapper::handle_response_progress,
				this, conn, ctx, std::move(delay_message), boost::asio::placeholders::error));
		}

	private:
		void handle_response_entry(connection_ptr conn, context_ptr ctx, boost::system::error_code const& error)
		{
			if (!conn->socket().is_open())
				return;

			if (!error)
			{
				ctx->apply_post_func();
			}
			ctx.reset(); // release as soon as possible
			
			if(!error)
			{
				response_progress();
			}
		}

		void handle_response_progress(connection_ptr conn, context_ptr ctx, context_container_t delay_messages, boost::system::error_code const& error)
		{
			if (!conn->socket().is_open())
				return;

			if (!error)
			{
				ctx->apply_post_func();
			}
			ctx.reset();		// release as soon as possible

			if (!error)
			{
				if(!delay_messages.empty())
				{
					response_progress(std::move(delay_messages));
				}
				else
				{
					response_progress();
				}
			}
		}

	private:
		io_service_t				ios_;
		ios_work_ptr				ios_work_;
		std::thread					thread_;
		context_container_t			delay_messages_;
		bool						write_in_progress_;
		mutable std::mutex			mutex_;
	};


	/// A pool of io_service objects.
	template <typename CodecPolicy>
	class io_service_pool : boost::noncopyable
	{
	public:
		using codec_policy = CodecPolicy;
		using ios_wrapper_t = ios_wrapper<codec_policy>;
		using iterator = typename std::list<ios_wrapper_t>::iterator;

	public:
		explicit io_service_pool(size_t pool_size)
			: ios_wrappers_(pool_size)
			, next_io_service_(ios_wrappers_.begin())
		{
		}

		void start()
		{
			for (auto& ios : ios_wrappers_)
				ios.start();
		}

		void stop()
		{
			for (auto& ios : ios_wrappers_)
				ios.stop();

			for (auto& ios : ios_wrappers_)
				ios.wait();
		}

		ios_wrapper_t& get_ios_wrapper()
		{
			auto current = next_io_service_++;
			if (ios_wrappers_.end() == next_io_service_)
			{
				next_io_service_ = ios_wrappers_.begin();
			}

			return *current;
		}

	private:
		std::list<ios_wrapper_t>		ios_wrappers_;
		iterator						next_io_service_;
	};
} }