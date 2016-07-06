#pragma once

#include <string>
#include <list>
#include <type_traits>
#include <kapok/Kapok.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "../consts.h"
#include "../common.h"
#include "../function_traits.hpp"
#include "../protocol.hpp"

namespace std
{
	template<bool Test, class T = void>
	using disable_if_t = typename enable_if<!Test, T>::type;
}

namespace timax { namespace rpc { namespace policy 
{
	class task_thread_safety
	{
	public:
		auto get_lock() const
		{
			return std::unique_lock<std::mutex>{ mutex_ };
		}

		auto wait() const
		{
			auto lock = get_lock();
			cond_.wait(lock);
			return std::move(lock);
		}

	private:
		mutable	std::mutex					mutex_;
		mutable std::condition_variable		cond_;
	};
} } }

// task
namespace timax { namespace rpc 
{
	template <typename ClientThreadSafepolict, typename TaskThreadSafePolicy = void>
	class async_client;

	template <typename AsyncClient>
	struct is_task_thread_safe;

	template <typename TaskThreadSafepolict, typename ClientThreadSafePolicy>
	struct is_task_thread_safe<async_client<TaskThreadSafepolict, ClientThreadSafePolicy>> : std::true_type
	{
	};

	template <typename ClientThreadSafePolicy>
	struct is_task_thread_safe<async_client<void, ClientThreadSafePolicy>> : std::false_type
	{
	};

	template <typename AsyncClient>
	struct is_client_thread_safe;

	template <typename TaskThreadSafepolict, typename ClientThreadSafePolicy>
	struct is_client_thread_safe<async_client<TaskThreadSafepolict, ClientThreadSafePolicy>> : std::true_type
	{
	};

	template <typename TaskThreadSafepolict>
	struct is_client_thread_safe<async_client<TaskThreadSafepolict, void>> : std::false_type
	{
	};

	using deadline_timer_t = boost::asio::deadline_timer;
	using io_service_t = boost::asio::io_service;
	using tcp = boost::asio::ip::tcp;

	enum class task_status
	{
		established,
		processing,
		accomplished,
		aborted,
	};

	struct task_base_t
	{
		explicit task_base_t(io_service_t& io, std::string req)
			: timer(io)
			, status(task_status::established)
			, req(std::move(req))
		{

		}

		deadline_timer_t			timer;
		task_status					status;
		head_t						head;
		std::string					req;
		std::string					rep;
	};


	template <typename ThreadSafePolicy = void>
	struct task : task_base_t
	{	
		using tsp = ThreadSafePolicy;

		task(io_service_t& ios, std::string req)
			: task_base_t(ios, std::move(req))
		{
		}

		auto get_lock() const
		{
			return tsp_.get_lock();
		}

		auto wait() const
		{
			return std::move(tsp_.wait());
		}

		void notify() const
		{
			tsp_.nofity_one();
		}

		template <typename F>
		void do_void(F&& f) const
		{
			auto lock = get_lock();
			f();
		}

		template <typename F>
		void do_return(F&& f) const
		{
			auto lock = get_lock();
			return f();
		}

		tsp		tsp_;
	};

	template <>
	struct task<void> : task_base_t
	{
		using tsp = void;

		task(io_service_t& ios, std::string req)
			: task_base_t(ios, std::move(req))
		{
		}

		void wait() const
		{
			std::unique_lock<std::mutex> lock{ mutex };
			cond.wait(lock);
		}

		void notify() const
		{
			cond.notify_one();
		}

		template <typename F>
		void do_void(F&& f) const
		{
			f();
		}

		template <typename F>
		void do_return(F&& f) const
		{
			return f();
		}

		mutable	std::mutex					mutex;
		mutable std::condition_variable		cond;
	};

	template <typename Proto, typename AsyncClient>
	class task_wrapper
	{
	public:
		using protocol_type = Proto;
		using client_type = AsyncClient;
		using client_ptr = boost::shared_ptr<client_type>;
		using tsp = typename client_type::ttsp;

		using result_type = typename protocol_type::result_type;
		using task_ptr = boost::shared_ptr<task<tsp>>;
		
		task_wrapper(protocol_type const& protocol, task_ptr task, client_ptr client)
			: protocol_(protocol)
			, task_(task)
			, client_(client)
		{

		}

		auto get() const
			-> std::enable_if_t<is_task_thread_safe<client_type>::value, result_type>
		{
			auto lock = std::move(task_->wait());
			return protocol_.parse_json(task_->rep);
		}

