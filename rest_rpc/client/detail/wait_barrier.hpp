#pragma once 

namespace timax { namespace rpc
{
	class result_barrier
	{
	public:
		template <typename Pred>
		void wait(Pred&& pred)
		{
			lock_t locker{ mutex_ };
			cond_var_.wait(locker, std::forward<Pred>(pred));
		}

		void notify()
		{
			cond_var_.notify_one();
		}

	protected:
		std::mutex					mutex_;
		std::condition_variable		cond_var_;
	};
} }