#pragma once

#include "../../forward.hpp"

namespace timax{ namespace rpc { namespace detail 
{
	class async_connection
	{
	public:
		async_connection(
			io_service_t& ios, 
			std::string const& address,
			std::string const& port,
			std::function<void()> on_success)
			: socket_(ios)
			, resolver_(ios)
			, query_(tcp::v4(), address, port)
		{
			connect(std::move(on_success));
		}

		tcp::socket& socket()
		{
			return socket_;
		}

	private:
		void connect(std::function<void()> on_success)
		{
			start_connect_from_resolve(std::move(on_success));
		}

		void start_connect_from_resolve(std::function<void()> on_success)
		{
			resolver_.async_resolve(query_, boost::bind(&async_connection::handle_resolve, this,
				std::move(on_success), boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void start_connect(
			std::function<void()> on_success,
			tcp::resolver::iterator endpoint_iterator)
		{
			async_connect(socket_, endpoint_iterator, boost::bind(&async_connection::handle_connection, this,
				std::move(on_success), boost::asio::placeholders::error, boost::asio::placeholders::iterator));
		}

		void handle_resolve(
			std::function<void()> on_success, 
			boost::system::error_code const& error, 
			tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				start_connect(std::move(on_success), endpoint_iterator);
			}
			//else
			//{
			//	start_connect_from_resolve(std::move(on_success));
			//}
		}

		void handle_connection(
			std::function<void()> on_success,
			boost::system::error_code const& error,
			tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				on_success();
			}
			else
			{
				// reconnect
				start_connect(std::move(on_success), endpoint_iterator);
			}
		}

	private:
		tcp::socket				socket_;
		tcp::resolver			resolver_;
		tcp::resolver::query	query_;
	};
} } }