		auto get() const
			-> std::disable_if_t<is_task_thread_safe<client_type>::value, result_type>
		{
			task_->wait();
			return protocol_.parse_json(task_->rep);
		}

		void cancle()
		{
			task_->do_void([this] 
			{
				client_->cancle(task_);
			});
		}

	private:
		protocol_type const&		protocol_;
		task_ptr					task_;
		client_ptr					client_;
	};
} }

// implement async_client
namespace timax { namespace rpc
{

	class simple_client_thread_safety
	{
	public:
		template <typename F>
		void do_void(F&& f) const
		{
			std::unique_lock<std::mutex> lock{ mutex_ };
			f();
		}

		template <typename F>
		auto do_return(F&& f) const
		{
			std::unique_lock<std::mutex> lock{ mutex_ };
			return f();
		}

	private:
		mutable std::mutex		mutex_;
	};

	class trivial_client_thread_safety
	{
	public:
		template <typename F>
		void do_void(F&& f) const
		{
			f();
		}

		template <typename F>
		auto do_return(F&& f) const
		{
			return f();
		}
	};

	enum class client_status
	{
		disable = 0,		// client is not available
		ready = 2,			// there is not any task to do, and client is connected
		busy = 3,			// tasks_ is no empty
	};

	template 
	<
		typename ClientThreadSafePolicy = trivial_client_thread_safety, 
		typename TaskThreadSafePolicy	= void
	>
	class async_client
	{
	public:
		using ttsp = TaskThreadSafePolicy;
		using ctsp = ClientThreadSafePolicy;
		using this_type = async_client<ttsp, ctsp>;
		using task_type = task<ttsp>;
		using task_ptr = boost::shared_ptr<task_type>;

	public:
		async_client(io_service_t& ios, std::string address, std::string port)
			: ios_(ios)
			, socket_(ios)
			, resolver_(ios)
			, timer_(ios)
			, address_(std::move(address))
			, port_(std::move(port))
			, status_(client_status::disable)
			, connected_(false)
		{

		}

		~async_client()
		{

		}

		template <typename Proto, typename ... Args>
		static task_ptr make_task(io_service_t& ios, Proto const& proto, Args&& ... args)
		{
			return boost::make_shared(ios, std::move(proto.make_json(std::forward<Args>(args)...)));
		}

