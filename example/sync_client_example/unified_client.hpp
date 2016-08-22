#pragma once
#include <msgpack.hpp>
#include <boost/timer.hpp>

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