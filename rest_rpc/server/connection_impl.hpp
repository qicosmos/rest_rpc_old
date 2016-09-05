#pragma once

namespace timax { namespace rpc 
{
	template <typename Decode>
	connection<Decode>::connection(server_ptr server, boost::asio::io_service& io_service, std::size_t timeout_milli)
		: server_(server)
		, socket_(io_service)
		, head_(), data_()
		, timer_(io_service)
		, timeout_milli_(timeout_milli)
	{

	}

	template <typename Decode>
	void connection<Decode>::start()
	{
		set_no_delay();
		read_head();
	}

	template <typename Decode>
	tcp::socket& connection<Decode>::socket()
	{
		return socket_;
	}

	template <typename Decode>
	void connection<Decode>::read_head()
	{
		reset_timer();
		auto self(this->shared_from_this());
		boost::asio::async_read(socket_, boost::asio::buffer(&head_, sizeof(head_t)), [this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!socket_.is_open())
			{
				cancel_timer();
				return;
			}

			if (!ec)
			{
				if (head_.len > 0 && head_.len < MAX_BUF_LEN)
				{
					read_body();
					return;
				}

				if (head_.len == 0) //nobody, just head.
				{
					read_head();
				}
				else
				{
					SPD_LOG_ERROR("invalid body len {}", head_.len);
					cancel_timer();
				}
			}
			else
			{
				server_->remove_sub_conn(self.get());
				SPD_LOG_ERROR(ec.message().c_str());
				cancel_timer();
			}
		});
	}

	template <typename Decode>
	void connection<Decode>::read_body()
	{
		auto self(this->shared_from_this());
		data_.resize(head_.len);
		boost::asio::async_read(socket_, boost::asio::buffer(data_), [this, self](boost::system::error_code ec, std::size_t length)
		{
			cancel_timer();

			if (!socket_.is_open())
				return;

			if (!ec)
			{
				server_->callback(self, data_.data(), length);
			}
			else
			{
				SPD_LOG_ERROR(ec.message().c_str());
			}
		});
	}

	template <typename Decode>
	void connection<Decode>::reset_timer()
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
			server_->remove_sub_conn(self.get());
			close();
		});
	}

	template <typename Decode>
	void connection<Decode>::cancel_timer()
	{
		if (timeout_milli_ == 0)
			return;
	
		timer_.cancel();
	}

	template <typename Decode>
	void connection<Decode>::close()
	{
		boost::system::error_code ignored_ec;
		socket_.close(ignored_ec);
	}

	template <typename Decode>
	void connection<Decode>::set_no_delay()
	{
		boost::asio::ip::tcp::no_delay option(true);
		boost::system::error_code ec;
		socket_.set_option(option, ec);
	}

	template <typename Decode>
	void connection<Decode>::response(const char* data, size_t size, result_code code)
	{
		if (size > MAX_BUF_LEN - HEAD_LEN)
			throw std::overflow_error("the size is too big");

		write(get_message(data, size, code));
	}

	template <typename Decode>
	void connection<Decode>::response(std::string const& topic, char const* data, size_t size, result_code code)
	{
		write(get_message(topic, data, size, code));
	}

	template <typename Decode>
	auto connection<Decode>::get_message(const char* data, size_t size, result_code code)
		-> std::vector<boost::asio::const_buffer>
	{
		head_.code = static_cast<int16_t>(code);
		head_.len = static_cast<uint32_t>(size);

		return
		{ 
			boost::asio::buffer(&head_, sizeof(head_t)),
			boost::asio::buffer(data, size)
		};
	}

	template <typename Decode>
	auto connection<Decode>::get_message(std::string const& topic, const char* data, size_t size, result_code code)
		-> std::vector<boost::asio::const_buffer>
	{
		head_.code = static_cast<int16_t>(code);
		head_.len = static_cast<uint32_t>(size + topic.size() + 1);

		return
		{
			boost::asio::buffer(&head_, sizeof(head_t)),
			boost::asio::buffer(topic),
			boost::asio::buffer(data, size)
		};
	}

	template <typename Decode>
	void connection<Decode>::write(std::vector<boost::asio::const_buffer> const& message)
	{
		auto self = this->shared_from_this();
		boost::asio::async_write(socket_, message, [this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				g_succeed_count++;
				read_head();
			}
			else
			{
				SPD_LOG_ERROR(ec.message().c_str());
			}
		});
	}
} }