#pragma once
#include "detail/async_connection.hpp"
#include "detail/wait_barrier.hpp"
// for rpc in async client
#include "detail/async_rpc_context.hpp"
#include "detail/async_rpc_session.hpp"
#include "detail/async_rpc_session_impl.hpp"
// for rpc in async client

//#include "detail/async_sub_session.hpp"

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
		//using sub_session_container = std::map<std::string, std::shared_ptr<sub_session>>;

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
			rpc_task_base(client_ptr client, context_ptr ctx)
				: client_(client)
				, ctx_(ctx)
				, dismiss_(false)
			{
				set_error_function();
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
					client->call_impl(ctx_);
				}
			}

			void do_call_and_wait()
			{
				ctx_->create_barrier();
				dismiss_ = true;
				auto client = client_.lock();
				if (nullptr != client)
				{
					client->call_impl(ctx_);
					ctx_->wait();
				}
			}

			void set_error_function()
			{
				auto ctx_ptr = ctx_.get();
				ctx_ptr->error_func = [ctx_ptr](error_code code, char const* data, size_t size)
				{
					//std::cout << "enter error function" << std::endl;
					if (error_code::FAIL == code)
					{
						codec_policy codec{};
						ctx_ptr->err.set_message(std::move(
							codec.template unpack<std::string>(data, size)));
					}
					ctx_ptr->err.set_code(code);
					//std::cout << "leave error function" << std::endl;
				};
			}

		protected:
			client_weak						client_;
			context_ptr						ctx_;
			bool							dismiss_;
		};

		template <typename Ret, typename = void>
		class rpc_task : public rpc_task_base<Ret>
		{
		public:
			using base_type = rpc_task_base<Ret>;
			using result_type = Ret;
			using context_ptr = typename base_type::context_ptr;

		public:
			rpc_task(client_ptr client, context_ptr ctx)
				: base_type(client, ctx)
			{
			}

			template <typename F>
			rpc_task& when_ok(F&& f)
			{
				if (nullptr == result_)
					result_.reset(new result_type);

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
				this->ctx_->on_error = std::forward<F>(f);
				return *this;
			}

			result_type const& get()
			{
				this->wait();

				return *result_;
			}

		private:
			std::shared_ptr<result_type>	result_;
		};

		template <typename Dummy>
		class rpc_task<void, Dummy> : public rpc_task_base<void>
		{
		public:
			using base_type = rpc_task_base<void>;
			using result_type = void;
			using context_ptr = typename base_type::context_ptr;

		public:
			rpc_task(client_ptr client, context_ptr ctx)
				: base_type(client, ctx)
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
				this->ctx_->on_error = std::forward<F>(f);
				return *this;
			}
		};
		
		/******************* wrap context with type information *********************/

	public:
		async_client()
			: ios_()
			, ios_work_(std::make_unique<io_service_t::work>(ios_))
			, ios_run_thread_(boost::bind(&io_service_t::run, &ios_))
			, rpc_manager_(ios_)
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
		auto call(tcp::endpoint endpoint, Protocol const& protocol, Args&& ... args)
		{
			using result_type = typename Protocol::result_type;
			auto buffer = protocol.pack_args(codec_policy{}, std::forward<Args>(args)...);
			
			auto ctx = std::make_shared<rpc_context>(
				endpoint,
				protocol.name(),
				std::move(buffer));

			return rpc_task<result_type>{ this->shared_from_this(), ctx };
		}

	private:

		void call_impl(std::shared_ptr<rpc_context>& ctx)
		{
			rpc_manager_.call(ctx);
		}

	private:
		io_service_t				ios_;
		work_ptr					ios_work_;
		std::thread					ios_run_thread_;
		rpc_manager					rpc_manager_;
	};
} }