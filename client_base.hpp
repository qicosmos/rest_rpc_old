#pragma once

#include <string>
#include <Kapok/Kapok.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/preprocessor.hpp>
#include "consts.h"
#include "common.h"

class client_base
{
public:
	using io_service_t = boost::asio::io_service;
	using tcp = boost::asio::ip::tcp;

	struct head_t
	{
		int16_t data_type;
		int16_t	framework_type;
		int32_t len;
	};

protected:
	client_base(io_service_t& io)
		: io_(io)
		, socket_(io)
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

	std::string call_json(std::string const& json_str, framework_type ft = framework_type::DEFAULT)
	{
		bool r = send_json(json_str, ft);
		if (!r)
			throw std::runtime_error("call failed");

		return recieve_return();
	}

	std::string call_binary(uint8_t const* data, size_t length, framework_type ft = framework_type::DEFAULT)
	{
		bool r = send_binary(data, length, ft);
		if (!r)
			throw std::runtime_error("call failed");

		return recieve_return();
	}

private:

	bool send_json(std::string const& json_str, framework_type ft)
	{
		head_t head = 
		{ 
			json_str.length(), 
			static_cast<int16_t>(data_type::JSON), 
			static_cast<int16_t>(ft)
		};
		return send_impl(head, boost::asio::buffer(json_str));
	}

	bool send_binary(uint8_t const* data, size_t length, framework_type ft)
	{
		head_t head = 
		{ 
			length, 
			static_cast<int16_t>(data_type::BINARY),
			static_cast<int16_t>(ft)
		};
		return send_impl(head, boost::asio::buffer(data, length));
	}

	bool recieve()
	{
		boost::system::error_code ec;
		boost::asio::read(socket_, boost::asio::buffer(head_), ec);
		if (ec)
		{
			//log
			return false;
		}

		const int64_t i = *(int64_t*)(head_.data());
		const int body_len = i & 0xffff;
		const int type = i >> 32;
		if (body_len <= 0 || body_len > MAX_BUF_LEN)
		{
			return false;
		}
			
		boost::asio::read(socket_, boost::asio::buffer(recv_data_.data(), body_len), ec);
		if (ec)
		{
			return false;
		}

		return true;
	}

	bool send_impl(head_t const& head, boost::asio::const_buffer buffer)
	{
		std::vector<boost::asio::const_buffer> message;
		message.push_back(boost::asio::buffer(&head, sizeof(head_t)));
		message.push_back(buffer);
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

	std::string recieve_return()
	{
		auto r = recieve();
		if (!r)
			throw std::runtime_error("call failed");

		return std::string{ recv_data_.begin(), recv_data_.end() };
	}

protected:
	io_service_t&		io_;
	tcp::socket			socket_;
	std::array<char, HEAD_LEN>		head_;
	std::array<char, MAX_BUF_LEN>	recv_data_;
};

namespace protocol
{
	template <typename Func>
	struct protocol_base;

	template <typename Ret, typename ... Args>
	struct protocol_base<Ret(Args...)> : boost::function_traits<Ret(Args...)>
	{
		explicit protocol_base(std::string name)
			: name_(std::move(name))
		{}

		std::string make_json(Args&& ... args) const
		{
			Serializer sr;
			sr.Serialize(std::make_tuple(std::forward<Args>(args)...), name_.c_str());
			return sr.GetString();
		}

		template <typename Tag>
		std::string make_json(Tag&& tag, Args&& ... args) const
		{
			Serializer sr;
			sr.Serialize(std::make_tuple(std::forward<Tag>(tag), std::forward<Args>(args)...), name_.c_str());
			return sr.GetString();
		}

	private:
		std::string name_;
	};
}

#define TIMAX_DEFINE_PROTOCOL(handler, func_type) static const protocol::protocol_base<func_type> handler{ #handler }
#define TIMAX_MULTI_RESULT(...) std::tuple<__VA_ARGS__>
#define TIMAX_MULTI_RETURN(...) return std::make_tuple(__VA_ARGS__)

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

		// call without 
		template <typename Func, typename ... Args>
		auto call(Func const& func, Args&& ... args) -> typename Func::result_type
		{
			using result_type = typename Func::result_type;

			auto json_str = func.make_json(std::forward<Args>(args)...);
			auto result_str = client_base::call_json(json_str);
			
			DeSerializer dr;
			dr.Parse(result_str);
			auto& document = dr.GetDocument();
			if (static_cast<int>(result_code::OK) == document[CODE].GetInt())
			{
				response_msg<result_type> response = {};
				dr.Deserialize(response);
				return response.result;
			}
			else
			{
				throw;
			}
		}
	};
}
