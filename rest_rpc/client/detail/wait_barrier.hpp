#pragma once 

namespace timax { namespace rpc
{
	class result_barrier
	{
	public:
		result_barrier()
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
} }