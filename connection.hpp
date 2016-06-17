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
				head_t h = *(head_t*)(head_);
				//const int body_len = i & int{-1};
				//const int type = i << 32;
				if (h.len > 0 && h.len< MAX_BUF_LEN)
				{
					read_body(h);
					return;
				}

				if (h.len == 0) //nobody, just head.
				{
					read_head();
				}
				else
				{
					SPD_LOG_ERROR("invalid body len {}", h.len);
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

	void read_body(head_t const& head)
	{
		auto self(this->shared_from_this());
		boost::asio::async_read(socket_, boost::asio::buffer(data_, head.len), [this, head, self](boost::system::error_code ec, std::size_t length)
		{
			cancel_timer();

			if (!socket_.is_open())
				return;

			if (!ec)
			{
				router& _router = router::get();
				//if type is tag, need callback to client the tag
				bool round_trip = (head.framework_type == static_cast<int>(framework_type::ROUNDTRIP));
				
				//if tag is binary, route_binary
				bool binary_type = (head.data_type == static_cast<int>(data_type::BINARY));
				if (!binary_type)
				{
					_router.route(data_, length, self, round_trip);
				}
				else
				{
					_router.route_binary(data_, length, self, round_trip);
				}				
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
		head_t h = { 0, 0, static_cast<int32_t>(strlen(json_str)) };
		message_[0] = boost::asio::buffer(&h, HEAD_LEN);
		message_[1] = boost::asio::buffer((char*)json_str, strlen(json_str));

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

