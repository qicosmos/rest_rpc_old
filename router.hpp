#pragma once
#include <utility>
#include <map>
#include <string>
#include <boost/lexical_cast.hpp>
#include <kapok/Kapok.hpp>
#include "function_traits.hpp"


class router
{
	class token_parser
	{
	public:

		token_parser(std::string& s, char seperator)
		{
			v_ = split(s, seperator);
		}

		token_parser(const std::string& s)
		{
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

		void put_str(Value& val, rapidjson::Writer<StringBuffer>& wr, StringBuffer& buf)
		{
			val.Accept(wr);
			const char* js = buf.GetString();
			v_.push_back(js);

			wr.Reset(buf);
			buf.Clear();
		}

		std::size_t param_size()
		{
			return v_.size();
		}

	public:
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

	private:
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
			//return{};
		}
		
		static std::vector<std::string> split(std::string& s, char seperator)
		{
			std::vector<std::string> v;
			int pos = 0;
			while (true)
			{
				pos = s.find(seperator, 0);
				if (pos == std::string::npos)
				{
					if (!s.empty())
						v.push_back(s);
					break;
				}
				if (pos != 0)
					v.push_back(s.substr(0, pos));
				s = s.substr(pos + 1, s.length());
			}

			return v;
		}

		DeSerializer dr_;
		std::vector<std::string> v_;
	};

	class invoker_function
	{
	public:
		invoker_function() = default;
		invoker_function(const std::function<void(token_parser &)>& function, std::size_t size) : function_(function), param_size_(size)
		{

		}

		void operator()(token_parser &parser)
		{
			function_(parser);
		}

		const std::size_t param_size() const
		{
			return param_size_;
		}

	private:
		std::function<void(token_parser &)> function_;
		std::size_t param_size_;
	};

	//typedef std::function<void(token_parser &)> invoker_function;

	std::map<std::string, invoker_function> map_invokers_;

public:
	template<typename Function>
	void register_handler(std::string const & name, const Function& f) {
		return register_nonmember_func(name, f);
	}

	void remove_handler(std::string const& name) {
		this->map_invokers_.erase(name);
	}

	void route(std::string  & text) 
	{
		//token_parser parser(text, '/');
		token_parser parser(text);

		while (!parser.empty())
		{
			std::string func_name = parser.get<std::string>();

			auto it = map_invokers_.find(func_name);
			if (it == map_invokers_.end())
				throw std::runtime_error("unknown function: " + func_name);

			if (it->second.param_size() != parser.param_size()) //参数个数不匹配
			{
				break;
			}
				
			//找到的function中，开始将字符串转换为函数实参并调用
			it->second(parser);
		}
	}


private:
	//将注册的handler保存在map中
	template<typename Function>
	void register_nonmember_func(std::string const & name, const Function& f)
	{
		this->map_invokers_[name] = { std::bind(&invoker<Function>::template apply<std::tuple<>>, f,
			std::placeholders::_1, std::tuple<>()), function_traits<Function>::arity };
	}

	//template<typename Function, class Signature = Function, size_t N = 0, size_t M = function_traits<Signature>::arity>
	//struct invoker;

	//遍历function的实参类型，将字符串参数转换为实参并添加到tuple中
	template<typename Function, size_t N = 0, size_t M = function_traits<Function>::arity>
	struct invoker
	{
		template<typename Args>
		static inline void apply(const Function& func, token_parser & parser, Args const & args)
		{
			typedef typename function_traits<Function>::template args<N>::type arg_type;
			try
			{
				router::invoker<Function, N + 1, M>::apply(func, parser,
					std::tuple_cat(args, std::make_tuple(parser.get<arg_type>())));
			}
			catch (std::exception& e)
			{
				std::cout << e.what() << std::endl;
			}
		}
	};

	template<typename Function, size_t M>
	struct invoker<Function, M, M>
	{
		template<typename Args>
		static inline void apply(const Function& func, token_parser &, Args const & args)
		{
			//参数列表已经准备好，可以调用function了
			call(func, args);
		}
	};

	template<int...>
	struct IndexTuple {};

	template<int N, int... Indexes>
	struct MakeIndexes : MakeIndexes<N - 1, N - 1, Indexes...> {};

	template<int... indexes>
	struct MakeIndexes<0, indexes...>
	{
		typedef IndexTuple<indexes...> type;
	};

	//C++14的实现
	//template<typename F, size_t... I, typename ... Args>
	//static void call_helper(F f, std::index_sequence<I...>, const std::tuple<Args...>& tup)
	//{
	//	f(std::get<I>(tup)...);
	//}

	//template<typename F, typename ... Args>
	//static void call(F f, const std::tuple<Args...>& tp)
	//{
	//	call_helper(f, std::make_index_sequence<sizeof... (Args)>(), tp);
	//}

	template<typename F, int ... Indexes, typename ... Args>
	static void call_helper(F f, IndexTuple<Indexes...>, const std::tuple<Args...>& tup)
	{
		f(std::get<Indexes>(tup)...);
	}

	template<typename F, typename ... Args>
	static void call(F f, const std::tuple<Args...>& tp)
	{
		call_helper(f, typename MakeIndexes<sizeof... (Args)>::type(), tp);
	}
};

