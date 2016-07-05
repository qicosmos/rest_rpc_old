#pragma once

#include <string>
#include <list>
#include <kapok/Kapok.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "../consts.h"
#include "../common.h"
#include "../function_traits.hpp"
#include "../protocol.hpp"

namespace timax
{
	class async_client : public boost::enable_shared_from_this<async_client>
	{
		enum class client_status
		{
			disable			= 0,		// client is not available
			connecting		= 1,		// client is connecting to server
			ready			= 2,		// there is not any task to do, and client is connected
			busy			= 3,		// tasks_ is no empty
		};

		enum class task_status
		{
			calling,
			called,
		};

		using io_service_t = boost::asio::io_service;
		using tcp = boost::asio::ip::tcp;
		using deadline_timer_t = boost::asio::deadline_timer;

		struct task_t
		{
			task_t(io_service_t& ios, std::string req)
				: timer(ios)
				, status(task_status::calling)
				, req(req)
			{
			}

			deadline_timer_t			timer;
			task_status					status;
			head_t						head;
			std::string					req;
			std::string					rep;
			std::condition_variable		cond;
			std::mutex					mutex;
		};

		using task_ptr = boost::shared_ptr<task_t>;

		template <typename Proto, typename ... Args>
		task_ptr make_task(io_service_t& ios, Proto const& proto, Args&& ... args)
		{
			return boost::make_shared(ios, std::move(proto.make_json(std::forward<Args>(args)...)));
		}

		template <typename Proto>
		class tast_wrapper
		{
			using result_type = typename Proto::result_type;
			using function_t = std::function<void(result_type)>;
			using aclient_ptr = boost::shared_ptr<async_client>;

		public:
			tast_wrapper(task_ptr task, aclient_ptr client)
				: task_(task)
				, client_(client)
			{
			}

			result_type get() const
			{
				std::unique_lock<std::mutex> lock{ task_->mutex };
				task_->cond.wait(lock);

				// TO DO .....
				// throw exceptions
				return task_->proto.parse_json(task_->rep);
			}

			void cancle()
			{
				client_->cancle(task_);
			}

			tast_wrapper(tast_wrapper const&) = delete;
			tast_wrapper& operator=(tast_wrapper const&) = delete;

		private:
			task_ptr					task_;
			aclient_ptr					client_;
		};

	public:
		async_client(io_service_t& ios, std::string address, std::string port)
			: ios_(ios)
			, socket_(ios)
			, resolver_(ios)
			, timer_(ios)
			, address_(std::move(address))
			, port_(std::move(port))
			, connecting_(false)
			, status_(client_status::disable)
		{

		}

		template <typename Proto, typename ... Args>
		auto call(Proto const& proto, Args&& ... args)
			-> tast_wrapper<Proto>
		{
			auto task = make_task(ios_, proto, std::forward<Args>(args)... );

			task->head = 
			{
				static_cast<int16_t>(data_type::JSON),
				static_cast<int16_t>(task->proto.get_type()),
				static_cast<int32_t>(task->req.length)
			};

			if (!socket_.is_open())
			{
				tcp::resolver::query q = { tcp::v4(), address_, port_ };
				resolver_.async_resolve(q, boost::bind(&async_client::handle_resolve, shared_from_this(), 
					task, boost::asio::placeholders::error, boost::asio::placeholders::iterator));

				{
					std::unique_lock<std::mutex> lock{ mutex_ };
					status_ = client_status::connecting;
				}
				setup_timeout(task, timer_);
			}
			else
			{
				do_call(task);
			}

			return{ task, shared_from_this() };
		}

		void cancle(task_ptr task)
		{
			{
				std::unique_lock<std::mutex> lock{ mutex_ };
				auto itr = std::find(tasks_.begin(), tasks_.end(), task);
				if (itr != tasks_.end())
				{
					tasks_.erase(itr);
				}
			}

			{
				std::unique_lock<std::mutex> lock{ task->mutex };
				task->timer.cancel();
				if (task_status::calling == task->status)
				{
					lock.unlock();
					socket_.cancel();
				}
			}
		}

