#pragma once

#include "detail/connection.hpp"
#include "detail/wait_barrier.hpp"
#include "detail/async_rpc_session.hpp"
#include "detail/async_sub_session.hpp"

namespace timax { namespace rpc
{
	template <typename Marshal>
	class async_client : public boost::enable_shared_from_this<async_client<Marshal>>
	{
		using client_ptr = boost::shared_ptr<async_client>;
		using client_weak = boost::weak_ptr<async_client>;
		using marshal_policy = Marshal;
		using lock_t = std::unique_lock<std::mutex>;
		using work_ptr = std::unique_ptr<io_service_t::work>;
		using sub_session_container = std::map<std::string, std::shared_ptr<sub_session>>;

		/******************* wrap context with type information *********************/
		template <typename Ret, typename ProtoRet>
		friend class rpc_task;

		template <typename Ret, typename ProtoRet>
		class rpc_task
		{
			template <typename Ret0, typename ProtoRet0>
			friend class rpc_task;

		public:
			using result_type = Ret;
			using protocol_result_type = ProtoRet;
			using funtion_t = std::function<result_type(void)>;
			using context_ptr = rpc_session::context_ptr;
			using result_barrier_t = result_barrier<result_type>;
			using result_barrier_ptr = boost::shared_ptr<result_barrier_t>;

		public:
			rpc_task(client_ptr client, context_ptr ctx)
				: client_(client)
				, ctx_(ctx)
				, dismiss_(false)
			{
				set_function<result_type>();
			}

			template <typename Ret0, typename F>
			rpc_task(rpc_task<Ret0, protocol_result_type>& other, F&& f)
				: client_(other.client_)
				, ctx_(other.ctx_)
				, dismiss_(false)
			{
				auto inner_f = std::move(other.func_);
				func_ = [inner_f, f]()
				{
					return f(inner_f());
				};
			}

			template <typename F>
			rpc_task(rpc_task<void, protocol_result_type>& other, F&& f)
				: client_(other.client_)
				, ctx_(other.ctx_)
				, dismiss_(false)
			{
				auto inner_f = std::move(other.func_);
				func_ = [inner_f, f]()
				{
					inner_f();
					return f();
				};
			}

			~rpc_task()
			{
				if (!dismiss_)
				{
					do_call_managed();
				}
			}
		
			template <typename F>
			auto then(F&& f)
			{
				using next_triats_type = function_traits<std::remove_reference_t<F>>;
				using next_result_type = typename next_triats_type::return_type;
				using next_rpc_task = rpc_task<next_result_type, protocol_result_type>;
		
				dismiss_ = true;
				return next_rpc_task{ *this, std::forward<F>(f) };
			}

			void wait()
			{
				do_call_and_wait();
			}

			template <typename = std::enable_if_t<!std::is_same<result_type, void>::value>>
			auto const& get()
			{
				if (!result_barrier_->complete())
					do_call_and_wait();

				return result_barrier_->get_result();
			}
		
		private:
			void do_call_managed()
			{
				auto client = client_.lock();
				if (client)
				{
					auto f = std::move(func_);
					ctx_->func = [f]() { f(); };
					client->template call_impl<protocol_result_type>(ctx_);
				}
			}

			void do_call_and_wait()
			{
				result_barrier_ = boost::make_shared<result_barrier_t>();

				std::function<void()> f = [this]
				{
					result_barrier_->apply(func_);
				};

				dismiss_ = true;
				auto client = client_.lock();
				if (client)
				{
					ctx_->func = std::move(f);
					client->template call_impl<protocol_result_type>(ctx_);
					result_barrier_->wait();
				}
			}

			template <typename Ret0>
			auto set_function() -> std::enable_if_t<!std::is_same<void, Ret0>::value>
			{
				auto ctx = ctx_;
				func_ = [ctx]
				{
					marshal_policy mp;
					return mp.template unpack<result_type>(ctx->rep.data(), ctx->rep.size());
				};
			}

			template <typename Ret0>
			auto set_function() ->std::enable_if_t<std::is_same<void, Ret0>::value>
			{
			}

		private:
			client_weak				client_;
			context_ptr				ctx_;
			funtion_t				func_;
			result_barrier_ptr		result_barrier_;
			bool					dismiss_;
		};
		/******************* wrap context with type information *********************/

	public:
		async_client(std::string address, std::string port)
			: ios_()
			, ios_work_(std::make_unique<io_service_t::work>(ios_))
			, ios_run_thread_(boost::bind(&io_service_t::run, &ios_))
			, address_(std::move(address))
			, port_(std::move(port))
			, rpc_session_(ios_, address_, port_)
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