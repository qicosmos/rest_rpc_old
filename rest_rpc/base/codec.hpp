#pragma once
#include <msgpack.hpp>
#include <kapok\Kapok.hpp>
namespace timax
{
	namespace rpc 
	{
		struct blob
		{
			const char* data;
			size_t size;
		};

		struct msgpack_decode
		{
			template<typename T>
			T unpack(blob bl)
			{
				msgpack::unpacked msg;
				msgpack::unpack(&msg, bl.data, bl.size);
				return msg.get().as<T>();
			}
		};

		struct kapok_decode
		{
			template<typename T>
			T unpack(blob bl)
			{
				DeSerializer dr;
				dr.Parse(bl.data, bl.size);

				T t;
				dr.Deserialize(t);
				return t;
			}
		};
	}
}