	private:
		void handle_resolve(task_ptr task, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			check_system_error(task, error);

			// log try to connect
			boost::asio::async_connect(socket_, endpoint_iterator, boost::bind(&async_client::handle_connect, shared_from_this(),
				task, boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void setup_timeout(task_ptr task, deadline_timer_t& timer)
		{
			timer.cancel();
			timer.expires_from_now(boost::posix_time::milliseconds{ 1000 });
			timer.async_wait(boost::bind(&async_client::handle_timeout,
				shared_from_this(), task, boost::asio::placeholders::error));
		}

		void handle_timeout(task_ptr task, boost::system::error_code const& error)
		{	
			check_system_error(task, error);

			auto errc = boost::system::errc::make_error_code(boost::system::errc::timed_out);

			client_status status;
			{
				std::unique_lock<std::mutex> lock{ mutex_ };
				status = status_;
			}

			if (client_status::connecting == status)
			{
				destroy(errc);
			}
			else
			{
				cancle(task);
			}
			task->cond.notify_one();
		}

		void handle_connect(task_ptr task, boost::system::error_code const& error, tcp::resolver::iterator endpoint_iterator)
		{
			check_system_error(task, error);

			connecting_ = false;
			do_call(task);
		}

		void do_call(task_ptr task)
		{
			std::unique_lock<std::mutex> lock{ mutex_ };
			assert(client_status::connecting != status_ &&
				client_status::disable != status_);
			if (client_status::busy == status_)
			{
				tasks_.push_back(task);
			}
			else
			{
				status_ = client_status::busy;
				lock.unlock();
				start_send_recv(task);
			}
		}

		void start_send_recv(task_ptr task)
		{
			send(task);
			receive(task);
		}

		void send(task_ptr task)
		{
			std::vector<boost::asio::const_buffer> message;
			message.push_back(boost::asio::buffer(&task->head, sizeof(head_t)));
			message.push_back(boost::asio::buffer(task->req));

			boost::asio::async_write(socket_, message, boost::bind(&async_client::handle_write, 
				shared_from_this(), task, boost::asio::placeholders::error));
		}

		void handle_write(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(task, error);
		}

		void receive(task_ptr task)
		{
			boost::asio::async_read(socket_, boost::asio::buffer(&task->head, sizeof(head_t)),
				boost::bind(&async_client::handle_read_head, shared_from_this(),
					task, boost::asio::placeholders::error));
		}

		void handle_read_head(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(task, error);

			task->rep.resize(task->head.len);
			boost::asio::async_read(socket_, boost::asio::buffer(task->rep),
				boost::bind(&async_client::handle_read_body, shared_from_this(),
					task, boost::asio::placeholders::error));
		}

		void handle_read_body(task_ptr task, boost::system::error_code const& error)
		{
			check_system_error(task, error);
			task->cond.notify_one();

			std::unique_lock<std::mutex> lock{ mutex_ };
			assert(client_status::busy == status_);
			if (tasks_.empty())
			{
				status_ = client_status::ready;
			}
			else
			{
				auto task = tasks_.front();
				tasks_.pop_front();
				lock.unlock();
				start_send_recv(task);
			}
		}

		void check_system_error(task_ptr task, boost::system::error_code const& error)
		{
			if (error && boost::system::errc::operation_canceled != error)
			{
				// TO DO : log error
				destroy(error);
				throw boost::system::system_error{ error };
			}
		}

		void cancle_all(boost::system::error_code const& reason)
		{
			boost::system::error_code error;
			socket_.cancel(error);
			// log if error


		}

		void destroy(boost::system::error_code const& reason)
		{
			// logout destroy
			boost::system::error_code error;
			resolver_.cancel();

			socket_.cancel(error);
			// log if error
			socket_.shutdown(tcp::socket::shutdown_both, error);
			// log if error
			socket_.close(error);
			// log if error
		}

	private:
		io_service_t&			ios_;
		tcp::socket				socket_;
		tcp::resolver			resolver_;
		deadline_timer_t		timer_;
		std::string				address_;
		std::string				port_;
		bool					connecting_;
		client_status			status_;

		std::list<task_ptr>		tasks_;				// tasks to do
		std::mutex				mutex_;
	};
}
