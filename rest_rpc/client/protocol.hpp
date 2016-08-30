#pragma once

#include <boost/type_traits.hpp>

#define TIMAX_DEFINE_PROTOCOL(handler, func_type) static const ::timax::rpc::protocol_define<func_type> handler{ #handler }
#define TIMAX_DEFINE_PUB_PROTOCOL(handler, func_type) static const ::timax::rpc::pub_protocol_define<func_type> handler{ #handler }
#define TIMAX_DEFINE_SUB_PROTOCOL(handler, func_type) static const ::timax::rpc::sub_protocol_define<func_type> handler{ #handler }

namespace timax { namespace rpc
{
	template <typename Func>
	struct protocol_define;

	template <typename Func>
	struct protocol_define_base;

	template <typename Ret, typename ... Args>
	struct protocol_define_base<Ret(Args...)>
	{
		using result_type = typename boost::function_traits<Ret(Args...)>::result_type;

		explicit protocol_define_base(std::string name)
			: name_(std::move(name))
		{
		}

		std::string const& name() const noexcept
		{
			return name_;
		}

		framework_type get_type() const noexcept
		{
			return framework_type::DEFAULT;
		}

		template <typename Marshal, typename ... Args_>
		auto pack_args(Marshal const& m, Args_&& ... args) const
		{
			return m.pack_args(std::forward<Args_>(args)...);
		}

	private:
		std::string name_;
	};

	template <typename Ret, typename ... Args>
	struct protocol_define<Ret(Args...)> : protocol_define_base<Ret(Args...)>
	{
		using base_type = protocol_define_base<Ret(Args...)>;
		using result_type = typename base_type::result_type;

		explicit protocol_define(std::string name)
			: base_type(std::move(name))
		{
		}

		template <typename Marshal, typename = std::enable_if_t<!std::is_void<result_type>::value>>
		auto pack_result(Marshal const& m, result_type&& ret) const
		{
			return m.pack(std::forward<Ret>(ret));
		}

		template <typename Marshal, typename = std::enable_if_t<!std::is_void<result_type>::value>>
		result_type unpack(Marshal& m, char const* data, size_t length) const
		{
			return m.unpack<result_type>(data, length);
		}
	};

	template <typename ... Args>
	struct protocol_define<void(Args...)> : protocol_define_base<void(Args...)>
	{
		using base_type = protocol_define_base<void(Args...)>;

		explicit protocol_define(std::string name)
			: base_type(std::move(name))
		{
		}
	};

	template <typename Func>
	struct sub_protocol_define : protocol_define<Func>
	{
		explicit sub_protocol_define(std::string name)
			: protocol_define<Func>(std::move(name))
		{}

		framework_type get_type() const noexcept
		{
			return framework_type::SUB;
		}
	};

	template <typename Func>
	struct pub_protocol_define : protocol_define<Func>
	{
		explicit pub_protocol_define(std::string name)
			: protocol_define<Func>(std::move(name))
		{}

		framework_type get_type() const noexcept
		{
			return framework_type::PUB;
		}
	};

	//template <typename Ret, typename ... Args, typename Tag, typename TagPolicy>
	//struct protocol_with_tag<Ret(Args...), Tag, TagPolicy>
	//{
	//	using protocol_basic_t = protocol_define<Ret(Args...)>;
	//	using result_type = typename protocol_basic_t::result_type;
	//	using tag_t = Tag;
	//
	//	protocol_with_tag(protocol_basic_t const& protocol, tag_t tag)
	//		: tag_(std::move(tag))
	//		, protocol_(protocol)
	//	{
	//
	//	}
	//
	//	std::string make_json(Args&& ... args) const
	//	{
	//		Serializer sr;
	//		sr.Serialize(std::make_tuple(tag_, std::forward<Args>(args)...), protocol_.name().c_str());
	//		return sr.GetString();
	//	}
	//
	//	result_type parse_json(std::string const& json_str) const
	//	{
	//		DeSerializer dr;
	//		dr.Parse(json_str);
	//		auto& document = dr.GetDocument();
	//		if (static_cast<int>(result_code::OK) == document[CODE].GetInt())
	//		{
	//			response_msg<result_type, tag_t> response;
	//			dr.Deserialize(response);
	//			if (TagPolicy{}(tag_, response.tag))
	//			{
	//				return response.result;
	//			}
	//			throw std::invalid_argument("json result is not valid");
	//		}
	//		else
	//		{
	//			throw logic_error("request faild");
	//		}
	//	}
	//
	//	framework_type get_type() const noexcept
	//	{
	//		return framework_type::ROUNDTRIP;
	//	}
	//
	//private:
	//	tag_t tag_;
	//	protocol_basic_t const& protocol_;
	//};
	//
	//template <typename Func, typename Tag, typename TagPolicy = std::equal_to<std::decay_t<Tag>>>
	//auto with_tag(protocol_define<Func> const& protocol, Tag&& tag, TagPolicy = TagPolicy{})
	//{
	//	using tag_t = std::remove_reference_t<std::remove_cv_t<Tag>>;
	//	using protoco_with_tag_t = protocol_with_tag<Func, tag_t, std::equal_to<tag_t>>;
	//	return protoco_with_tag_t{ protocol, std::forward<Tag>(tag) };
	//}

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

	TIMAX_DEFINE_PROTOCOL(sub_topic, std::string(std::string const&));
} }
