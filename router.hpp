#pragma once
#include <utility>
#include <map>
#include <string>
#include <mutex>
#include <boost/lexical_cast.hpp>
#include "token_parser.hpp"
#include "function_traits.hpp"
#include "noncopyable.h"

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

class router : noncopyable
{
	std::map<std::string, invoker_function> map_invokers_;
	std::mutex mtx_;

public:
	static router& get()
	{
		static router instance;
		return instance;
	}

	template<typename Function>
	void register_handler(std::string const & name, const Function& f) {
		return register_nonmember_func(name, f);
	}

	template<typename Function, typename Self>
	void register_handler(std::string const & name, const Function& f, Self* self) {
		return register_member_func(name, f, self);
	}

	void remove_handler(std::string const& name) {
		this->map_invokers_.erase(name);
	}

	void route(const char* text)
	{
		token_parser& parser = token_parser::get();
		std::unique_lock<std::mutex> unique_lock(mtx_);
		parser.parse(text);

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
	router() = default;
	router(const router&) = delete;
	router(router&&) = delete;

	//将注册的handler保存在map中
	template<typename Function>
	void register_nonmember_func(std::string const & name, const Function& f)
	{
		this->map_invokers_[name] = { std::bind(&detail::invoker<Function>::template apply<std::tuple<>>, f,
			std::placeholders::_1, std::tuple<>()), function_traits<Function>::arity };
	}

	template<typename Function, typename Self>
	void register_member_func(const std::string& name, const Function& f, Self* self)
	{
		this->map_invokers_[name] = { std::bind(&detail::invoker<Function>::template apply_member<std::tuple<>, Self>, f, self, std::placeholders::_1,
			std::tuple<>()), function_traits<Function>::arity };
	}
};

namespace detail
{
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
				invoker<Function, N + 1, M>::apply(func, parser,
					std::tuple_cat(args, std::make_tuple(parser.get<arg_type>())));
			}
			catch (std::exception& e)
			{
				std::cout << e.what() << std::endl;
			}
		}

		template<typename Args, typename Self>
		static inline void apply_member(Function func, Self* self, token_parser & parser, const Args& args)
		{
			typedef typename function_traits<Function>::template args<N>::type arg_type;

			return invoker<Function, N + 1, M>::apply_member(func, self, parser, std::tuple_cat(args, std::make_tuple(parser.get<arg_type>())));
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

		template<typename Args, typename Self>
		static inline void apply_member(const Function& func, Self* self, token_parser &parser, const Args& args)
		{
			call_member(func, self, args);
		}
	};

	template<int...>
	struct index_sequence {};

	template<int N, int... Indexes>
	struct make_index_sequence : make_index_sequence<N - 1, N - 1, Indexes...> {};

	template<int... indexes>
	struct make_index_sequence<0, indexes...>
	{
		typedef index_sequence<indexes...> type;
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
	static void call_helper(F f, index_sequence<Indexes...>, const std::tuple<Args...>& tup)
	{
		f(std::get<Indexes>(tup)...);
	}

	template<typename F, typename ... Args>
	static void call(F f, const std::tuple<Args...>& tp)
	{
		call_helper(f, typename make_index_sequence<sizeof... (Args)>::type(), tp);
	}

	template<typename F, typename Self, int ... Indexes, typename ... Args>
	static void call_member_helper(const F& f, Self* self, index_sequence<Indexes...>, const std::tuple<Args...>& tup)
	{
		(*self.*f)(std::get<Indexes>(tup)...);
	}

	template<typename F, typename Self, typename ... Args>
	static void call_member(const F& f, Self* self, const std::tuple<Args...>& tp)
	{
		call_member_helper(f, self, typename make_index_sequence<sizeof... (Args)>::type(), tp);
	}
}
