#pragma once

namespace timax { namespace rpc 
{
	class client_exception
	{
	public:
		client_exception(result_code code, std::string message)
			: code_(code)
			, message_(std::move(message))
		{

		}

		result_code errcode() const noexcept
		{
			return code_;
		}

		std::string const& message() const noexcept
		{
			return message_;
		}

		std::string what() const
		{
			std::string what;
			switch (code_)
			{
			case result_code::FAIL:
				what = "User defined failure. ";
				break;
			case result_code::EXCEPTION:
				what = "Exception. ";
				break;
			case result_code::ARGUMENT_EXCEPTION:
				what = "Argument exception. ";
				break;
			default:
				what = "Unknown failure. ";
				break;
			}
			what += message_;
			return what;
		}

	private:
		result_code		code_;
		std::string		message_;
	};

	class client_base
	{
	public:
		using io_service_t = boost::asio::io_service;
		using tcp = boost::asio::ip::tcp;

	protected:
		client_base(io_service_t& io)
			: io_(io)
			, socket_(io)
		{

		}

		client_base(io_service_t& io, std::string address, std::string port)
			: io_(io)
			, socket_(io)
			, address_(std::move(address))
			, port_(std::move(port))
			, head_t_(nullptr)
		{

		}

	public:
		void connect(std::string const& address, std::string const& port)
		{
			tcp::resolver resolver(io_);
			tcp::resolver::query query(tcp::v4(), address, port);
			tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
			boost::asio::connect(socket_, endpoint_iterator);
		}

		void disconnect()
		{
			socket_.shutdown(boost::asio::socket_base::shutdown_both);
			socket_.close();
		}

		size_t receive()
		{
			receive_head();
			receive_body();

			if (!check_head())
				return 0;

			return head_t_->len;
		}

		const char* recv_data() const noexcept
		{
			return recv_data_.data();
		}

	protected:

		void call(std::string const& handle_name, char const* data, size_t size)
		{
			if (!send(handle_name, data, size))
				throw std::runtime_error("call failed");
		}

		void receive_head()
		{
			boost::system::error_code ec;
			boost::asio::read(socket_, boost::asio::buffer(head_), ec);
			if (ec)
			{
				//log
				//std::cout << ec.message() << std::endl;
				throw std::runtime_error(ec.message());
			}

			const int64_t i = *(int64_t*)(head_.data());
			head_t_ = (head_t*)(head_.data());
		}

		void receive_body()
		{
			const size_t body_len = head_t_->len;
			boost::system::error_code ec;

			if (body_len <= 0 || body_len > MAX_BUF_LEN)
			{
				throw std::runtime_error("call failed");
			}

			boost::asio::read(socket_, boost::asio::buffer(recv_data_.data(), body_len), ec);
			if (ec)
			{
				throw std::runtime_error(ec.message());
			}
		}

		bool send(std::string const& handler_name, char const* data, size_t size)
		{
			head_t head =
			{
				0, 0,
				static_cast<int32_t>(size + handler_name.size() + 1)
			};

			auto message = get_messages(head, handler_name, data, size);
			return send_impl(message);
		}

		bool send_impl(const std::vector<boost::asio::const_buffer>& message)
		{
			boost::system::error_code ec;
			boost::asio::write(socket_, message, ec);
			if (ec)
			{
				//log
				std::cout << ec.message() << std::endl;
				return false;
			}
			else
			{
				return true;
			}
		}

		std::vector<boost::asio::const_buffer> get_messages(head_t const& head, std::string const& handler_name, const char* data, size_t len)
		{
			std::vector<boost::asio::const_buffer> message;
			message.push_back(boost::asio::buffer(&head, sizeof(head_t)));
			message.push_back(boost::asio::buffer(handler_name.c_str(), handler_name.length() + 1));
			message.push_back(boost::asio::buffer(data, len));
			return message;
		}

		void set_no_delay()
		{
			boost::asio::ip::tcp::no_delay option(true);
			boost::system::error_code ec;
			socket_.set_option(option, ec);
		}

		bool check_head()
		{
			return (nullptr != head_t_) && (head_t_->code != 0);
		}

	protected:
		io_service_t&					io_;
		tcp::socket						socket_;
		std::string						address_;
		std::string						port_;
		std::array<char, HEAD_LEN>		head_;
		std::array<char, MAX_BUF_LEN>	recv_data_;
		head_t*							head_t_;
	};

	template <typename Marshal>
	class sync_client : public client_base
	{
		using base_type = client_base;

	public:
		sync_client(io_service_t& io)
			: client_base(io)
		{

		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)
			-> std::enable_if_t<!std::is_void<typename Protocol::result_type>::value, typename Protocol::result_type>
		{
			// pack arguments to buffer
			auto buffer = protocol.pack_args(marshal_, std::forward<Args>(args)...);

			// call the rpc
			base_type::call(protocol.name(), buffer.data(), buffer.size());

			base_type::receive_head();
			check_head();
			base_type::receive_body();
			// unpack the receive data
			return protocol.unpack(marshal_, recv_data(), head_t_->len);
		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)
			-> std::enable_if_t<std::is_void<typename Protocol::result_type>::value>
		{
			auto buffer = protocol.pack_args(marshal_, std::forward<Args>(args)...);
			base_type::call(protocol.name(), buffer.data(), buffer.size());
			base_type::receive_head();
			check_head();
		}

		template <typename Protocol, typename ... Args>
		auto pub(Protocol const& protocol, Args&& ... args)
		{
			return call(protocol, std::forward<Args>(args)...);
		}

		template <typename Protocol, typename F>
		auto sub(Protocol const& protocol, F&& f)
			-> std::enable_if_t<std::is_void<typename Protocol::result_type>::value>
		{
			if (!call(sub_topic, protocol.name()))
			{
				throw std::runtime_error{ "Failed to register topic." };
			}

			while (true)
			{
				base_type::receive_head();
				check_head();
				f();
			}
		}

		template <typename Protocol, typename F>
		auto sub(Protocol const& protocol, F&& f)
			-> std::enable_if_t<!std::is_void<typename Protocol::result_type>::value>
		{
			if (!call(sub_topic, protocol.name()))
			{
				throw std::runtime_error{ "Failed to register topic." };
			}

			while (true)
			{
				base_type::receive_head();
				check_head();
				base_type::receive_body();
				
				f(protocol.unpack(marshal_, recv_data(), head_t_->len););
			}
		}

	private:
		void check_head()
		{
			if (!base_type::check_head())
			{
				throw client_exception
				{
					static_cast<result_code>(head_t_->code),
					std::move(marshal_.template unpack<std::string>(recv_data(), head_t_->len))
				};
			}
		}

	private:
		Marshal		marshal_;
	};

} }

namespace timax { namespace rpc
{

	// client without protocol
	using sync_raw_client = client_base;
} }