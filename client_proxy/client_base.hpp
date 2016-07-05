#pragma once

#include <string>
#include <kapok/Kapok.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/preprocessor.hpp>
#include "../consts.h"
#include "../common.h"
#include "../function_traits.hpp"
#include "protocol.hpp"

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

		if (len==0)
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

namespace protocol
{
	template <typename Func>
	struct protocol_define;

	template <typename Func, typename Tag, typename TagPolicy>
	struct protocol_with_tag;

	template <typename Ret, typename ... Args>
	struct protocol_define<Ret(Args...)>// : function_traits<Ret(Args...)>
	{
		using result_type = typename function_traits<Ret(Args...)>::return_type;

		explicit protocol_define(std::string name)
			: name_(std::move(name))
		{}

		std::string make_json(Args&& ... args) const
		{
			Serializer sr;
			sr.Serialize(std::make_tuple(std::forward<Args>(args)...), name_.c_str());
			return sr.GetString();
		}

		result_type parse_json(std::string const& json_str) const
		{
			DeSerializer dr;
			dr.Parse(json_str);
			auto& document = dr.GetDocument();
			if (static_cast<int>(result_code::OK) == document[CODE].GetInt())
			{
				response_msg<result_type> response;
				dr.Deserialize(response);
				return response.result;
			}
			else
			{
				throw;
			}
		}

		std::string const& name() const noexcept
		{
			return name_;
		}

		framework_type get_type() const noexcept
		{
			return framework_type::DEFAULT;
		}

	private:
		std::string name_;
	};

	template <typename Ret, typename ... Args, typename Tag, typename TagPolicy>
	struct protocol_with_tag<Ret(Args...), Tag, TagPolicy>
	{
		using protocol_basic_t = protocol_define<Ret(Args...)>;
		using result_type = typename protocol_basic_t::result_type;
		using tag_t = Tag;

		protocol_with_tag(protocol_basic_t const& protocol, tag_t tag)
			: tag_(std::move(tag))
			, protocol_(protocol)
		{

		}

		std::string make_json(Args&& ... args) const
		{
			Serializer sr;
			sr.Serialize(std::make_tuple(tag_, std::forward<Args>(args)...), protocol_.name().c_str());
			return sr.GetString();
		}

		result_type parse_json(std::string const& json_str) const
		{
			DeSerializer dr;
			dr.Parse(json_str);
			auto& document = dr.GetDocument();
			if (static_cast<int>(result_code::OK) == document[CODE].GetInt())
			{
				response_msg<result_type, tag_t> response;
				dr.Deserialize(response);
				if (TagPolicy{}(tag_, response.tag))
				{
					return response.result;
				}
				throw;
			}
			else
			{
				throw;
			}
		}

		framework_type get_type() const noexcept
		{
			return framework_type::ROUNDTRIP;
		}

	private:
		tag_t tag_;
		protocol_basic_t const& protocol_;
	};
}

template <typename Func, typename Tag, typename TagPolicy = std::equal_to<std::decay_t<Tag>>>
auto with_tag(protocol::protocol_define<Func> const& protocol, Tag&& tag, TagPolicy = TagPolicy{})
{
	using tag_t = std::remove_reference_t<std::remove_cv_t<Tag>>;
	using protoco_with_tag_t = protocol::protocol_with_tag<Func, tag_t, std::equal_to<tag_t>>;
	return protoco_with_tag_t{ protocol, std::forward<Tag>(tag) };
}

#define TIMAX_DEFINE_PROTOCOL(handler, func_type) static const ::protocol::protocol_define<func_type> handler{ #handler }

namespace timax
{
	template <typename Func, typename ... Args>
	struct is_argument_match
	{
	private:
		template <typename T>
		static std::false_type test(...);

		template <typename T, typename =
			decltype(std::declval<T>()(std::declval<Args>()...))>
		static std::true_type test(int);

		using result_type = decltype(test<Func>(0));
	public:
		static constexpr bool value = result_type::value;
	};

	class client_proxy : public client_base
	{
	public:
		client_proxy(io_service_t& io)
			: client_base(io)
		{

		}

		template <typename Protocol, typename ... Args>
		auto call(Protocol const& protocol, Args&& ... args)// -> typename Protocol::result_type
		{
			auto json_str = protocol.make_json(std::forward<std::remove_reference_t<Args>>(args)...);
			auto result_str = client_base::call_json(json_str, protocol.get_type());
			return protocol.parse_json(result_str);
		}

		template <typename Protocol>
		std::string call_binary(Protocol const& protocol, const char* data, size_t len)
		{
			return client_base::call_binary(protocol.name(), data, len, protocol.get_type());
		}

		std::string call_binary(const std::string& handler_name, const char* data, size_t len, framework_type ft= framework_type::DEFAULT)
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
}