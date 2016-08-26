#pragma once
#include <msgpack.hpp>

namespace timax { namespace rpc 
{
	struct blob
	{
		char const* ptr;
		size_t size;
	};
} }

namespace timax { namespace rpc
{
	struct msgpack_decode
	{
		using buffer_type = msgpack::sbuffer;

		template <typename ... Args>
		buffer_type pack_args(Args&& ... args) const
		{
			buffer_type buffer;
			auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
			msgpack::pack(buffer, args_tuple);
			return buffer;
		}

		template <typename T>
		buffer_type pack(T&& t) const
		{
			buffer_type buffer;
			msgpack::pack(buffer, std::forward<T>(t));
			return buffer;
		}

		template <typename T>
		T unpack(char const* data, size_t length)
		{
			msgpack::unpack(&msg_, data, length);
			return msg_.get().as<T>();
		}

	private:
		msgpack::unpacked msg_;
	};
} }

