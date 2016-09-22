#pragma once

namespace timax { namespace rpc 
{
	//std::string g_str = "HTTP/1.0 200 OK\r\n"
	//	"Content-Length: 4\r\n"
	//	"Content-Type: text/html\r\n"
	//	"Connection: Keep-Alive\r\n\r\n"
	//	"TEST";

	template <typename Decode>
	struct response_context;

	template <typename Decode>
	class ios_wrapper;

	template <typename Decode>
	class connection : public std::enable_shared_from_this<connection<Decode>>, private boost::noncopyable
	{
		using server_ptr = std::shared_ptr<server<Decode>>;
		using ios_wrapper_t = ios_wrapper<Decode>;
		using deadline_timer_t = boost::asio::deadline_timer;
		using context_t = response_context<Decode>;
		using context_ptr = std::shared_ptr<context_t>;

		template <size_t Size>
		using sarray = std::array<char, Size>;

	public:
		inline connection(server_ptr server, ios_wrapper_t& wrapper, std::size_t timeout_milli);
		
		inline void start();
		inline tcp::socket& socket();
		inline void close();
		void response(context_ptr& ctx);
	private:
		friend class server<Decode>;
		inline void read_head();
		inline void read_body();
		inline void reset_timer();
		inline void cancel_timer();
		inline void set_no_delay();

		server_ptr				server_;
		ios_wrapper_t&			ios_wrapper_;
		tcp::socket				socket_;
		head_t					head_;				
		std::vector<char>		data_;
		deadline_timer_t		timer_;
		std::size_t				timeout_milli_;
	};
} }