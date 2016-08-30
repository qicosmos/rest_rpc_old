#pragma once
#include <boost/archive/text_oarchive.hpp> 
#include <boost/archive/text_iarchive.hpp> 
#include <sstream> 

namespace boost {
	namespace serialization {

		/**
		* serialization for tuples
		*/
		template<typename Archive, size_t... I, typename... Args>
		void serialize(Archive & ar, const std::index_sequence<I...>&, std::tuple<Args...> & t, unsigned int version)
		{
			bool arr[] = { (ar & std::get<I>(t), false)... };
			(void*)arr;
		}

		template<typename Archive, typename... Args>
		void serialize(Archive & ar, std::tuple<Args...> & t, unsigned int version)
		{
			serialize(ar, std::make_index_sequence<sizeof... (Args)>{}, t, version);
		}

	} // end serialization namespace
} // end boost namespace

namespace timax {
	namespace rpc
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

		struct boost_codec
		{
			template<typename T>
			T unpack(char const* data, size_t length)
			{
				std::stringstream ss;
				ss.write(data, length);
				boost::archive::text_iarchive ia(ss);
				T t;
				ia >> t;
				return t;
			}

			using buffer_type = std::vector<char>;

			template <typename ... Args>
			buffer_type pack_args(Args&& ... args) const
			{
				auto args_tuple = std::make_tuple(std::forward<Args>(args)...);
				std::stringstream ss;
				boost::archive::text_oarchive oa(ss);
				oa << args_tuple;

				return assign(ss);
			}

			template <typename T>
			buffer_type pack(T&& t)
			{
				std::stringstream ss;
				boost::archive::text_oarchive oa(ss);
				oa << std::forward<T>(t);

				return assign(ss);
			}

			vector<char> assign(std::stringstream& ss) const
			{
				vector<char> vec;
				std::streampos beg = ss.tellg();
				ss.seekg(0, std::ios_base::end);
				std::streampos end = ss.tellg();
				ss.seekg(0, std::ios_base::beg);
				vec.reserve(end - beg);

				vec.assign(std::istreambuf_iterator<char>(ss), std::istreambuf_iterator<char>());
				return vec;
			}
		};
	}
}
