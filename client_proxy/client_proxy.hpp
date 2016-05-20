#pragma once
#include <string>
#include <kapok/Kapok.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>


using boost::asio::ip::tcp;

#include <boost/asio/yield.hpp>
template<typename HandlerT>
struct call_detail :
	public boost::enable_shared_from_this<call_detail<HandlerT>>,
	boost::asio::coroutine
{
	call_detail(const std::string& json_str, tcp::socket& socket, HandlerT handler)
		:json_str_(json_str), len_(json_str.size()), socket_(socket), handler_(handler)
	{
		message_.push_back(boost::asio::buffer(&len_, 4));
		message_.push_back(boost::asio::buffer(json_str_));
	}
	void do_call(boost::system::error_code const& ec)
	{
#define __CHECK_RETURN()	if (ec) {handler_(ec, std::string());return;}

		reenter(this)
		{
			yield socket_.async_send(message_, boost::bind(&call_detail<HandlerT>::do_call, this->shared_from_this(), boost::asio::placeholders::error));
			__CHECK_RETURN();
			message_.clear();
			yield socket_.async_receive(boost::asio::buffer(&len_, 4), boost::bind(&call_detail<HandlerT>::do_call, this->shared_from_this(), boost::asio::placeholders::error));
			__CHECK_RETURN();
			recv_json_.resize(len_);
			yield socket_.async_receive(boost::asio::buffer(&recv_json_[0], len_), boost::bind(&call_detail<HandlerT>::do_call, this->shared_from_this(), boost::asio::placeholders::error));
			__CHECK_RETURN();
			handler_(ec, recv_json_);
		}
	}

private:
	int len_;
	std::vector<boost::asio::const_buffer> message_;
	std::string recv_json_;
	std::string json_str_;

	tcp::socket& socket_;
	HandlerT handler_;
};
#include <boost/asio/unyield.hpp>

#include <boost/lambda/bind.hpp>
#include <boost/lambda/lambda.hpp>

class client_proxy : private boost::noncopyable
{
public:
	client_proxy(boost::asio::io_service& io_service)
		: io_service_(io_service),
		socket_(io_service)
	{}

	template<typename... Args>
	std::string make_json(const char* handler_name, Args&&... args)
	{
		return make_request_json(handler_name, std::forward<Args>(args)...);
	}

	std::string call(const std::string& json_str)
	{
		int len = json_str.length();

		std::vector<boost::asio::const_buffer> message;
		message.push_back(boost::asio::buffer(&len, 4));
		message.push_back(boost::asio::buffer(json_str));
		socket_.send(message);
		message.clear();
		socket_.receive(boost::asio::buffer(&len, 4));
		std::string recv_json;
		recv_json.resize(len);
		socket_.receive(boost::asio::buffer(&recv_json[0], len));
		return recv_json;
	}

	template<typename HandlerT>
	void async_call(const std::string& json_str, HandlerT handler)
	{
		auto helper = boost::make_shared<call_detail<HandlerT>>(json_str, socket_, handler);
		helper->do_call({});
	}

	template<typename... Args>
	std::string call(const char* handler_name, Args&&... args)
	{
		auto json_str = make_request_json(handler_name, std::forward<Args>(args)...);
		return call(json_str);
	}

	template<typename HandlerT, typename... Args>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(HandlerT, void(boost::system::error_code, std::string))
	async_call(const char* handler_name, HandlerT handler, Args&&... args)
	{
		boost::asio::detail::async_result_init<
			HandlerT, void(boost::system::error_code, std::string)> init(
				BOOST_ASIO_MOVE_CAST(HandlerT)(handler));

		using namespace boost::asio;
		async_call_impl<
			BOOST_ASIO_HANDLER_TYPE(HandlerT, void(boost::system::error_code, std::string))
		>(handler_name, init.handler, std::forward<Args>(args)...);
		return init.result.get();
	}

	void connect(const std::string& addr, const std::string& port)
	{
		tcp::resolver resolver(io_service_);
		tcp::resolver::query query(tcp::v4(), addr, port);
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

		boost::asio::connect(socket_, endpoint_iterator);
	}

