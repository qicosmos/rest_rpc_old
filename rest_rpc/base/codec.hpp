#pragma once
#include <msgpack.hpp>
#include "../../Kapok/kapok/Kapok.hpp"
namespace timax
{
	namespace rpc 
	{
		using blob = msgpack::type::raw_ref;

		struct msgpack_decode
		{
			template<typename T>
			T unpack(blob bl)
			{
				msgpack::unpack(&msg_, bl.ptr, bl.size);
				return msg_.get().as<T>();
			}

		private:
			msgpack::unpacked msg_;
		};

		struct kapok_decode
		{
			template<typename T>
			T unpack(blob bl)
			{
				DeSerializer dr;
				dr.Parse(bl.ptr, bl.size);

				T t;
				dr.Deserialize(t);
				return t;
			}
		};
	}
}

