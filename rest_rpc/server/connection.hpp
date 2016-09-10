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
		using deadline_timer_t = boost::asio::deadline_timer;

		template <size_t Size>
		using sarray = std::array<char, Size>;

	public:
		inline connection(server_ptr server, boost::asio::io_service& io_service, std::size_t timeout_milli);
		
		inline void start();
		inline tcp::socket& socket();
		inline void close();
	private:
		friend class server<Decode>;
		inline void read_head();
		inline void read_body();
		inline void reset_timer();
		inline void cancel_timer();		
		inline void set_no_delay();
		inline void response(const char* data, size_t size, result_code code = result_code::OK);
		inline void response(std::string const& topic, char const* data, size_t size, result_code code = result_code::OK);
		inline auto get_message(char const* data, size_t size, result_code code)->std::vector<boost::asio::const_buffer>;
		inline auto get_message(std::string const& topic, const char* data, size_t size, result_code code)->std::vector<boost::asio::const_buffer>;
		inline void write(std::vector<boost::asio::const_buffer> const& message);

		server_ptr				server_;
		tcp::socket				socket_;
		head_t					head_;				
		std::vector<char>		data_;
		deadline_timer_t		timer_;
		std::size_t				timeout_milli_;
	};
} }