	void connect(const std::string& addr, const std::string& port, int timeout)
	{
		tcp::resolver::query query(addr, port);
		tcp::resolver::iterator iter = tcp::resolver(io_service_).resolve(query);

		boost::system::error_code ec = boost::asio::error::would_block;
		socket_.async_connect(iter->endpoint(), boost::lambda::var(ec) = boost::lambda::_1);

		boost::asio::deadline_timer timer(io_service_);

		timer.expires_from_now(boost::posix_time::millisec(timeout));
		timer.async_wait([this](boost::system::error_code ec)
		{
			if (ec)
			{
				//TODO: log
				return;
			}
			socket_.close(ec);
		});

		// Block until the asynchronous operation has completed.
		do io_service_.run_one(); while (ec == boost::asio::error::would_block);

		if (ec || !socket_.is_open())
			throw boost::system::system_error(ec ? ec : boost::asio::error::operation_aborted);

		timer.cancel(ec);
	}

	template<typename HandlerT>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(HandlerT, void(boost::system::error_code))
	async_connect(const std::string& addr, const std::string& port, HandlerT handler)
	{
		boost::asio::detail::async_result_init<
			HandlerT, void(boost::system::error_code)> init(
				BOOST_ASIO_MOVE_CAST(HandlerT)(handler));

		using namespace boost::asio;
		async_connect_impl<
			BOOST_ASIO_HANDLER_TYPE(HandlerT, void(boost::system::error_code))
		>(addr, port, init.handler);
		return init.result.get();
	}

	template<typename HandlerT>
	void async_connect_impl(const std::string& addr, const std::string& port, HandlerT handler)
	{
		tcp::resolver resolver(io_service_);
		tcp::resolver::query query(tcp::v4(), addr, port);
		boost::system::error_code ec;
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, ec);
		if (ec)
		{
			handler(ec);
			return;
		}

		boost::asio::async_connect(socket_, endpoint_iterator,
			[handler](boost::system::error_code ec, tcp::resolver::iterator) mutable
		{
			handler(ec);
		});
	}

	template<typename HandlerT>
	inline BOOST_ASIO_INITFN_RESULT_TYPE(HandlerT, void(boost::system::error_code))
		async_connect(const std::string& addr, const std::string& port, int timeout, HandlerT handler)
	{
		boost::asio::detail::async_result_init<
			HandlerT, void(boost::system::error_code)> init(
				BOOST_ASIO_MOVE_CAST(HandlerT)(handler));

		using namespace boost::asio;
		async_connect_impl<
			BOOST_ASIO_HANDLER_TYPE(HandlerT, void(boost::system::error_code))
		>(addr, port, timeout, init.handler);
		return init.result.get();
	}

private:
	template<typename HandlerT, typename... Args>
	void async_call_impl(const char* handler_name, HandlerT handler, Args&&... args)
	{
		auto json_str = make_request_json(handler_name, std::forward<Args>(args)...);
		async_call(json_str, handler);
	}

	template<typename HandlerT>
	void async_connect_impl(const std::string& addr, const std::string& port, int timeout, HandlerT handler)
	{
		tcp::resolver resolver(io_service_);
		tcp::resolver::query query(tcp::v4(), addr, port);
		boost::system::error_code ec;
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query, ec);
		if (ec)
		{
			handler(ec);
			return;
		}

		auto timer = boost::make_shared<boost::asio::deadline_timer>(io_service_);
		timer->expires_from_now(boost::posix_time::millisec(timeout));
		timer->async_wait([this](boost::system::error_code ec)
		{
			if (ec)
			{
				//TODO: log
				return;
			}
			socket_.close(ec);
		});

		boost::asio::async_connect(socket_, endpoint_iterator,
			[handler, timer](boost::system::error_code ec, tcp::resolver::iterator) mutable
		{
			boost::system::error_code ignored_ec;
			timer->cancel(ignored_ec);
			handler(ec);
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
	enum { max_length = 8192 };
	char data_[max_length];
};

