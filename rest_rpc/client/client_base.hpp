#pragma once
#include <atomic>

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
		client_base() : socket_(io_)
		{

		}

		client_base(std::string address, std::string port) : socket_(io_)
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
			address_ = address;
			port_ = port;
			is_connected_ = true;
		}

		void disconnect()
		{
			socket_.shutdown(boost::asio::socket_base::shutdown_both);
			socket_.close();
			is_connected_ = false;
		}

		bool is_connected() const
		{
			return is_connected_;
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

			if (body_len <= 0 || body_len > MAX_BUF_LEN - HEAD_LEN)
			{
				throw std::overflow_error("Size too big!");
			}

			recv_data_.resize(body_len);
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
				0, 0, 0,
				static_cast<uint32_t>(size + handler_name.size() + 1)
			};

			if (head.len > MAX_BUF_LEN - HEAD_LEN)
			{
				// TODO throw a right exception
				throw std::overflow_error("Size too big!");
			}

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
			return (nullptr != head_t_) && (head_t_->code == (int16_t)result_code::OK);
		}

	protected:
		io_service_t					io_;
		tcp::socket						socket_;
		std::string						address_;
		std::string						port_;
		std::array<char, HEAD_LEN>		head_;
		std::vector<char>				recv_data_;
		head_t*							head_t_;
		bool is_connected_ = false;
	};

	template <typename Marshal>
	class sync_client : public client_base
	{
		using base_type = client_base;
		using marshal_policy = Marshal;

	public:
		sync_client() : need_cancel_(false)
		{

		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)
			-> std::enable_if_t<!std::is_void<typename Protocol::result_type>::value, typename Protocol::result_type>
		{
			if (!is_connected())
				connect(address_, port_);

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
			if (!is_connected())
				connect(address_, port_);

			auto buffer = protocol.pack_args(marshal_, std::forward<Args>(args)...);
			base_type::call(protocol.name(), buffer.data(), buffer.size());
			//base_type::receive_head();
			//check_head();
		}

		template <typename T=void, typename ... Args>
		auto call(const string& rpc_service, Args&& ... args)
		{
			if (!is_connected())
				connect(address_, port_);

			auto buffer = marshal_.pack_args(std::forward<Args>(args)...);
			base_type::call(rpc_service, buffer.data(), buffer.size());
			base_type::receive_head();
			check_head();
			base_type::receive_body();
			// unpack the receive data
			return marshal_.unpack<T>(recv_data(), head_t_->len);
		}

		template <typename ... Args>
		void call_void(const string& rpc_service, Args&& ... args)
		{
			if (!is_connected())
				connect(address_, port_);

			auto buffer = marshal_.pack_args(std::forward<Args>(args)...);
			base_type::call(rpc_service, buffer.data(), buffer.size());
		}

		template <typename Protocol, typename ... Args>
		auto pub(Protocol const& protocol, Args&& ... args)
		{
			if (!is_connected())
				connect(address_, port_);

			return call(protocol, std::forward<Args>(args)...);

			//auto buffer = protocol.pack_args(marshal_, std::forward<Args>(args)...);
			//base_type::call(protocol.name(), buffer.data(), buffer.size());
		}

		//template <typename Protocol, typename F>
		//auto sub(Protocol const& protocol, F&& f)
		//	-> std::enable_if_t<std::is_void<typename Protocol::result_type>::value>
		//{
		//	std::string result = call(sub_topic, protocol.name());
		//	if (result.empty())
		//	{
		//		throw std::runtime_error{ "Failed to register topic." };
		//	}

		//	while (true)
		//	{
		//		base_type::receive_head();
		//		check_head();
		//		f();
		//	}
		//}

		template <typename Protocol, typename F>
		auto sub(Protocol const& protocol, F&& f)
			-> std::enable_if_t<!std::is_void<typename Protocol::result_type>::value>
		{
			if (!is_connected())
				connect(address_, port_);

			std::string result = call(sub_topic, protocol.name());
			
			if (result.empty())
			{
				throw std::runtime_error{ "Failed to register topic." };
			}

			std::string result_confirm = call(sub_confirm, protocol.name());

			if (result_confirm.empty())
			{
				throw std::runtime_error{ "Failed to register confirm." };
			}

			while (!need_cancel_)
			{
				base_type::receive_head();
				check_head();
				base_type::receive_body();
				
				f(protocol.unpack(marshal_, recv_data(), head_t_->len));
			}

			disconnect();
		}

		void cancel_sub_topic(const std::string& topic)
		{
			need_cancel_ = true;
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
		marshal_policy		marshal_;
		std::atomic<bool>	need_cancel_;		
	};

} }

namespace timax { namespace rpc
{

	// client without protocol
	using sync_raw_client = client_base;
} }