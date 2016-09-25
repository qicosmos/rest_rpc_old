#pragma once

namespace timax { namespace rpc 
{
	connection::connection(ios_wrapper& ios, duration_t time_out)
		: ios_wrapper_(ios)
		, socket_(ios_wrapper_.get_ios())
		, timer_(ios_wrapper_.get_ios())
		, time_out_(time_out)
	{
	}

	void connection::response(context_ptr& ctx)
	{
		auto self = this->shared_from_this();
		ios_wrapper_.write(self, ctx);
	}

	void connection::close()
	{
		boost::system::error_code ignored_ec;
		socket_.close(ignored_ec);
	}

	tcp::socket& connection::socket()
	{
		return socket_;
	}

	void connection::start()
	{
		set_no_delay();
		read_head();
	}

	void connection::on_error(boost::system::error_code const& error)
	{
		SPD_LOG_DEBUG(error.message().c_str());

		close();

		decltype(auto) on_error = get_on_error();

		if (on_error)
			on_error(this->shared_from_this(), error);
	}

	void connection::set_on_error(connection_on_error_t on_error)
	{
		get_on_error() = std::move(on_error);
	}

	void connection::set_on_read(connection_on_read_t on_read)
	{
		get_on_read() = std::move(on_read);
	}

	void connection::set_on_read_pages(connection_on_read_pages_t on_read_pages)
	{
		get_on_read_page() = std::move(on_read_pages);
	}

	connection::connection_on_error_t& connection::get_on_error()
	{
		static connection_on_error_t on_error;
		return on_error;
	}

	connection::connection_on_read_t& connection::get_on_read()
	{
		static connection_on_read_t on_read;
		return on_read;
	}

	connection::connection_on_read_pages_t& connection::get_on_read_page()
	{
		static connection_on_read_pages_t on_read_page;
		return on_read_page;
	}

	blob_t connection::get_read_buffer() const
	{
		return{ usual_read_buffer_.data(), head_.len };
	}

	void connection::set_no_delay()
	{
		boost::asio::ip::tcp::no_delay option(true);
		boost::system::error_code ec;
		socket_.set_option(option, ec);
	}

	void connection::expires_timer()
	{
		if (time_out_.count() == 0)
			return;

		timer_.expires_from_now(time_out_);
		// timer_.async_wait
	}

	void connection::cancel_timer()
	{
		if (time_out_.count() == 0)
			return;

		timer_.cancel();
	}

	void connection::read_head()
	{
		expires_timer();
		async_read(socket_, boost::asio::buffer(&head_, sizeof(head_t)),
			boost::bind(&connection::handle_read_head, this->shared_from_this(), asio_error));
	}

	void connection::read_body()
	{
		if (head_.len <= PAGE_SIZE)
		{
			async_read(socket_, boost::asio::buffer(usual_read_buffer_.data(), head_.len),
				boost::bind(&connection::handle_read_body, this->shared_from_this(), asio_error));
		}
		else if(head_.len <= MAX_BUF_LEN)
		{
			std::vector<char> read_buffer(head_.len, 0);
			async_read(socket_, boost::asio::buffer(read_buffer.data(), read_buffer.size()), boost::bind(
				&connection::handle_read_body_pages, this->shared_from_this(), std::move(read_buffer), asio_error));
		}
		else
		{
			socket_.close();
			//on_error();
		}
	}

	void connection::handle_read_head(boost::system::error_code const& error)
	{
		if (!socket_.is_open())
			return;

		if (!error)
		{
			if (head_.len == 0)
			{
				read_head();
			}
			else
			{
				read_body();
			}
		}
		else
		{
			cancel_timer();
			on_error(error);
		}
	}

	void connection::handle_read_body(boost::system::error_code const& error)
	{
		cancel_timer();
		if (!socket_.is_open())
			return;

		if (!error)
		{
			decltype(auto) on_read = get_on_read();
			if (on_read)
				on_read(this->shared_from_this());

			read_head();
		}
		else
		{
			cancel_timer();
			on_error(error);
		}
	}

	void connection::handle_read_body_pages(std::vector<char> read_buffer, boost::system::error_code const& error)
	{
		cancel_timer();
		if (!socket_.is_open())
			return;

		if (!error)
		{
			decltype(auto) on_read_pages = get_on_read_page();

			if (on_read_pages)
				on_read_pages(this->shared_from_this(), std::move(read_buffer));

			read_head();
		}
		else
		{
			cancel_timer();
			on_error(error);
		}
	}
} }