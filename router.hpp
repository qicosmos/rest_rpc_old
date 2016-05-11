#pragma once
#include <utility>
#include <map>
#include <string>
#include <mutex>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include "token_parser.hpp"
#include "function_traits.hpp"
#include "common.h"

class invoker_function
{
public:
	invoker_function() = default;
	invoker_function(const std::function<void(token_parser &, std::string&)>& function, std::size_t size) : function_(function), param_size_(size)
	{

	}

	void operator()(token_parser &parser, std::string& result)
	{
		function_(parser, result);
	}

	const std::size_t param_size() const
	{
		return param_size_;
	}

private:
	std::function<void(token_parser &, std::string& result)> function_;
	std::size_t param_size_;
};

namespace detail
{
	//template<int...>
	//struct index_sequence {};

	//template<int N, int... Indexes>
	//struct make_index_sequence : make_index_sequence<N - 1, N - 1, Indexes...> {};

	//template<int... indexes>
	//struct make_index_sequence<0, indexes...>
	//{
	//	typedef index_sequence<indexes...> type;
	//};
	
	//C++14的实现
	
}

class router : boost::noncopyable
{
public:
	static router& get()
	{
		static router instance;
		return instance;
	}

	template<typename Function>
	void register_handler(std::string const & name, const Function& f) 
	{
		return register_nonmember_func(name, f);
	}

	template<typename Function, typename Self>
	void register_handler(std::string const & name, const Function& f, Self* self) 
	{
		return register_member_func(name, f, self);
	}

	void remove_handler(std::string const& name) 
	{
		this->map_invokers_.erase(name);
	}

	void route(const char* text, std::size_t length, const std::function<void(const char*)>& callback = nullptr)
	{
		token_parser& parser = token_parser::get();
		std::unique_lock<std::mutex> unique_lock(mtx_);
		parser.parse(text, length);

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
			std::string result = "";
			it->second(parser, result);
			if (callback != nullptr)
			{
				callback(result.c_str());
			}
		}
	}

private:
	router() = default;
	router(const router&) = delete;
	router(router&&) = delete;

	template<typename T>
	static std::string get_json(result_code code, const T& r)
	{
		response_msg<T> msg = { code, r };

		sr_.Serialize(msg);
		return sr_.GetString();
	}

	template<typename F, size_t... I, typename ... Args>
	static auto call_helper(const F& f, const std::index_sequence<I...>&, const std::tuple<Args...>& tup)
	{
		return f(std::get<I>(tup)...);
	}

	template<typename F, typename ... Args>
	static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type call(const F& f, std::string&, const std::tuple<Args...>& tp)
	{
		call_helper(f, std::make_index_sequence<sizeof... (Args)>{}, tp);
	}

	template<typename F, typename ... Args>
	static typename std::enable_if<!std::is_void<typename std::result_of<F(Args...)>::type>::value>::type call(const F& f, std::string& result, const std::tuple<Args...>& tp)
	{
		auto r = call_helper(f, std::make_index_sequence<sizeof... (Args)>{}, tp);
		result = get_json(result_code::OK, r);
	}

	template<typename F, typename Self, size_t... Indexes, typename ... Args>
	static auto call_member_helper(const F& f, Self* self, const std::index_sequence<Indexes...>&, const std::tuple<Args...>& tup)
	{
		return (*self.*f)(std::get<Indexes>(tup)...);
	}

	template<typename F, typename Self, typename ... Args>
	static typename std::enable_if<std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
		call_member(const F& f, Self* self, std::string&, const std::tuple<Args...>& tp)
	{
		call_member_helper(f, self, typename std::make_index_sequence<sizeof... (Args)>{}, tp);
	}

	template<typename F, typename Self, typename ... Args>
	static typename std::enable_if<!std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>::type
		call_member(const F& f, Self* self, std::string& result, const std::tuple<Args...>& tp)
	{
		auto r = call_member_helper(f, self, typename std::make_index_sequence<sizeof... (Args)>{}, tp);
		result = get_json(result_code::OK, r);
	}

	//template<typename Function, class Signature = Function, size_t N = 0, size_t M = function_traits<Signature>::arity>
	//struct invoker;

	//遍历function的实参类型，将字符串参数转换为实参并添加到tuple中
	template<typename Function, size_t N = 0, size_t M = function_traits<Function>::arity>
	struct invoker
	{
		template<typename Args>
		static inline void apply(const Function& func, token_parser & parser, std::string& result, Args const & args)
		{
			typedef typename function_traits<Function>::template args<N>::type arg_type;
			try
			{
				invoker<Function, N + 1, M>::apply(func, parser, result,
					std::tuple_cat(args, std::make_tuple(parser.get<arg_type>())));
			}
			catch (std::exception& e)
			{
				result = get_json(result_code::EXCEPTION, e.what());
			}
		}

		template<typename Args, typename Self>
		static inline void apply_member(Function func, Self* self, token_parser & parser, std::string& result, const Args& args)
		{
			typedef typename function_traits<Function>::template args<N>::type arg_type;

			try
			{
				invoker<Function, N + 1, M>::apply_member(func, self, parser, result, std::tuple_cat(args, std::make_tuple(parser.get<arg_type>())));
			}
			catch (const std::exception& e)
			{
				result = get_json(result_code::EXCEPTION, e.what());
			}
		}
	};

	template<typename Function, size_t M>
	struct invoker<Function, M, M>
	{
		template<typename Args>
		static inline void apply(const Function& func, token_parser &, std::string& result, Args const & args)
		{
			//参数列表已经准备好，可以调用function了
			call(func, result, args);
		}

		template<typename Args, typename Self>
		static inline void apply_member(const Function& func, Self* self, token_parser &parser, std::string& result, const Args& args)
		{
			call_member(func, self, result, args);
		}
	};

	//将注册的handler保存在map中
	template<typename Function>
	void register_nonmember_func(std::string const & name, const Function& f)
	{
		this->map_invokers_[name] = { std::bind(&invoker<Function>::template apply<std::tuple<>>, f,
			std::placeholders::_1, std::placeholders::_2, std::tuple<>()), function_traits<Function>::arity };
	}

	template<typename Function, typename Self>
	void register_member_func(const std::string& name, const Function& f, Self* self)
	{
		this->map_invokers_[name] = { std::bind(&invoker<Function>::template apply_member<std::tuple<>, Self>, f, self, std::placeholders::_1,
			std::placeholders::_2, std::tuple<>()), function_traits<Function>::arity };
	}

	std::map<std::string, invoker_function> map_invokers_;
	std::mutex mtx_;
	static Serializer sr_;
};
Serializer router::sr_;