		template <typename Proto, typename ... Args>
		auto call(Proto const& proto, Args&& ... args)
			-> task_wrapper<Proto, this_type>
		{
			auto task = make_task(ios_, proto, std::forward<Args>(args)...);

			task->head =
			{
				static_cast<int16_t>(data_type::JSON),
				static_cast<int16_t>(proto.get_type()),
				static_cast<int32_t>(task->req.length)
			};

			if (!socket_.is_open())
			{
				thread_safe_policy_.do_void([this, task]
				{
					if (!connecting_)
					{
						connecting_ = true;
						status_ = client_status::busy;
			
						tcp::resolver::query q = { tcp::v4(), address_, port_ };
						resolver_.async_resolve(q, boost::bind(&async_client::handle_resolve, shared_from_this(),
							task, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
			
						setup_timeout(task, timer_);
					}
					else
					{
						assert(status_ == client_status::busy);
						tasks_.push(task);
					}
				});
			}
			else
			{
				do_call(task);
			}

			return{ proto, task, shared_from_this() };
		}

		void cancle(task_ptr task)
		{
			thread_safe_policy_.do_void([this, task] 
			{
				task->timer.cancel();

				auto itr = std::find(tasks_.begin(), tasks_.end(), task);
				if (tasks_.end() == itr)
				{
					// In this situation, task is in progress, we need cancle socket
					if (current_ == task && task->status == task_status::processing)
					{
						current_.reset();
						socket_.cancel();
					}
				}
				else
				{
					tasks_.erase(itr);
				}

				task->status = task_status::aborted;
			});	
		}

	private:
		void check_system_error(boost::system::error_code const& error)
		{
			if (error && boost::system::errc::operation_canceled != error)
			{
				// TO DO : log error
				destroy(error);
				throw boost::system::system_error{ error };
			}
		}

		void handle_timeout(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(error);

			auto errc = boost::system::errc::make_error_code(boost::system::errc::timed_out);
			thread_safe_policy_.do_void([this, task]
			{
				if (connecting_)
				{
					// connecting to server time out
					destroy(errc);
				}
				else
				{
					cancle_private(task);
				}
			});
			
			task->cond.notify_one();
		}

		void handle_resolve(task_ptr task, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			check_system_error(error);

			// log try to connect
			boost::asio::async_connect(socket_, endpoint_iterator, boost::bind(&async_client::handle_connect, shared_from_this(),
				task, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void handle_connect(task_ptr task, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			check_system_error(error);
			timer_.cancel();
			thread_safe_policy_.do_void([this] { connecting_ = false; });
			start_send_recv(task);		// we do the task directly
		}

		void handle_write(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(error);
		}

		void handle_read_head(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(error);

			task->rep.resize(task->head.len);
			boost::asio::async_read(socket_, boost::asio::buffer(task->rep),
				boost::bind(&async_client::handle_read_body, shared_from_this(),
					task, boost::asio::placeholders::error));
		}

		void handle_read_body(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(error);
			task->timer.cancle();
			task->do_void([task] { task->status = task_status::accomplished; });
			task->notify();

			auto next_task = thread_safe_policy_.do_return([this]() -> task_ptr
			{
				assert(client_status::busy == status);
				if (tasks_.empty())
				{
					status_ == client_status::ready;
					current_.reset();
					return task_ptr{ nullptr };
				}

				auto task = tasks_.front();
				tasks_.pop_front();
				current_ = task;
				return task;
			});

			if (!next_task)
			{
				start_send_recv(next_task);
			}
		}

		void setup_timeout(task_ptr task, deadline_timer_t& timer)
		{
			timer.cancel();
			timer.expires_from_now(boost::posix_time::milliseconds{ 1000 });
			timer.async_wait(boost::bind(&async_client::handle_timeout,
				shared_from_this(), task, boost::asio::placeholders::error));
		}

		void do_call(task_ptr task)
		{
			if (!thread_safe_policy_.do_return([this, task]() -> bool 
			{
				assert(client_status::disable != status_);
				if (client_status::busy == status_)
				{
					tasks_.push_back(task);
					return true;
				}

				current_ = task;
				status_ = client_status::busy;
				return false;
			}));
			{
				start_send_recv(task);
			}
		}

		void start_send_recv(task_ptr task)
		{
			task.do_void([task] 
			{
				task->status = task_status::processing;
			});

			send(task);
			receive(task);
			setup_timeout(task, task->timer)
		}

		void send(task_ptr task)
		{
			std::vector<boost::asio::const_buffer> message;
			message.push_back(boost::asio::buffer(&task->head, sizeof(head_t)));
			message.push_back(boost::asio::buffer(task->req));

			boost::asio::async_write(socket_, message, boost::bind(&async_client::handle_write,
				shared_from_this(), task, boost::asio::placeholders::error));
		}

		void receive(task_ptr task)
		{
			boost::asio::async_read(socket_, boost::asio::buffer(&task->head, sizeof(head_t)),
				boost::bind(&async_client::handle_read_head, shared_from_this(),
					task, boost::asio::placeholders::error));
		}

		void destroy(boost::system::error_code const& error)
		{
			// TO DO ...  log out error
			resolver_.cancel();
			timer_.cancel();

			thread_safe_policy_.do_void([this] 
			{
				current_->timer.cancel();
				current_->do_void([this] 
				{	
					if (task_status::processing == current_->status)
					{
						socket_.cancel();
						current_->status = task_status::aborted;
						current_->notify();
					}
				});
				current_.reset();

				for (auto task : tasks_)
				{
					task.do_void([task]
					{
						assert(task_status::established == task->status);
						task->status = task_status::aborted;
						task->notify();
					});
				}

				tasks_.clear();
				status_ = client_status::disable;
			});
		}

		void cancle_private(task_ptr task)
		{
			// this line of code is thread safe
			task->timer.cancel();

			// task is not added to the tasks_
			auto itr = std::find(tasks_.begin(), tasks_.end(), task);
			if (tasks_.end() != itr)
			{
				tasks_.erase(itr);
			}
			else if(current_ == task)
			{
				task->do_void([this, task] 
				{
					// In this situation, task is in progress, we need cancle socket
					if (!task_status::processing == task->status)
					{
						socket_.cancel();
						current_->status = task_status::aborted;
						current_->notify();
						current_.reset();
					}
				});
			}
		}

	private:
		io_service_t&			ios_;
		tcp::socket				socket_;
		tcp::resolver			resolver_;
		deadline_timer_t		timer_;
		std::string				address_;
		std::string				port_;
		client_status			status_;
		bool					connecting_;
		std::list<task_ptr>		tasks_;
		task_ptr				current_;
		ctsp					thread_safe_policy_;
	};
} }