#pragma once

namespace timax { namespace rpc 
{
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
		{

		}

	public:
		void connect(std::string const& address, std::string const& port)
		{
			tcp::resolver resolver(io_);
			tcp::resolver::query query(tcp::v4(), address, port);
			tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
			boost::asio::connect(socket_, endpoint_iterator);
			//set_no_delay();
		}

		std::string call_json(std::string const& json_str, framework_type ft = framework_type::DEFAULT)
		{
			bool r = send_json(json_str, ft);
			if (!r)
				throw std::runtime_error("call failed");

			return recieve_return();
		}

		std::string call_binary(std::string const& handler_name, char const* data, size_t length, framework_type ft = framework_type::DEFAULT)
		{
			bool r = send_binary(handler_name, data, length, ft);
			if (!r)
				throw std::runtime_error("call failed");

			return recieve_return();
		}

		size_t recieve()
		{
			boost::system::error_code ec;
			boost::asio::read(socket_, boost::asio::buffer(head_), ec);
			if (ec)
			{
				//log
				return 0;
			}

			const int64_t i = *(int64_t*)(head_.data());
			head_t h = *(head_t*)(head_.data());
			const size_t body_len = h.len;

			if (body_len <= 0 || body_len > MAX_BUF_LEN)
			{
				return 0;
			}

			boost::asio::read(socket_, boost::asio::buffer(recv_data_.data(), body_len), ec);
			if (ec)
			{
				return 0;
			}

			return body_len;
		}

		const char* data() const
		{
			return recv_data_.data();
		}

	protected:

		bool send_json(std::string const& json_str, framework_type ft)
		{
			head_t head =
			{
				static_cast<int16_t>(data_type::JSON),
				static_cast<int16_t>(ft),
				static_cast<int32_t>(json_str.length())
			};

			const auto& message = get_json_messages(head, json_str);
			return send_impl(message);
		}

		bool send_binary(std::string const& handler_name, char const* data, size_t length, framework_type ft)
		{
			head_t head =
			{
				static_cast<int16_t>(data_type::BINARY),
				static_cast<int16_t>(ft),
				static_cast<int32_t>(handler_name.length() + 1 + length)
			};
			const auto& message = get_binary_messages(head, handler_name, data, length);
			return send_impl(message);
		}

		bool send_impl(const std::vector<boost::asio::const_buffer>& message)
		{
			boost::system::error_code ec;
			boost::asio::write(socket_, message, ec);
			if (ec)
			{
				//log
				return false;
			}
			else
			{
				return true;
			}
		}

		std::vector<boost::asio::const_buffer> get_json_messages(head_t const& head, std::string const& json_str)
		{
			std::vector<boost::asio::const_buffer> message;
			message.push_back(boost::asio::buffer(&head, sizeof(head_t)));
			message.push_back(boost::asio::buffer(json_str));
			return message;
		}

		std::vector<boost::asio::const_buffer> get_binary_messages(head_t const& head, std::string const& handler_name, const char* data, size_t len)
		{
			std::vector<boost::asio::const_buffer> message;
			message.push_back(boost::asio::buffer(&head, sizeof(head_t)));
			message.push_back(boost::asio::buffer(handler_name.c_str(), handler_name.length() + 1));
			message.push_back(boost::asio::buffer(data, len));
			return message;
		}

		std::string recieve_return()
		{
			size_t len = recieve();

			if (len == 0)
				throw std::runtime_error("call failed");

			return std::string{ recv_data_.begin(), recv_data_.begin() + len };
		}

		void set_no_delay()
		{
			boost::asio::ip::tcp::no_delay option(true);
			boost::system::error_code ec;
			socket_.set_option(option, ec);
		}

	protected:
		io_service_t&					io_;
		tcp::socket						socket_;
		std::string						address_;
		std::string						port_;
		std::array<char, HEAD_LEN>		head_;
		std::array<char, MAX_BUF_LEN>	recv_data_;
	};

} }

namespace timax { namespace rpc
{
	class sync_client : public client_base
	{
	public:
		sync_client(io_service_t& io)
			: client_base(io)
		{

		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)// -> typename Protocol::result_type
		{
			auto json_str = protocol.make_json(std::forward<Args>(args)...);
			auto result_str = client_base::call_json(json_str, protocol.get_type());
			return protocol.parse_json(result_str);
		}

		template <typename Protocol>
		std::string call_binary(Protocol const& protocol, const char* data, size_t len)
		{
			return client_base::call_binary(protocol.name(), data, len, protocol.get_type());
		}

		std::string call_binary(const std::string& handler_name, const char* data, size_t len, framework_type ft = framework_type::DEFAULT)
		{
			return client_base::call_binary(handler_name, data, len, ft);
		}

		std::string sub(const std::string& topic)
		{
			return call(SUB_TOPIC, topic);
		}

		template<typename Protocol>
		std::string sub(Protocol const& protocol)
		{
			return sub(protocol.name());
		}

		template<typename... Args>
		void pub(const char* handler_name, Args&&... args)
		{
			auto json_str = make_request_json(handler_name, std::forward<Args>(args)...);
			send_json(json_str, framework_type::DEFAULT);
		}

		template <typename Protocol, typename ... Args>
		void pub(Protocol const& protocol, Args&& ... args)// -> typename Protocol::result_type
		{
			auto json_str = protocol.make_json(std::forward<Args>(args)...);
			send_json(json_str, framework_type::DEFAULT);
		}

		template<typename... Args>
		std::string call(const char* handler_name, Args&&... args)
		{
			auto json_str = make_request_json(handler_name, std::forward<Args>(args)...);
			return call_json(json_str);
		}

	private:
		template<typename T>
		std::string make_request_json(const char* handler_name, T&& t)
		{
			Serializer sr;
			sr.Serialize(std::forward<T>(t), handler_name);
			return sr.GetString();
		}

		std::string make_request_json(const char* handler_name)
		{
			return make_request_json(handler_name, "");
		}

		template<typename... Args>
		std::string make_request_json(const char* handler_name, Args&&... args)
		{
			auto tp = std::make_tuple(std::forward<Args>(args)...);
			return make_request_json(handler_name, tp);
		}
	};

	// client without protocol
	using sync_raw_client = client_base;
} }