#pragma once
#include <iostream>
#include <memory>
#include <boost/asio.hpp>

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
				read_body(*(int*)head_);
			}
			else
			{
				//log
				return;
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
				router::get().route(data_, length);
				read_head();
			}
			else
			{
				//log
				return;
			}
		});
	}

	void do_write()
	{
		auto self(this->shared_from_this());
		boost::asio::async_write(socket_, boost::asio::buffer(data_, max_length),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				//do_read();
			}
			else
			{
				//log
				return;
			}
		});
	}

	tcp::socket socket_;
	enum { max_length = 35 };
	char data_[max_length];
	char head_[4];
	std::array<boost::asio::mutable_buffer, 2> message_;
};

