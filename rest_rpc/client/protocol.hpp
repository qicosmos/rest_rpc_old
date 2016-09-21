#pragma once

#include <boost/type_traits.hpp>

#define TIMAX_DEFINE_PROTOCOL(handler, func_type) static const ::timax::rpc::protocol_define<func_type> handler{ #handler }
#define TIMAX_DEFINE_SUB_PROTOCOL(handler, type) static const ::timax::rpc::sub_protocol<type> handler{ #handler }

namespace timax { namespace rpc
{
	template <typename Func, typename ... Args>
	struct is_argument_match
	{
	private:
		template <typename T>
		static std::false_type test(...);

		template <typename T, typename =
			decltype(std::declval<T>()(std::declval<Args>()...))>
		static std::true_type test(int);

		using result_type = decltype(test<Func>(0));
	public:
		static constexpr bool value = result_type::value;
	};

	template <typename Func>
	struct protocol_define;

	template <typename Func>
	struct protocol_define_base;

	template <typename Ret, typename ... Args>
	struct protocol_define_base<Ret(Args...)>
	{
		using result_type = typename boost::function_traits<Ret(Args...)>::result_type;
		using signature_type = Ret(Args...);

		explicit protocol_define_base(std::string name)
			: name_(std::move(name))
		{
		}

		std::string const& name() const noexcept
		{
			return name_;
		}

		template <typename Marshal, typename ... TArgs>
		auto pack_args(Marshal const& m, TArgs&& ... args) const
		{
			static_assert(is_argument_match<signature_type, TArgs...>::value, "Arguments` types don`t match the protocol!");
			return m.pack_args(std::move(static_cast<Args>(std::forward<TArgs>(args)))...);
		}

	private:
		std::string name_;
	};

	template <typename Ret, typename ... Args>
	struct protocol_define<Ret(Args...)> : protocol_define_base<Ret(Args...)>
	{
		using base_type = protocol_define_base<Ret(Args...)>;
		using result_type = typename base_type::result_type;
		using signature_type = typename base_type::signature_type;

		explicit protocol_define(std::string name)
			: base_type(std::move(name))
		{
		}

		template <typename Marshal, typename = std::enable_if_t<!std::is_void<result_type>::value>>
		auto pack_result(Marshal const& m, result_type&& ret) const
		{
			return m.pack(std::forward<result_type>(ret));
		}

		template <typename Marshal, typename = std::enable_if_t<!std::is_void<result_type>::value>>
		result_type unpack(Marshal& m, char const* data, size_t length) const
		{
			return m.template unpack<result_type>(data, length);
		}
	};

	template <typename ... Args>
	struct protocol_define<void(Args...)> : protocol_define_base<void(Args...)>
	{
		using base_type = protocol_define_base<void(Args...)>;
		using result_type = typename base_type::result_type;
		using signature_type = typename base_type::signature_type;

		explicit protocol_define(std::string name)
			: base_type(std::move(name))
		{
		}
	};

	template <typename T>
	struct sub_protocol
	{
		using result_type = T;
		
		explicit sub_protocol(std::string topic_name)
			: topic_(std::move(topic_name))
		{}

		template <typename CodecPolicy>
		auto pack(CodecPolicy& cp) const
		{
			return cp.pack(topic_);
		}

		template <typename CodecPolicy>
		result_type unpack(CodecPolicy& cp, char const* data, size_t size) const
		{
			return cp.template unpack<result_type>(data, size);
		}

		auto const& name() const noexcept
		{
			return topic_;
		}

	private:
		std::string topic_;
	};

	template <>
	struct sub_protocol<void>;

	TIMAX_DEFINE_PROTOCOL(sub_topic, std::string(std::string const&));
} }
