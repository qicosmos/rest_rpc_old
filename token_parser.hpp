#pragma once
#include <kapok/Kapok.hpp>
class token_parser
{
public:
	static token_parser& get()
	{
		static token_parser instance;
		return instance;
	}

	void parse(const char* s)
	{
		v_.clear();
		dr_.Parse(s);
		Document& doc = dr_.GetDocument();
		auto it = doc.MemberBegin();
		v_.push_back(it->name.GetString());

		StringBuffer buf;
		rapidjson::Writer<StringBuffer> wr(buf);
		if (it->value.IsArray())
		{
			for (size_t i = 0; i < it->value.Size(); i++)
			{
				put_str(it->value[i], wr, buf);
			}
		}
		else
		{
			put_str(it->value, wr, buf);
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

	std::size_t param_size()
	{
		return v_.size();
	}

private:
	token_parser() = default;
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
		val.Accept(wr);
		const char* js = buf.GetString();
		const int len = strlen(js);
		if (len == 0)
			return;

		if (len == 2 && js[0] == '"')
			return;

		v_.push_back(js);

		wr.Reset(buf);
		buf.Clear();
	}

	DeSerializer dr_;
	std::vector<std::string> v_;
};

