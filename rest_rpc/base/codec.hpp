#pragma once
#include <msgpack.hpp>

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
	}
}

