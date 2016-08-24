#pragma once
#include "../base/function_traits.hpp"
#include <msgpack.hpp>

namespace timax { namespace rpc 
{
	class router : boost::noncopyable
	{
	public:
		static router& get()
		{
			static router instance;
			return instance;
		}

		template<typename Function, typename AfterFunction>
		void register_handler(std::string const & name, const Function& f, const AfterFunction& af)
		{
			register_nonmember_func(name, f, af);
		}

		template<typename Function, typename AfterFunction, typename Self>
		void register_handler(const std::string& name, const Function& f, Self* self, const AfterFunction& af)
		{
			register_member_func(name, f, self, af);
		}

		void remove_handler(std::string const& name)
		{
			this->invokers_.erase(name);
		}

		bool has_handler(const std::string func_name)
		{
			return invokers_.find(func_name) != invokers_.end();
		}

		void route(const char* data, size_t size)
		{
			std::string func_name = data;
			auto it = invokers_.find(func_name);
			if (it != invokers_.end())
			{
				msgpack::unpacked msg;
				int length = func_name.length();
				msgpack::unpack(&msg, data + length + 1, size - length - 1);
				
				it->second(msg.get());
			}		
		}

	private:
		router() = default;
		router(const router&) = delete;
		router(router&&) = delete;

		template<typename Function, typename AfterFunction>
		void register_nonmember_func(std::string const & name, const Function& f, const AfterFunction& afterfunc)
		{
			using std::placeholders::_1;

			this->invokers_[name] = { std::bind(&invoker1<Function, AfterFunction>::apply, f, afterfunc, _1) };
		}

		template<typename Function, typename AfterFunction, typename Self>
		void register_member_func(const std::string& name, const Function& f, Self* self, const AfterFunction& afterfunc)
		{
			using std::placeholders::_1;

			this->invokers_[name] = { std::bind(&invoker1<Function, AfterFunction>::template apply_member<Self>, f, self, afterfunc, _1) };
		}

		template<typename Function, typename AfterFunction>
		struct invoker1
		{
			static inline void apply(const Function& func, const AfterFunction& afterfunc, const msgpack::object& o)
			{
				using tuple_type = typename function_traits<Function>::tuple_type;
				tuple_type tp;
				o.convert(tp);

				call(func, afterfunc, tp);
			}

			template<typename F, typename AfterFunction, typename ... Args>
			static typename std::enable_if<std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
				call(const F& f, const AfterFunction& af, const std::tuple<Args...>& tp)
			{
				call_helper(f, std::make_index_sequence<sizeof... (Args)>{}, tp);
				af();
			}

			template<typename F, typename AfterFunction, typename ... Args>
			static typename std::enable_if<!std::is_void<typename std::result_of<F(Args...)>::type>::value>::type
				call(const F& f, const AfterFunction& af, const std::tuple<Args...>& tp)
			{
				auto r = call_helper(f, std::make_index_sequence<sizeof... (Args)>{}, tp);
				af(r);
			}

			template<typename F, size_t... I, typename ... Args>
			static auto call_helper(const F& f, const std::index_sequence<I...>&, const std::tuple<Args...>& tup)
			{
				return f(std::get<I>(tup)...);
			}

			//member function
			template<typename Self>
			static inline void apply_member(const Function& func, Self* self, const AfterFunction& afterfunc, const msgpack::object& o)
			{
				using tuple_type = typename function_traits<Function>::tuple_type;
				tuple_type tp;
				o.convert(tp);

				call_member(func, self, afterfunc, tp);
			}

			template<typename F, typename AfterFunction, typename Self, typename ... Args>
			static inline std::enable_if_t<std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>
				call_member(const F& f, Self* self, const AfterFunction& af, const std::tuple<Args...>& tp)
			{
				call_member_helper(f, self, std::make_index_sequence<sizeof... (Args)>{}, tp);
				af();
			}

			template<typename F, typename AfterFunction, typename Self, typename ... Args>
			static inline std::enable_if_t<!std::is_void<typename std::result_of<F(Self, Args...)>::type>::value>
				call_member(const F& f, Self* self, const AfterFunction& af, const std::tuple<Args...>& tp)
			{
				auto r = call_member_helper(f, self, std::make_index_sequence<sizeof... (Args)>{}, tp);
				af(r);
			}

			template<typename F, typename Self, size_t... Indexes, typename ... Args>
			static auto call_member_helper(const F& f, Self* self, const std::index_sequence<Indexes...>&, const std::tuple<Args...>& tup)
			{
				return (*self.*f)(std::get<Indexes>(tup)...);
			}
		};

		std::map<std::string, std::function<void(const msgpack::object&)>> invokers_;
	};
} }



