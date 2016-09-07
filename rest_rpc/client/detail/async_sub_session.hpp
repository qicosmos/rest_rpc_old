#pragma once

namespace timax { namespace rpc 
{
	class sub_session : public std::enable_shared_from_this<sub_session>
	{
		using function_t = std::function<void(char const*, size_t)>;

	public:
		template <typename Cont>
		sub_session(
			io_service_t& ios,
			Cont&& cont,
			std::string const& address,
			std::string const& port,
			function_t&& func)
			: hb_timer_(ios)
			, request_(cont.begin(), cont.end())
			, connection_(ios, address, port)
			, function_(std::move(func))
			, running_flag_(false)
		{
		}

		void start()
		{
			running_flag_.store(true);
			connection_.start(std::bind(&sub_session::request_sub, this->shared_from_this()));
		}

		void stop()
		{
			running_flag_.store(false);
		}

	private:
		auto get_send_message(std::string const& call) -> std::array<boost::asio::const_buffer, 3>
		{
			head_ =
			{
				0, 0, 0,
				static_cast<uint32_t>(call.size() + request_.size() + 1)
			};

			return
			{
				boost::asio::buffer(&head_, sizeof(head_t)),
				boost::asio::buffer(call.c_str(), call.length() + 1),
				boost::asio::buffer(request_)
			};
		}

		void request_sub()
		{
			if (running_flag_.load())
			{
				auto message = get_send_message(sub_topic.name());
				async_write(connection_.socket(), message, boost::bind(
					&sub_session::handle_request_sub, this->shared_from_this(), boost::asio::placeholders::error));
			}
		}

		void confirm_sub()
		{
			auto message = get_send_message(sub_confirm.name());
			async_write(connection_.socket(), message, boost::bind(
				&sub_session::handle_confirm_sub, this->shared_from_this(), boost::asio::placeholders::error));
		}

		void begin_sub_procedure()
		{
			setup_heartbeat_timer();			// setup heart beat
			recv_sub_head();
		}

		void setup_heartbeat_timer()
		{
			hb_timer_.expires_from_now(boost::posix_time::seconds{ 15 });
			hb_timer_.async_wait(boost::bind(&sub_session::handle_heartbeat, this->shared_from_this(), boost::asio::placeholders::error));
		}

		void recv_sub_head()
		{
			async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)), boost::bind(
				&sub_session::handle_sub_head, this->shared_from_this(), boost::asio::placeholders::error));
		}

	private:
		void handle_request_sub(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load())
			{
				async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)), boost::bind(
					&sub_session::handle_response_sub_head, this->shared_from_this(), boost::asio::placeholders::error));
			}
		}

		void handle_response_sub_head(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load())
			{
				if (head_.len > 0)
				{
					response_.resize(head_.len);
					async_read(connection_.socket(), boost::asio::buffer(response_), boost::bind(
						&sub_session::handle_response_sub_body, this->shared_from_this(), boost::asio::placeholders::error));
				}
			}
		}

		void handle_response_sub_body(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load()) 
			{
				if(result_code::OK == static_cast<result_code>(head_.code))
					confirm_sub();
			}
		}

		void handle_confirm_sub(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load())
			{	
				begin_sub_procedure();
			}
		}

		void handle_sub_head(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load() && 0 == head_.code)
			{
				if (head_.len > 0)
				{
					// in this case, we got sub message
					response_.resize(head_.len);
					async_read(connection_.socket(), boost::asio::buffer(response_), boost::bind(
						&sub_session::handle_sub_body, this->shared_from_this(), boost::asio::placeholders::error));
				}
				else
				{
					// in this case we got heart beat back
					recv_sub_head();
				}
			}
		}

		void handle_sub_body(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load())
			{
				if (function_)
					function_(response_.data(), response_.size());
				
				recv_sub_head();
			}
		}

		void handle_heartbeat(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load())
			{
				head_ = { 0 };
				async_write(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)), 
					boost::bind(&sub_session::handle_send_hb, this->shared_from_this(), boost::asio::placeholders::error));

				setup_heartbeat_timer();
			}
		}

		void handle_send_hb(boost::system::error_code const& error)
		{

		}

	private:
		deadline_timer_t					hb_timer_;
		async_connection					connection_;

		head_t								head_;
		std::vector<char> const				request_;
		std::vector<char>					response_;
		function_t							function_;
		std::atomic<bool>					running_flag_;
	};

} }