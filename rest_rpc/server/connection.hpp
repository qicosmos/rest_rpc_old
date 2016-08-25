#pragma once

namespace timax { namespace rpc 
{
	std::string g_str = "HTTP/1.0 200 OK\r\n"
		"Content-Length: 4\r\n"
		"Content-Type: text/html\r\n"
		"Connection: Keep-Alive\r\n\r\n"
		"TEST";

	class connection : public std::enable_shared_from_this<connection>, private boost::noncopyable
	{
		friend class router;
		friend class server;

		using server_ptr = std::shared_ptr<server>;
		using message_t  = std::array<boost::asio::mutable_buffer, 2>;
		using deadline_timer_t = boost::asio::deadline_timer;

		template <size_t Size>
		using sarray = std::array<char, Size>;

	public:
		connection(server_ptr server, boost::asio::io_service& io_service, std::size_t timeout_milli);
		void start();
		tcp::socket& socket();
		//add timeout later
		void response(const char* data, size_t size, result_code code = result_code::OK);

	private:
		void read_head();
		void read_body(head_t const& head);
		void reset_timer();
		void cancel_timer();
		void close();
		void set_no_delay();

		server_ptr				server_;
		tcp::socket				socket_;
		sarray<HEAD_LEN>		head_;
		sarray<MAX_BUF_LEN>		data_;
		sarray<106>				read_buf_;
		message_t				message_;
		deadline_timer_t		timer_;
		std::size_t				timeout_milli_;
	};
} }