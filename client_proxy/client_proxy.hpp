#pragma once
#include <string>
#include <kapok/Kapok.hpp>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class client_proxy : public std::enable_shared_from_this<client_proxy>, private boost::noncopyable
{
public:
	client_proxy(boost::asio::io_service& io_service,const std::string& addr, const std::string& port)
		: io_service_(io_service),
		socket_(io_service)
	{
		tcp::resolver resolver(io_service);
		tcp::resolver::query query(tcp::v4(), addr, port);
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		do_connect(endpoint_iterator);
	}

	template<typename... Args>
	std::string make_json(const char* handler_name, Args&&... args)
	{
		return make_request_json(handler_name, std::forward<Args>(args)...);
	}

	std::string call(const std::string& json_str)
	{
		int len = json_str.length();

		message_.push_back(boost::asio::buffer(&len, 4));
		message_.push_back(boost::asio::buffer(json_str));
		socket_.send(message_);
		message_.clear();
		socket_.receive(boost::asio::buffer(&len, 4));
		std::string recv_json;
		recv_json.resize(len);
		socket_.receive(boost::asio::buffer(&recv_json[0], len));
		return recv_json;
	}

	template<typename... Args>
	std::string call(const char* handler_name, Args&&... args)
	{
		auto json_str = make_request_json(handler_name, std::forward<Args>(args)...);
		return call(json_str);
	}

private:
	void do_connect(tcp::resolver::iterator endpoint_iterator)
	{
		boost::system::error_code ec;
		boost::asio::connect(socket_, endpoint_iterator, ec);
		if (ec)
			std::cout << ec.message() << std::endl;
		else
		{
			//do_read();
		}
	}

	void do_read()
	{
		boost::asio::async_read(socket_, boost::asio::buffer(data_, max_length), [](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				
			}
			else
			{
				//log
				return;
			}
		});
	}

	template<typename T>
	std::string make_request_json(const char* handler_name, T&& t)
	{
		sr_.Serialize(std::forward<T>(t), handler_name);
		return sr_.GetString();
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

private:
	Serializer sr_;

	boost::asio::io_service& io_service_;
	tcp::socket socket_;
	std::vector<boost::asio::const_buffer> message_;
	enum { max_length = 8192 };
	char data_[max_length];
};

