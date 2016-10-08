#pragma once
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
	template <typename CodecPolicy>
	class async_client
	{
		using codec_policy = CodecPolicy;
		using lock_t = std::unique_lock<std::mutex>;
		using work_ptr = std::unique_ptr<io_service_t::work>;
		using context_t = rpc_context<codec_policy>;
		using context_ptr = std::shared_ptr<context_t>;

		using rpc_manager_t = rpc_manager<codec_policy>;
		using sub_manager_t = sub_manager<codec_policy>;

		/******************* wrap context with type information *********************/
		template <typename Ret>
		friend class rpc_task_base;

		template <typename Ret>
		class rpc_task_base
		{
		public:
			using result_type = Ret;
			using context_ptr = typename rpc_session<codec_policy>::context_ptr;

		public:
			rpc_task_base(async_client& client, context_ptr ctx)
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

			rpc_task_base(rpc_task_base&& other)
				: client_(other.client_)
				, ctx_(std::move(other.ctx_))
				, dismiss_(other.dismiss_)
			{
				other.dismiss_ = true;
			}

			rpc_task_base(rpc_task_base const&) = delete;
			rpc_task_base& operator=(rpc_task_base const&) = delete;
			rpc_task_base& operator=(rpc_task_base&&) = delete;

			void wait(duration_t duration = duration_t::max()) &
			{ 
				ctx_->timeout = duration;
				do_call_and_wait();
			}

		private:
			void do_call_managed()
			{
				client_.call_impl(ctx_);
			}

			void do_call_and_wait()
			{
				if (!dismiss_)
				{
					dismiss_ = true;
					ctx_->create_barrier();
					client_.call_impl(ctx_);
					ctx_->wait();
				}
			}

		protected:
			async_client&					client_;
			context_ptr					ctx_;
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
			rpc_task(async_client& client, context_ptr ctx)
				: base_type(client, ctx)
			{
			}

			template <typename F>
			rpc_task&& on_ok(F&& f) &&
			{
				if (nullptr == result_)
				{
					//result_.reset(new result_type);
					result_ = std::make_shared<result_type>();
					this->ctx_->on_ok = [f, r = result_](char const* data, size_t size)
					{
						codec_policy codec{};
						*r = codec.template unpack<result_type>(data, size);
						f(*r);
					};
				}
					
				return std::move(*this);
			}

			template <typename F>
			rpc_task&& on_error(F&& f) &&
			{
				this->ctx_->on_error = std::forward<F>(f);
				return std::move(*this);
			}

			rpc_task&& timeout(duration_t t) &&
			{
				this->ctx_->timeout = t;
				return std::move(*this);
			}

			result_type const& get(duration_t duration = duration_t::max()) &
			{
				if (nullptr == result_)
				{
					result_ = std::make_shared<result_type>();
					this->ctx_->on_ok = [r = result_](char const* data, size_t size)
					{
						codec_policy codec{};
						*r = codec.template unpack<result_type>(data, size);
					};
				}

				this->wait(duration);
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
			rpc_task(async_client& client, context_ptr ctx)
				: base_type(client, ctx)
			{
			}

			template <typename F>
			rpc_task&& on_ok(F&& f) &&
			{
				this->ctx_->on_ok = [f](char const* data, size_t size) { f(); };
				return std::move(*this);
			}

			template <typename F>
			rpc_task&& on_error(F&& f) &&
			{
				this->ctx_->on_error = std::forward<F>(f);
				return std::move(*this);
			}

			rpc_task&& timeout(duration_t t) &&
			{
				this->ctx_->timeout = t;
				return std::move(*this);
			}
		};
		
		/******************* wrap context with type information *********************/

	public:
		async_client()
			: ios_()
			, ios_work_(std::make_unique<io_service_t::work>(ios_))
			, ios_run_thread_(boost::bind(&io_service_t::run, &ios_))
			, rpc_manager_(ios_)
			, sub_manager_(ios_)
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
			
			auto ctx = std::make_shared<context_t>(
				ios_,
				endpoint,
				protocol.name(),
				std::move(buffer));

			return rpc_task<result_type>{ *this, ctx };
		}

		template <typename Protocol, typename Func>
		void sub(tcp::endpoint const& endpoint, Protocol const& protocol, Func&& func)
		{
			sub_manager_.sub(endpoint, protocol, std::forward<Func>(func));
		}

		template <typename Protocol, typename Func, typename EFunc>
		void sub(tcp::endpoint const& endpoint, Protocol const& protocol, Func&& func, EFunc&& efunc)
		{
			sub_manager_.sub(endpoint, protocol, std::forward<Func>(func), std::forward<EFunc>(efunc));
		}

		template <typename Protocol, typename Func>
		void remove_sub(tcp::endpoint const& endpoint, Protocol const& protocol)
		{
			//sub_manager_.remove()
		}

	private:

		void call_impl(std::shared_ptr<context_t>& ctx)
		{
			rpc_manager_.call(ctx);
		}

	private:
		io_service_t				ios_;
		work_ptr					ios_work_;
		std::thread				ios_run_thread_;
		rpc_manager_t				rpc_manager_;
		sub_manager_t				sub_manager_;
	};
} }
