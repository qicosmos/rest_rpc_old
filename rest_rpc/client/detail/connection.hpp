#pragma once

#include "../../forward.hpp"

namespace timax{ namespace rpc 
{
	class async_connection
	{
	public:
		async_connection(
			io_service_t& ios, 
			std::string const& address,
			std::string const& port)
			: socket_(ios)
			, resolver_(ios)
			, query_(tcp::v4(), address, port)
		{
		}

		void start(std::function<void()>&& on_success)
		{
			on_success_ = std::move(on_success);
			connect();
		}

		tcp::socket& socket()
		{
			return socket_;
		}

	private:
		void connect()
		{
			start_connect_from_resolve();
		}

		void start_connect_from_resolve()
		{
			resolver_.async_resolve(query_, boost::bind(&async_connection::handle_resolve, this,
				boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void start_connect(tcp::resolver::iterator endpoint_iterator)
		{
			async_connect(socket_, endpoint_iterator, boost::bind(&async_connection::handle_connection, this,
				boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void handle_resolve(
			boost::system::error_code const& error, 
			tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				start_connect(endpoint_iterator);
			}
			//else
			//{
			//	start_connect_from_resolve(std::move(on_success));
			//}
		}

		void handle_connection(
			boost::system::error_code const& error,
			tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				if (on_success_)
					on_success_();
			}
			else
			{
				// reconnect
				start_connect(endpoint_iterator);
			}
		}

	private:
		tcp::socket				socket_;
		tcp::resolver			resolver_;
		tcp::resolver::query	query_;
		std::function<void()>	on_success_;
	};
} }