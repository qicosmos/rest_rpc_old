#pragma once 

#include "../../forward.hpp"

namespace timax { namespace rpc  { namespace detail 
{
	class result_barrier_base
	{
	public:
		result_barrier_base()
			: complete_(false)
		{

		}

		void wait()
		{
			lock_t locker{ mutex_ };
			cond_var_.wait(locker, [this] { return complete_; });
		}

		void notify()
		{
			complete_ = true;
			cond_var_.notify_one();
		}

		bool complete() const
		{
			return complete_;
		}

	protected:
		std::mutex					mutex_;
		std::condition_variable		cond_var_;
		bool						complete_;
	};

	template <typename Ret>
	class result_barrier : public result_barrier_base
	{
	public:
		using result_type = Ret;
		using base_type = result_barrier_base;

		template <typename F>
		void apply(F&& func)
		{
			result_ = func();
			base_type::notify();
		}

		result_type const& get_result() const
		{
			return result_;
		}

	private:
		result_type							result_;
	};

	template <>
	class result_barrier<void> : public result_barrier_base
	{
	public:
		using result_type = void;
		using base_type = result_barrier_base;

		template <typename F>
		void apply(F&& func)
		{
			func();
			base_type::notify();
		}
	};
} } }