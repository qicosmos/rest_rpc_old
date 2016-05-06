#pragma once
#include "connection.hpp"
#include "io_service_pool.hpp"

using boost::asio::ip::tcp;

class server : private boost::noncopyable
{
public:

	server(short port, size_t size) : io_service_pool_(size),
		acceptor_(io_service_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
	{
		do_accept();
	}

	~server()
	{
		io_service_pool_.stop();
		thd_->join();
	}

	void run()
	{
		thd_ = std::make_shared<std::thread>([this] {io_service_pool_.run(); });
	}

private:
	void do_accept()
	{
		conn_.reset(new connection(io_service_pool_.get_io_service()));
		acceptor_.async_accept(conn_->socket(), [this](boost::system::error_code ec)
		{
			if (!ec)
			{
				conn_->start();
			}

			do_accept();
		});
	}

	io_service_pool io_service_pool_;
	tcp::acceptor acceptor_;
	std::shared_ptr<connection> conn_;
	std::shared_ptr<std::thread> thd_;
};

