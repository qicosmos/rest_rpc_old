#pragma once
#include <boost/asio.hpp>
#include <msgpack.hpp>
#include <string>
#include <boost/timer.hpp>

using blob = msgpack::type::raw_ref;
namespace timax{namespace rpc
{
	class unified_client
	{
	public:
		unified_client() : socket_(ios_) {}

		bool connect(std::string const& address, std::string const& port)
		{
			boost::asio::ip::tcp::resolver resolver(ios_);
			tcp::resolver::query query(tcp::v4(), address, port);
			tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
			boost::system::error_code ec;
			boost::asio::connect(socket_, endpoint_iterator, ec);

			return ec == 0;
			//set_no_delay();
		}

		template<typename... Args>
		void call(const std::string& handler_name, Args&&... args)
		{
			const auto& pack = get_pack_message(handler_name, std::forward<Args>(args)...);
			head_t head =
			{
				static_cast<int16_t>(data_type::JSON),
				static_cast<int16_t>(framework_type::DEFAULT),
				static_cast<int32_t>(pack.size())
			};

			bool r = send_impl(get_messages(head, pack));
			auto result = recieve_return();
		}

		template<typename... Args>
		msgpack::sbuffer get_pack_message(Args&&... args)
		{
			auto tp = std::make_tuple(std::forward<Args>(args)...);
			std::cout << typeid(tp).name() << std::endl;
			msgpack::sbuffer buffer;
			msgpack::pack(buffer, tp);

			return buffer;
			//msgpack::unpacked msg;
			//msgpack::unpack(&msg, buffer.data(), buffer.size());
			//decltype(tp) tp1;
			//msg.get().convert(tp1);

			//char buf[7];
			//memcpy(buf, std::get<2>(tp1).ptr, 6);
		}

		std::vector<boost::asio::const_buffer> get_messages(const head_t& head, const msgpack::sbuffer& sbuf)
		{
			std::vector<boost::asio::const_buffer> message;
			message.push_back(boost::asio::buffer(&head, sizeof(head_t)));
			message.push_back(boost::asio::buffer(sbuf.data(), sbuf.size()));
			return message;
		}

	private:
		bool send_impl(const std::vector<boost::asio::const_buffer>& messages)
		{
			boost::system::error_code ec;
			boost::asio::write(socket_, messages, ec);
			if (ec)
			{
				//log
				//std::cout << ec.message() << std::endl;
				return false;
			}
			else
			{
				return true;
			}
		}

		std::string recieve_return()
		{
			size_t len = recieve();

			if (len == 0)
				throw std::runtime_error("call failed");

			return std::string{ recv_data_.begin(), recv_data_.begin() + len };
		}

		size_t recieve()
		{
			boost::system::error_code ec;
			boost::asio::read(socket_, boost::asio::buffer(head_), ec);
			if (ec)
			{
				//log
				std::cout << ec.message() << std::endl;
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

		boost::asio::io_service ios_;
		boost::asio::ip::tcp::socket socket_;		

		std::array<char, HEAD_LEN>		head_;
		std::array<char, MAX_BUF_LEN>	recv_data_;
	};
}}

void test_msgpack_tuple()
{
	std::tuple<std::array<int, 4>, std::string, std::vector<uint8_t>> tp;
	std::vector<uint8_t> v;
	int const num = 30000000L;
	v.reserve(num);

	for (int i = 0; i < num; ++i) v.push_back(i);

	std::array<int, 4> arr = { 1,2,3,4 };
	tp = std::make_tuple(arr, "it is a test", std::move(v));

	boost::timer t;
	std::stringstream buffer;
	msgpack::pack(buffer, tp);
	std::cout << t.elapsed() << std::endl;

	buffer.seekg(0);
	std::tuple<std::array<int, 4>, std::string, std::vector<uint8_t>> tp2;
	std::get<2>(tp2).reserve(num);
	t.restart();
	msgpack::object_handle oh;
	msgpack::unpack(oh, buffer.str().data(), buffer.str().size());
	oh.get().convert(tp2);
	std::cout << t.elapsed() << std::endl;
}

void test_binary()
{
	const char* data = "hello";
	msgpack::type::raw_ref r = { data, 6 };

	std::vector<char> v(data, data + 6);

	msgpack::sbuffer sbuf;
	msgpack::pack(sbuf, v);
	msgpack::pack(sbuf, r);

	msgpack::unpacked msg;
	msgpack::unpack(&msg, sbuf.data(), sbuf.size());
	msgpack::object o = msg.get();
	msgpack::type::raw_ref r1;
	o.convert(r1);

	std::cout << r1.ptr << std::endl;
}