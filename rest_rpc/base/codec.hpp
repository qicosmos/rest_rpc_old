#pragma once

namespace timax { namespace rpc
{
	using blob = msgpack::type::raw_ref;

	struct msgpack_codec
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

	struct kapok_codec
	{
		template<typename T>
		T unpack(char const* data, size_t length)
		{
			DeSerializer dr;
			dr.Parse(data, length);

			T t;
			dr.Deserialize(t);
			return t;
		}

		using buffer_type = std::string;

		template <typename ... Args>
		buffer_type pack_args(Args&& ... args) const
		{
			auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
			Serializer sr;			
			sr.Serialize(args_tuple);
			return sr.GetString();
		}

		template <typename T>
		buffer_type pack(T&& t) const
		{
			Serializer sr;
			sr.Serialize(std::forward<T>(t));
			return sr.GetString();
		}
	};
} }
