#pragma once

#include "../forward.hpp"

namespace timax { namespace rpc { namespace detail 
{
	using deadline_timer_t = boost::asio::deadline_timer;
	using io_service_t = boost::asio::io_service;
	using tcp = boost::asio::ip::tcp;

	template <typename Marshal>
	class async_client : public boost::enable_shared_from_this<async_client<Marshal>>
	{
		using client_ptr = boost::shared_ptr<async_client>;
		using marshal_policy = Marshal;

		/************************* context data for rpc *****************************/
		struct rpc_context
		{
			using function_t = std::function<void(char const*, size_t)>;
			enum class status_t
			{
				established,
				processing,
				accomplished,
				aborted,
			};

			status_t				status;
			deadline_timer_t		timeout;
			head_t					head;
			std::vector<char>		req;		// request buffer
			std::vector<char>		rep;		// response buffer

			function_t				func;
		};

		using context_t = rpc_context;
		using context_ptr = boost::shared_ptr<context_t>;
		/************************* context data for rpc *****************************/

		/******************* wrap context with type information *********************/
		template <typename T>
		class type_rpc_context
		{
		public:
			using result_type = T;
		public:
			explicit type_rpc_context(context_ptr ctx)
				: ctx_(ctx)
			{
			}

			template <typename F>
			void then(F&& f)
			{
				ctx_->func = [f](char const* data, size_t len)
				{
					f(marshal_policy{}.template unpack<result_type>(data, len));
				};
			}

		private:
			context_ptr		ctx_;
		};
		/******************* wrap context with type information *********************/

	public:

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& .. args)
		{

		}

		//template <typename F, typename Protocol, typename ... Args>
		//void call(F&& f, Protocol const& protocol, Args&& ... args)
		//{
		//
		//}

	private:
		std::map<uint32_t, 
	};
} } }

// task
namespace timax { namespace rpc 
{
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

	struct task_t
	{
		explicit task_t(io_service_t& io, std::string req)
			: timer(io)
			, status(task_status::established)
			, req(std::move(req))
		{

		}

		bool is_finished() const
		{
			return status == task_status::aborted || status == task_status::accomplished;
		}

		auto get_lock() const
		{
			return std::unique_lock<std::mutex>{ mutex };
		}

		auto wait() const
		{
			auto lock = get_lock();
			cond.wait(lock, [this] { return is_finished(); });
			return std::move(lock);
		}

		void notify() const
		{
			cond.notify_all();
		}

		void abort()
		{
			{
				auto lock = get_lock();
				if (!is_finished())
					status = task_status::aborted;
			}
			notify();
		}

		deadline_timer_t			timer;
		task_status					status;
		head_t						head;
		std::string					req;
		std::string					rep;

		mutable	std::mutex					mutex;
		mutable std::condition_variable		cond;
	};

	template <typename Proto, typename AsyncClient>
	class task_wrapper
	{
	public:
		using protocol_type = Proto;
		using task_ptr = boost::shared_ptr<task_t>;
		using client_ptr = boost::shared_ptr<AsyncClient>;
		using result_type = typename protocol_type::result_type;
		
		task_wrapper(protocol_type const& protocol, task_ptr task, client_ptr client)
			: protocol_(protocol)
			, task_(task)
			, client_(client)
		{

		}

		result_type get() const
		{
			if (nullptr != task_)
			{
				auto lock = task_->wait();
				if(task_status::accomplished == task_->status)
					return protocol_.parse_json(task_->rep);
			}
			throw;
		}

		void wait() const
		{
			if (nullptr != task_)
			{
				auto lock = task_->wait();
				if (task_status::accomplished == task_->status)
				{
					protocol_.parse_json(task_->rep);
					return;
				}	
			}
			throw;
		}

		void cancel()
		{
			client_->cancel(task_);
			task_.reset();
		}

	private:
		protocol_type				protocol_;
		task_ptr					task_;
		client_ptr					client_;
	};
} }

#define TIMAX_ERROR_THROW_CANCEL_RETURN(e) \
if(e)\
{\
	if (boost::system::errc::operation_canceled == e)\
	{\
		SPD_LOG_INFO(e.message().c_str()); return;\
	}\
	SPD_LOG_ERROR(e.message().c_str());\
	throw boost::system::system_error{ e };\
}

// implement async_client
namespace timax { namespace rpc
{
	enum class client_status
	{
		disable = 0,		// client is not available
		ready = 2,			// there is not any task to do, and client is connected
		busy = 3,			// tasks_ is no empty
	};

	class async_client : public boost::enable_shared_from_this<async_client>
	{
	public:
		using this_type = async_client;
		using task_type = task_t;
		using task_ptr = boost::shared_ptr<task_type>;
		using ptmf_t = void (async_client::*)(task_ptr, boost::system::error_code const&);

