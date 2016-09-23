#pragma once

#include "../forward.hpp"

namespace timax { namespace rpc 
{
	class ios_wrapper
	{
	public:
		using post_func_t = std::function<void()>;
		using connection_ptr = std::shared_ptr<connection>;
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

		void write(connection_ptr& conn_ptr, context_ptr& context)
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

		io_service_t& get_ios() noexcept
		{
			return ios_;
		}

	private:
		void write_progress_entry(connection_ptr& conn_ptr, context_ptr& context)
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
			auto& conn_ptr = delay_messages.front().first;
			auto& ctx_ptr = delay_messages.front().second;
			
			async_write(conn_ptr->socket(), ctx_ptr->get_message(), boost::bind(
				&ios_wrapper::handle_write, this, std::move(delay_messages), asio_error));
		}

	private:
		void handle_write_entry(connection_ptr conn_ptr, context_ptr context, boost::system::error_code const& error)
		{
			assert(nullptr != context);
			if (!conn_ptr->socket().is_open())
				return;

			if (error)
			{
				conn_ptr->on_error(error);
			}
			else
			{
				// for test
				//g_succeed_count++;
				// for test

				// call the post function
				context->apply_post_func();

				// release the memory as soon as possible
				context.reset();

				// continue
				write_progress();
			}
		}

		void handle_write(context_container_t delay_messages, boost::system::error_code const& error)
		{
			connection_ptr conn_ptr;
			context_ptr ctx_ptr;
			std::tie(conn_ptr, ctx_ptr) = std::move(delay_messages.front());
			delay_messages.pop_front();

			if (!conn_ptr->socket().is_open())
				return;

			if (error)
			{
				conn_ptr->on_error(error);
			}
			else
			{
				// for test
				//g_succeed_count++;
				// for test

				// call the post function
				ctx_ptr->apply_post_func();

				// release the memory as soon as possible
				ctx_ptr.reset();
				conn_ptr.reset();

				// continue
				if (!delay_messages.empty())
				{
					write_progress(std::move(delay_messages));
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