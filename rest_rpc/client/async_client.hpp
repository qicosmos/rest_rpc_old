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

		/******************* wrap context with type information *********************/
		template <typename Ret>
		friend class rpc_task;

		template <typename Ret>
		class rpc_task
		{
			template <typename Ret0>
			friend class rpc_task;

		public:
			using result_type = Ret;
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
			rpc_task(rpc_task<Ret0>& other, F&& f)
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
			rpc_task(rpc_task<void>& other, F&& f)
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
				using next_rpc_task = rpc_task<next_result_type>;
		
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
					client->call_impl(ctx_);
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
					client->call_impl(ctx_);
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
				func_ = [] {};
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
			, ios_run_thread_([this] { ios_.run(); })
			, address_(std::move(address))
			, port_(std::move(port))
			, rpc_session_(ios_, address_, port_)
		{
		}

		~async_client()
		{
			ios_work_.reset();
			if (ios_run_thread_.joinable())
				ios_run_thread_.join();
		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)
		{
			using result_type = typename Protocol::result_type;
			auto buffer = marshal_policy{}.pack_args(std::forward<Args>(args)...);
			auto ctx = boost::make_shared<rpc_context>(protocol.name(), std::move(buffer));
			return rpc_task<result_type>(this->shared_from_this(), ctx);
		}

		template <typename Protocol, typename F>
		void sub(Protocol const& protocol, F&& f)
		{
			auto buffer = marshal_policy{}.pack_args(protocol.name());
		}

	private:
		void call_impl(boost::shared_ptr<rpc_context> ctx)
		{
			rpc_session_.call(ctx);
		}

	private:
		io_service_t		ios_;
		work_ptr			ios_work_;
		std::thread			ios_run_thread_;
		std::string			address_;
		std::string			port_;
		rpc_session			rpc_session_;
	};
} }