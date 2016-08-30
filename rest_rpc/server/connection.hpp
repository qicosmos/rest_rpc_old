#pragma once

namespace timax { namespace rpc 
{
	std::string g_str = "HTTP/1.0 200 OK\r\n"
		"Content-Length: 4\r\n"
		"Content-Type: text/html\r\n"
		"Connection: Keep-Alive\r\n\r\n"
		"TEST";

	template <typename Decode>
	class connection : public std::enable_shared_from_this<connection<Decode>>, private boost::noncopyable
	{
		using server_ptr = std::shared_ptr<server<Decode>>;
		using message_t  = std::array<boost::asio::mutable_buffer, 2>;
		using deadline_timer_t = boost::asio::deadline_timer;

		template <size_t Size>
		using sarray = std::array<char, Size>;

	public:
		connection(server_ptr server, boost::asio::io_service& io_service, std::size_t timeout_milli);
		
		void start();
		tcp::socket& socket();

	private:
		friend class server<Decode>;
		void read_head();
		void read_body(head_t const& head);
		void reset_timer();
		void cancel_timer();
		void close();
		void set_no_delay();
		void response(const char* data, size_t size, result_code code = result_code::OK);

		//just for ab test.
		void do_read()
		{
			auto self(this->shared_from_this());
			boost::asio::async_read(socket_, boost::asio::buffer(read_buf_), [this, self](boost::system::error_code ec, std::size_t length)
			{
				if (ec)
				{
					close();
					return;
				}

				boost::asio::async_write(socket_, boost::asio::buffer(g_str), [this, self](boost::system::error_code ec, std::size_t length) {
					if (ec)
					{
						close();
						return;
					}

					do_read();
				});
				//boost::system::error_code ec1;
				//boost::asio::write(socket_, boost::asio::buffer(g_str), ec1);
				//do_read();
			});
		}

		server_ptr				server_;
		tcp::socket				socket_;
		sarray<HEAD_LEN>		head_;
		std::vector<char>		data_;
		sarray<106>				read_buf_; //for ab test
		message_t				message_;
		deadline_timer_t		timer_;
		std::size_t				timeout_milli_;
	};
} }