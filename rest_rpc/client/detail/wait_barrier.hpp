#pragma once 

namespace timax { namespace rpc
{
	class result_barrier
	{
	public:
		template <typename Pred>
		void wait(Pred&& pred)
		{
			using namespace std::chrono_literals;
			lock_t locker{ mutex_ };
			if (!cond_var_.wait_for(locker, 5s, std::forward<Pred>(pred)))
			{
				std::cout << "WTF" << std::endl;
			}
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