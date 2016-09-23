#pragma once

namespace timax { namespace rpc 
{
	class ios_wrapper;

	class connection : public std::enable_shared_from_this<connection>
	{
	public:
		template <typename CodecPolicy> friend class server;
		template <typename CodecPolicy> friend class router;
		friend class ios_wrapper;

		using connection_ptr = std::shared_ptr<connection>;
		using context_ptr = std::shared_ptr<context_t>;
		using connection_on_error_t = std::function<void(connection_ptr, boost::system::error_code const& error)>;
		using connection_on_read_t = std::function<void(connection_ptr)>;
		using connection_on_read_pages_t = std::function<void(connection_ptr, std::vector<char>)>;

	public:
		connection(ios_wrapper& ios, duration_t time_out);
		void response(context_ptr& ctx);
		void close();

	protected:
		tcp::socket& socket();
		void start();
		void on_error(boost::system::error_code const& error);
		static void set_on_error(connection_on_error_t on_error);
		static void set_on_read(connection_on_read_t on_read);
		static void set_on_read_pages(connection_on_read_pages_t on_read_pages);
		blob_t get_read_buffer() const;

	private:
		void set_no_delay();
		void expires_timer();
		void cancel_timer();
		void read_head();
		void read_body();

	private:
		void handle_read_head(boost::system::error_code const& error);
		void handle_read_body(boost::system::error_code const& error);		
		void handle_read_body_pages(std::vector<char> read_buffer, boost::system::error_code const& error);

	private:
		ios_wrapper&						ios_wrapper_;
		tcp::socket							socket_;
		head_t								head_;
		std::array<char, PAGE_SIZE>			usual_read_buffer_;
		steady_timer_t						timer_;
		duration_t							time_out_;

		static connection_on_error_t		on_error_;
		static connection_on_read_t			on_read_;
		static connection_on_read_pages_t	on_read_pages_;
	};

	connection::connection_on_error_t connection::on_error_;
	connection::connection_on_read_t connection::on_read_;
	connection::connection_on_read_pages_t connection::on_read_pages_;
} }