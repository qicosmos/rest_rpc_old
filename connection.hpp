#pragma once
#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include "common.h"
#include "router.hpp"

using boost::asio::ip::tcp;

class connection : public std::enable_shared_from_this<connection>, private boost::noncopyable
{
public:
	connection(boost::asio::io_service& io_service, std::size_t timeout_milli) : socket_(io_service), message_{ boost::asio::buffer(head_),
		boost::asio::buffer(data_) }, timer_(io_service), timeout_milli_(timeout_milli)
	{
	}

	void start()
	{
		read_head();
	}

	tcp::socket& socket()
	{
		return socket_;
	}

	void read_head()
	{
		reset_timer();
		auto self(this->shared_from_this());
		boost::asio::async_read(socket_, boost::asio::buffer(head_), [this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!socket_.is_open())
			{
				cancel_timer();
				return;
			}

			if (!ec)
			{
				const int body_len = *(int*)head_;
				if (body_len > 0 && body_len< MAX_BUF_LEN)
				{
					read_body(body_len);
					return;
				}

				if (body_len == 0) //nobody, just head.
				{
					read_head();
				}
				else
				{
					SPD_LOG_ERROR("invalid body len {}", body_len);
					cancel_timer();
				}
			}
			else
			{
				SPD_LOG_INFO(ec.message().c_str());
				cancel_timer();
			}
		});
	}

	void read_body(std::size_t size)
	{
		auto self(this->shared_from_this());
		boost::asio::async_read(socket_, boost::asio::buffer(data_, size), [this, self](boost::system::error_code ec, std::size_t length)
		{
			cancel_timer();

			if (!socket_.is_open())
				return;

			if (!ec)
			{
				router& _router = router::get();
				_router.route(data_, length, self);
			}
			else
			{
				SPD_LOG_INFO(ec.message().c_str());
			}
		});
	}

	//add timeout later
	void response(const char* json_str)
	{
		auto self(this->shared_from_this());
		int len = strlen(json_str);
		message_[0] = boost::asio::buffer(&len, HEAD_LEN);
		message_[1] = boost::asio::buffer((char*)json_str, len);
		boost::asio::async_write(socket_, message_, [this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				read_head();
			}
			else
			{
				SPD_LOG_INFO(ec.message().c_str());
			}
		});
	}

	void reset_timer()
	{
		if (timeout_milli_ == 0)
			return;

		auto self(this->shared_from_this());
		timer_.expires_from_now(boost::posix_time::milliseconds(timeout_milli_));
		timer_.async_wait([this, self](const boost::system::error_code& ec)
		{
			if (!socket_.is_open())
			{
				return;
			}

			if (ec)
			{
				SPD_LOG_INFO(ec.message().c_str());
				return;
			}

			SPD_LOG_INFO("connection timeout");

			close();
		});
	}

	void cancel_timer()
	{
		if (timeout_milli_ == 0)
			return;

		timer_.cancel();
	}

	void close()
	{
		boost::system::error_code ignored_ec;
		socket_.close(ignored_ec);
	}

	tcp::socket socket_;
	char head_[HEAD_LEN];
	char data_[MAX_BUF_LEN];
	std::array<boost::asio::mutable_buffer, 2> message_;
	boost::asio::deadline_timer timer_;
	std::size_t timeout_milli_;
};

