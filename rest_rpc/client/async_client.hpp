#pragma once
#include "../forward.hpp"
#include "detail/async_connection.hpp"
#include "detail/wait_barrier.hpp"
// for rpc in async client
#include "detail/async_rpc_context.hpp"
#include "detail/async_rpc_session.hpp"
#include "detail/async_rpc_session_impl.hpp"
// for rpc in async client

#include "detail/async_sub_session.hpp"

namespace timax { namespace rpc
{
	template <typename Codec>
	class async_client : public std::enable_shared_from_this<async_client<Codec>>
	{
		using client_ptr = std::shared_ptr<async_client>;
		using client_weak = std::weak_ptr<async_client>;
		using codec_policy = Codec;
		using lock_t = std::unique_lock<std::mutex>;
		using work_ptr = std::unique_ptr<io_service_t::work>;
		using sub_session_container = std::map<std::string, std::shared_ptr<sub_session>>;

		/******************* wrap context with type information *********************/
		template <typename Ret>
		friend class rpc_task_base;

		template <typename Ret>
		class rpc_task_base
		{
		public:
			using result_type = Ret;
			using context_ptr = rpc_session::context_ptr;

		public:
			rpc_task_base(client_ptr client, context_ptr ct)
				: client_(client)
				, ctx_(ctx)
				, dismiss_(false)
			{
			}

			~rpc_task_base()
			{
				if (!dismiss_)
				{
					do_call_managed();
				}
			}

			void wait()
			{
				if(ctx_->complete())
					do_call_and_wait();
			}

		private:
			void do_call_managed()
			{
				auto client = client_.lock();
				if (client)
				{
					client->template call_impl<result_type>(ctx_);
				}
			}

			void do_call_and_wait()
			{
				ctx_->create_barrier();
				dismiss_ = true;
				auto client = client_.lock();
				if (nullptr != client)
				{
					client->template call_impl<result_type>(ctx_);
					ctx_->wait();
				}
			}

			template <typename F>
			void set_error_function(F&& f)
			{
				ctx_->on_error = [f](error_code errcode, char const* data, size_t size)
				{
					if (error_code::FAIL == errcode)
					{
						codec_policy codec{};
						auto error_message = codec.template unpack<std::string>(data, size);
						f(client_error{ errcode, std::move(error_message) });
					}
					else
					{
						f(client_error{ errcode, "" });
					}
				};
			}

		private:
			client_weak					client_;
			context_ptr					ctx_;
			bool						dismiss_;
		};

		template <typename Ret>
		class rpc_task : public rpc_task_base<Ret>
		{
		public:
			using base_type = rpc_task_base<Ret>;
			using result_type = Ret;

		public:
			rpc_task(client_ptr client, context_ptr ctx)
				: base_type(client, ctx)
				, result_(new result_type{})
			{
			}

			template <typename F>
			rpc_task& when_ok(F&& f)
			{
				auto result = result_;

				this->ctx_->on_ok = [f, result](char const* data, size_t size)
				{
					codec_policy codec{};
					*result = codec.template unpack<result_type>(data, size);
					f(*result);
				};
				return *this;
			}

			template <typename F>
			rpc_task& when_error(F&& f)
			{
				this->set_error_function(std::forward<F>(f));
				return *this;
			}

			result_type const& get()
			{
				this->wait();
				return *result_;
			}

		private:
			std::shared_ptr<result_type> result_;
		};

		template <>
		class rpc_task<void> : public rpc_task_base<void>
		{
		public:
			using base_type = rpc_task_base<void>;
			using result_type = void;

		public:
			rpc_task(client_ptr client, context_ptr ctx)
				: base_type(client, ctx)
				, result_(new result_type{})
			{
			}

			template <typename F>
			rpc_task& when_ok(F&& f)
			{
				this->ctx_->on_ok = [f](char const* data, size_t size) { f(); };
				return *this;
			}

			template <typename F>
			rpc_task& when_error(F&& f)
			{
				this->set_error_function(std::forward<F>(f));
				return *this;
			}
		};
		
		/******************* wrap context with type information *********************/

	public:
		async_client()
			: ios_()
			, ios_work_(std::make_unique<io_service_t::work>(ios_))
			, ios_run_thread_(boost::bind(&io_service_t::run, &ios_))
		{
		}

		~async_client()
		{
			ios_work_.reset();
			if (!ios_.stopped())
				ios_.stop();
			if (ios_run_thread_.joinable())
				ios_run_thread_.join();
		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)
		{
			using result_type = typename Protocol::result_type;
			auto buffer = protocol.pack_args(marshal_policy{}, std::forward<Args>(args)...);
			auto ctx = boost::make_shared<rpc_context>(protocol.name(), std::move(buffer));
			return rpc_task<result_type, result_type>(this->shared_from_this(), ctx);
		}

		template <typename Protocol, typename F>
		void sub(Protocol const& protocol, F&& f)
		{
			using result_type = typename Protocol::result_type;
			static_assert(!std::is_same<void, result_type>::value, "Can`t subscribe for a topic with void return type!");

			auto buffer = sub_topic.pack_args(marshal_policy{}, protocol.name());

			lock_t locker{ sub_mutex_ };
			if (sub_sessions_.end() == sub_sessions_.find(protocol.name()))
			{
				auto new_sub_session = std::make_shared<sub_session>(
					ios_, std::move(buffer), address_, port_, [f](char const* data, size_t size)
				{
					marshal_policy mp;
					f(mp.template unpack<result_type>(data, size));
				});
				new_sub_session->start();
				sub_sessions_.emplace(protocol.name(), std::move(new_sub_session));
			}
		}

		template <typename Protocol>
		void cancle_sub(Protocol const& protocol)
		{
			lock_t locker{ sub_mutex_ };
			auto itr = sub_sessions_.find(protocol.name());
			if (sub_sessions_.end() != itr)
			{
				itr->second->stop();
				sub_sessions_.erase(itr);
			}
		}

	private:
		template <typename Ret>
		auto call_impl(boost::shared_ptr<rpc_context> ctx) 
			-> std::enable_if_t<!std::is_same<void, Ret>::value>
		{
			rpc_session_.call(ctx);
		}

		template <typename Ret>
		auto call_impl(boost::shared_ptr<rpc_context> ctx)
			-> std::enable_if_t<std::is_same<void, Ret>::value>
		{
			rpc_session_.call_void(ctx);
		}

	private:
		io_service_t				ios_;
		work_ptr					ios_work_;
		std::thread					ios_run_thread_;
		std::string					address_;
		std::string					port_;
		rpc_session					rpc_session_;
		sub_session_container		sub_sessions_;
		std::mutex					sub_mutex_;
	};
} }