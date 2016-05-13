#pragma once
#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include "common.h"

using boost::asio::ip::tcp;

class connection : public std::enable_shared_from_this<connection>, private boost::noncopyable
{
public:
	connection(boost::asio::io_service& io_service) : socket_(io_service), message_{ boost::asio::buffer(head_), boost::asio::buffer(data_) }
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

private:
	void read_head()
	{
		auto self(this->shared_from_this());
		boost::asio::async_read(socket_, boost::asio::buffer(head_), [this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				const int body_len = *(int*)head_;
				if (body_len > MAX_BUF_LEN)
				{
					//log outof range

				}
				else
				{
					read_body(body_len);
				}
			}
			else
			{
				//log
			}
		});
	}

	void read_body(std::size_t size)
	{
		auto self(this->shared_from_this());
		boost::asio::async_read(socket_, boost::asio::buffer(data_, size), [this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				router& _router = router::get();
				bool is_send_ok = false;
				_router.route(data_, length, [this,&is_send_ok](const char* json) { is_send_ok = response(json); });
				if(is_send_ok)
					read_head();
			}
			else
			{
				//log
				
			}
		});
	}

	//will be improved to async send in future.
	bool response(const char* json_str)
	{
		int len = strlen(json_str);

		message_[0] = boost::asio::buffer(&len, 4);
		message_[1] = boost::asio::buffer((char*)json_str, len);
		boost::system::error_code ec;
		boost::asio::write(socket_, message_, ec);

		if (!ec)
		{
			//log
			g_succeed_count++;
			return true;
		}
		else
		{
			return false;
		}
	}

	tcp::socket socket_;
	char head_[4];
	char data_[MAX_BUF_LEN];
	std::array<boost::asio::mutable_buffer, 2> message_;
	bool is_send_ok_ = false;
};

