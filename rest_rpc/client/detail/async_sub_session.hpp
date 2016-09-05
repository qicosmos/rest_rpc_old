#pragma once

namespace timax { namespace rpc 
{
	class sub_session
	{
	public:
		template <typename Cont>
		sub_session(
			io_service_t& ios,
			Cont&& cont,
			std::string const& address,
			std::string const& port)
			: ios_(ios)
			, request_(cont.begin(), cont.end())
			, connection_(ios_, address, port, [this] { request_sub(); })
		{
		}

	private:
		auto get_send_message(std::string const& call) -> std::array<boost::asio::const_buffer, 3>
		{
			head_ =
			{
				0, 0, 0,
				static_cast<uint32_t>(sizeof(head_t) + call.size() + request_.size() + 1)
			};

			return
			{
				boost::asio::buffer(&head_, sizeof(head_t)),
				boost::asio::buffer(call),
				boost::asio::buffer(request_)
			};
		}

		void request_sub()
		{
			auto message = get_send_message(sub_topic.name());
			async_write(connection_.socket(), message, boost::bind(
				&sub_session::handle_request_sub, this, boost::asio::placeholders::error));
		}

		void confirm_sub()
		{
			auto message = get_send_message(sub_confirm.name());
			async_write(connection_.socket(), message, boost::bind(
				&sub_session::handle_confirm_sub, this, boost::asio::placeholders::error));
		}

	private:
		void handle_request_sub(boost::system::error_code const& error)
		{
			if (!error)
			{
				async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)), boost::bind(
					&sub_session::handle_response_sub_head, this, boost::asio::placeholders::error));
			}
		}

		void handle_response_sub_head(boost::system::error_code const& error)
		{
			if (!error && head_.len > 0)
			{
				response_.resize(head_.len);
				async_read(connection_.socket(), boost::asio::buffer(response_), boost::bind(
					&sub_session::handle_response_sub_body, this, boost::asio::placeholders::error));
			}
		}

		void handle_response_sub_body(boost::system::error_code const& error)
		{
			if (!error)
			{
				confirm_sub();
			}
		}

		void handle_confirm_sub(boost::system::error_code const& error)
		{
			if (!error)
			{
				async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)), boost::bind(
					&sub_session::handle_sub_head, this, boost::asio::placeholders::error));
			}
		}

		void handle_sub_head(boost::system::error_code const& error)
		{
			if (!error)
			{
				response_.resize(head_.len);
				async_read(connection_.socket(), boost::asio::buffer(response_), boost::bind(
					&sub_session::handle_sub_body, this, boost::asio::placeholders::error));
			}
		}

		void handle_sub_body(boost::system::error_code const& error)
		{
			if (!error)
			{
				async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)), boost::bind(
					&sub_session::handle_sub_head, this, boost::asio::placeholders::error));
			}
		}

	private:
		io_service_t&						ios_;
		head_t								head_;
		std::vector<char> const				request_;
		std::vector<char>					response_;
		async_connection					connection_;
	};

} }