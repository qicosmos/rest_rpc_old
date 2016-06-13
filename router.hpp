#pragma once
#include <utility>
#include <map>
#include <string>
#include <mutex>
#include <memory>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include "token_parser.hpp"
#include "function_traits.hpp"
#include "common.h"
#include "utils.hpp"

class connection;

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

	template<typename Function>
	void register_binary_handler(std::string const & name, const Function& f)
	{
		this->map_binary_[name] = f;
	}

	template<typename Function, typename Self>
	void register_binary_handler(std::string const & name, const Function& f, Self* self)
	{
		this->map_binary_[name] = [f, self](const char* data, size_t len, std::string& result) { (*self.*f)(data, len, result); };
	}

	void remove_handler(std::string const& name) 
	{
		this->map_invokers_.erase(name);
		this->map_binary_.erase(name);
	}

	void set_callback(const std::function<void(const std::string&, const char*, std::shared_ptr<connection>, bool)>& callback)
	{
		callback_to_server_ = callback;
	}

	template<typename T>
	void route(const char* text, std::size_t length, T conn, bool round_trip)
	{
		token_parser parser;
		parser.parse(text, length);

		while (!parser.empty())
		{
			std::string result = "";
			std::string func_name = parser.get<std::string>();

			auto it = map_invokers_.find(func_name);
			if (it == map_invokers_.end())
			{
				result = get_json(result_code::ARGUMENT_EXCEPTION, "unknown function: " + func_name);
				callback_to_server_(func_name, result.c_str(), conn, true); //has error
				return;
			}

			if (it->second.param_size() != parser.param_size()) //参数个数不匹配 
			{
				result = get_json(result_code::ARGUMENT_EXCEPTION, std::string("parameter number is not match"));
				callback_to_server_(func_name, result.c_str(), conn, true); //has error
				break;
			}

			//找到的function中，开始将字符串转换为函数实参并调用 
			it->second(parser, result);
			//response(result.c_str()); //callback to connection
			if(callback_to_server_)
				callback_to_server_(func_name, result.c_str(), conn, false);
		}
	}

	template<typename T>
	void route_binary(const char* data, std::size_t length, T conn, bool round_trip)
	{
		std::string result = "";
		std::string func_name = data;
		
		auto it = map_binary_.find(func_name);
		if (it == map_binary_.end())
		{
			result = get_json(result_code::ARGUMENT_EXCEPTION, "unknown function: " + func_name);
			callback_to_server_(func_name, result.c_str(), conn, true);
			return;
		}

		const int offset = func_name.length() + 1;
		it->second(data + offset, length - offset, result);
		result = get_json(result_code::OK, result);
		callback_to_server_(func_name, result.c_str(), conn, false);
	}

private:
	router() = default;
	router(const router&) = delete;
	router(router&&) = delete;

	template<typename F, size_t... I, typename ... Args>
	static auto call_helper(const F& f, const std::index_sequence<I...>&, const std::tuple<Args...>& tup)
	{
		return f(std::get<I>(tup)...);
	}

	template<typename F, typename ... Args>
	static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type call(const F& f, std::string& result, const std::tuple<Args...>& tp)
	{
		call_helper(f, std::make_index_sequence<sizeof... (Args)>{}, tp);
		result = get_json(result_code::OK, 0);
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
		call_member(const F& f, Self* self, std::string& result, const std::tuple<Args...>& tp)
	{
		call_member_helper(f, self, typename std::make_index_sequence<sizeof... (Args)>{}, tp);
		result = get_json(result_code::OK, 0);
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
			catch (std::invalid_argument& e)
			{
				result = get_json(result_code::ARGUMENT_EXCEPTION, e.what());
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
			catch (std::invalid_argument& e)
			{
				result = get_json(result_code::ARGUMENT_EXCEPTION, e.what());
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
	std::function<void(const std::string&, const char*, std::shared_ptr<connection>, bool)> callback_to_server_;
	std::map<std::string, std::function<void(const char*, size_t, std::string& result)>> map_binary_;
};