	public:
		async_client(io_service_t& ios, std::string address, std::string port)
			: ios_(ios)
			, socket_(ios)
			, resolver_(ios)
			, timer_(ios)
			, address_(std::move(address))
			, port_(std::move(port))
			, status_(client_status::disable)
			, connecting_(false)
		{

		}

		~async_client()
		{
			destroy(boost::system::errc::make_error_code(boost::system::errc::success));
		}

		template <typename Proto, typename ... Args>
		static task_ptr make_task(io_service_t& ios, Proto const& proto, Args&& ... args)
		{
			return boost::make_shared<task_type>(ios, std::move(proto.make_json(std::forward<Args>(args)...)));
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
				static_cast<int32_t>(task->req.length())
			};

			ios_.post([this, task] 
			{
				if (!socket_.is_open())
				{
					if (!connecting_)
					{
						connecting_ = true;
						status_ = client_status::busy;

						tcp::resolver::query q = { tcp::v4(), address_, port_ };
						resolver_.async_resolve(q, boost::bind(&async_client::handle_resolve, shared_from_this(),
							task, boost::asio::placeholders::error, boost::asio::placeholders::iterator));

						setup_timeout(task, timer_, &async_client::handle_connect_timeout);
					}
					else
					{
						assert(status_ == client_status::busy);
						tasks_.push_back(task);
					}
				}
				else
				{
					do_call(task);
				}
			});

			return{ proto, task, shared_from_this() };
		}

		//template <typename Protocol, typename 

		void cancel(task_ptr task)
		{
			task->timer.cancel();
			ios_.post([this, task] 
			{
				task->abort();

				auto itr = std::find(tasks_.begin(), tasks_.end(), task);
				if (tasks_.end() != itr)
				{
					tasks_.erase(itr);
				}
			});
		}

	private:
		void handle_timeout(task_ptr task, boost::system::error_code const& error)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);
			task->abort();
		}

		void handle_connect_timeout(task_ptr task, boost::system::error_code const& error)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);

			if (connecting_)
			{
				auto errc = boost::system::errc::make_error_code(boost::system::errc::timed_out);
				destroy(errc);
			}
	
			task->timer.cancel();
			task->abort();
		}

		void handle_resolve(task_ptr task, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);

			// log try to connect
			boost::asio::async_connect(socket_, endpoint_iterator, boost::bind(&async_client::handle_connect, shared_from_this(),
				task, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void handle_connect(task_ptr task, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);

			timer_.cancel();
			connecting_ = false;
			start_send_recv(task);		// we do the task directly
		}

		void handle_write(task_ptr task, boost::system::error_code const& error)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);
		}

		void handle_read_head(task_ptr task, boost::system::error_code const& error)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);
			
			task->rep.resize(task->head.len + 1);
			boost::asio::async_read(socket_, boost::asio::buffer(&task->rep[0], task->head.len),
				boost::bind(&async_client::handle_read_body, shared_from_this(),
					task, boost::asio::placeholders::error));
		}

		void handle_read_body(task_ptr task, boost::system::error_code const& error)
		{
			TIMAX_ERROR_THROW_CANCEL_RETURN(error);

			task->timer.cancel();
			{
				auto lock = task->get_lock();
				if (!task->is_finished())
					task->status = task_status::accomplished;
			}
			task->notify();

			call_next_if_there_exist();
		}

		void setup_timeout(task_ptr task, deadline_timer_t& timer, ptmf_t ptmf)
		{
			timer.cancel();
			timer.expires_from_now(boost::posix_time::seconds{ 60 });		// timeout 60s, for debug
			timer.async_wait(boost::bind(ptmf, shared_from_this(), task, boost::asio::placeholders::error));
		}

		void do_call(task_ptr task)
		{
			assert(client_status::disable != status_);
			if (client_status::busy == status_)
			{
				tasks_.push_back(task);
			}
			else
			{
				status_ = client_status::busy;
				start_send_recv(task);
			}
		}

		void start_send_recv(task_ptr task)
		{
			if (task->is_finished())
			{
				// task being aborted
				call_next_if_there_exist();
			}
			else
			{
				send(task);
				receive(task);
				setup_timeout(task, task->timer, &async_client::handle_timeout);
			}
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
			socket_.close();
			
			for (auto task : tasks_)
			{
				task->timer.cancel();
				task->abort();
			}

			tasks_.clear();
		}

		void call_next_if_there_exist()
		{
			assert(client_status::busy == status_);
			if (tasks_.empty())
			{
				status_ = client_status::ready;
			}
			else
			{
				auto next_task = tasks_.front();
				tasks_.pop_front();

				start_send_recv(next_task);
			}
		}

	private:
		io_service_t&				ios_;
		tcp::socket					socket_;
		tcp::resolver				resolver_;
		deadline_timer_t			timer_;
		std::string					address_;
		std::string					port_;
		client_status				status_;
		bool						connecting_;
		std::list<task_ptr>			tasks_;
	};

} }