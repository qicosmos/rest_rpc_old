#pragma once

#include "../forward.hpp"

namespace timax { namespace rpc 
{
	class ios_wrapper
	{
	public:
		using post_func_t = std::function<void()>;
		using connection_ptr = std::shared_ptr<connection>;

		struct context_t
		{
			context_t() = default;

			context_t(head_t const& h, message_t msg, post_func_t postf)
				: head(h)
				, message(std::move(msg))
				, post_func(std::move(postf))
			{
				head.len = static_cast<uint32_t>(message.size());
			}

			void apply_post_func() const
			{
				if (post_func)
					post_func();
			}

			auto get_message() const
				-> std::vector<boost::asio::const_buffer>
			{
				if (message.empty())
					return{ boost::asio::buffer(&head, sizeof(head_t)) };

				return{ boost::asio::buffer(&head, sizeof(head_t)), boost::asio::buffer(message) };
			}

			head_t			head;
			message_t		message;
			post_func_t		post_func;
		};

		using context_container_t = std::list<std::pair<connection_ptr, context_t*>>;
		using ios_work_ptr = std::unique_ptr<io_service_t::work>;

	public:
		ios_wrapper()
			: ios_()
			, ios_work_(std::make_unique<io_service_t::work>(ios_))
			, write_in_progress_(false)
		{		
		}

		~ios_wrapper()
		{
			stop();
			wait();
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

		void write_ok(connection_ptr conn_ptr, message_t&& message, post_func_t&& post_func)
		{
			auto context = new context_t{ conn_ptr->head_, std::move(message), std::move(post_func) };
			write_impl(conn_ptr, context);
		}

		void write_error(connection_ptr conn_ptr, message_t&& message, post_func_t&& post_func)
		{
			auto context = new context_t{ conn_ptr->head_, std::move(message), std::move(post_func) };
			context->head.code = static_cast<int16_t>(result_code::FAIL);
			write_impl(conn_ptr, context);
		}

		io_service_t& get_ios() noexcept
		{
			return ios_;
		}

	private:

		void write_impl(connection_ptr conn_ptr, context_t* context)
		{
			assert(nullptr != context);

			lock_t lock{ mutex_ };
			if (!write_in_progress_)
			{
				write_in_progress_ = true;
				lock.unlock();
				write_progress_entry(conn_ptr, context);
			}
			else
			{
				delay_messages_.emplace_back(conn_ptr, context);
			}
		}

		void write_progress_entry(connection_ptr conn_ptr, context_t* context)
		{
			assert(nullptr != context);
			async_write(conn_ptr->socket(), context->get_message(), boost::bind(
				&ios_wrapper::handle_write_entry, this, conn_ptr, context, asio_error));
		}

		void write_progress()
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
				write_progress(std::move(delay_messages));
			}
		}

		void write_progress(context_container_t delay_messages)
		{
			connection_ptr conn_ptr;
			context_t* context;
			std::tie(conn_ptr, context) = std::move(delay_messages.front());
			delay_messages.pop_front();
			async_write(conn_ptr->socket(), context->get_message(), boost::bind(
				&ios_wrapper::handle_write, this, conn_ptr, context, std::move(delay_messages), boost::asio::placeholders::error));
		}

	private:
		void handle_write_entry(connection_ptr conn_ptr, context_t* context, boost::system::error_code error)
		{
			assert(nullptr != context);
			if (!conn_ptr->socket().is_open())
				return;

			if (!error)
			{
				// call the post function
				context->apply_post_func();
			}
			delete context;
			
			if (!error)
			{
				write_progress();
			}
		}

		void handle_write(connection_ptr conn_ptr, context_t* context, context_container_t delay_messages, boost::system::error_code const& error)
		{
			if (!conn_ptr->socket().is_open())
				return;

			if (!error)
			{
				// call the post function
				context->apply_post_func();
			}
			delete context;

			if(!error)
			{
				if (!delay_messages.empty())
				{
					write_progress(delay_messages);
				}
				else
				{
					write_progress();
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

	class io_service_pool : boost::noncopyable
	{
	public:
		using iterator = std::list<ios_wrapper>::iterator;

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

		ios_wrapper& get_ios_wrapper()
		{
			auto current = next_io_service_++;
			if (ios_wrappers_.end() == next_io_service_)
			{
				next_io_service_ = ios_wrappers_.begin();
			}

			return *current;
		}

	private:
		std::list<ios_wrapper>		ios_wrappers_;
		iterator					next_io_service_;
	};
} }