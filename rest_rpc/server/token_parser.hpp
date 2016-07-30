#pragma once

namespace timax { namespace rpc 
{
	class token_parser
	{
	public:
		static std::string const tag_connection;

		void parse(const char* s, std::size_t length, bool round_trip = false)
		{
			v_.clear();
			tag_.clear();		// the allocated memory will not be released
			dr_.Parse(s, length);
			Document& doc = dr_.GetDocument();
			auto it = doc.MemberBegin();
			v_.push_back(it->name.GetString());

			StringBuffer buf;
			rapidjson::Writer<StringBuffer> wr(buf);

			if (round_trip)
			{
				// has tag
				assert(it->value.IsArray() && it->value.Size() > 1);
				tag_ = tag_connection + as_str(it->value[0]);
				for (rapidjson::SizeType i = 1; i < it->value.Size(); i++)
				{
					put_str(it->value[i], wr, buf);
				}
			}
			else
			{
				if (it->value.IsArray())
				{
					for (rapidjson::SizeType i = 0; i < it->value.Size(); i++)
					{
						put_str(it->value[i], wr, buf);
					}
				}
				else
				{
					put_str(it->value, wr, buf);
				}
			}
		}

		template<typename RequestedType>
		typename std::decay<RequestedType>::type get()
		{
			if (v_.empty())
				throw std::invalid_argument("unexpected end of input");

			try
			{
				typedef typename std::decay<RequestedType>::type result_type;

				auto it = v_.begin();
				result_type result = lexical_cast<typename std::decay<result_type>::type>(*it);
				v_.erase(it);
				return result;
			}
			catch (std::exception& e)
			{
				v_.clear();
				throw std::invalid_argument(std::string("invalid argument: ") + e.what());
			}
		}

		bool empty() const { return v_.empty(); }

		std::size_t param_size() const
		{
			return v_.size();
		}

		std::string const& tag() const
		{
			return tag_;
		}

		token_parser() = default;
	private:

		token_parser(const token_parser&) = delete;
		token_parser(token_parser&&) = delete;

		template<typename T>
		typename std::enable_if<is_basic_type<T>::value, T>::type lexical_cast(const std::string& str)
		{
			return boost::lexical_cast<T>(str);
		}

		template<typename T>
		typename std::enable_if<!is_basic_type<T>::value, T>::type lexical_cast(const std::string& str)
		{
			dr_.Parse(str);
			T t;
			dr_.Deserialize<T>(t, false);
			return t;
		}

		void put_str(Value& val, rapidjson::Writer<StringBuffer>& wr, StringBuffer& buf)
		{
			if (val.IsString())
			{
				v_.push_back(val.GetString());
				return;
			}

			val.Accept(wr);
			const char* js = buf.GetString();
			size_t len = strlen(js);
			if (len == 0)
				return;

			if (len == 2 && js[0] == '"')
				return;

			v_.push_back(js);

			wr.Reset(buf);
			buf.Clear();
		}

		std::string as_str(Value const& val)
		{
			if (val.IsString())
			{
				return val.GetString();
			}

			StringBuffer buf;
			rapidjson::Writer<StringBuffer> wr(buf);
			val.Accept(wr);
			const char* js = buf.GetString();
			size_t len = strlen(js);
			if (len == 0)
				return "";
			if (len == 2 && js[0] == '"')
				return "";

			return{ js };
		}

		DeSerializer dr_;
		std::vector<std::string> v_;
		std::string tag_;
	};

	std::string const token_parser::tag_connection = ",\"tag\":";
} }

