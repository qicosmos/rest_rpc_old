#pragma once

namespace timax { namespace rpc 
{
	template <typename CodecPolicy>
	class router : boost::noncopyable
	{
	public:
		using codec_policy = CodecPolicy;
		using message_t = typename codec_policy::buffer_type;
		using connection_ptr = std::shared_ptr<connection>;
		using invoker_t = std::function<void(connection_ptr, char const*, size_t)>;
		using invoker_map_t = std::map<std::string, invoker_t>;

	public:
		template <typename Handler, typename PostFunc>
		bool register_invoker(std::string const& name, Handler&& handler, PostFunc&& post_func)
		{
			using is_void_t = std::is_void<typename function_traits<Handler>::result_type>;

			if (invokers_.find(name) != invokers_.end())
				return false;

			auto invoker = get_invoker(std::forward<Handler>(handler), std::forward<PostFunc>(post_func), is_void_t{});
			invokers_.emplace(name, std::move(invoker));
			return true;
		}

		template <typename Handler>
		bool register_invoker(std::string const& name, Handler&& handler)
		{
			using is_void_t = std::is_void<typename function_traits<Handler>::result_type>;

			if (invokers_.find(name) != invokers_.end())
				return false;

			auto invoker = get_invoker(std::forward<Handler>(handler), is_void_t{});
			invokers_.emplace(name, std::move(invoker));
			return true;
		}

		template <typename Handler, typename PostFunc>
		bool async_register_invoker(std::string const& name, Handler&& handler, PostFunc&& post_func)
		{
			using is_void_t = std::is_void<typename function_traits<Handler>::result_type>;

			if (invokers_.find(name) != invokers_.end())
				return false;

			auto invoker = get_async_invoker(std::forward<Handler>(handler), std::forward<PostFunc>(post_func), is_void_t{});
			invokers_.emplace(name, std::move(invoker));
			return true;
		}

		template <typename Handler>
		bool async_register_invoker(std::string const& name, Handler&& handler)
		{
			using is_void_t = std::is_void<typename function_traits<Handler>::result_type>;

			if (invokers_.find(name) != invokers_.end())
				return false;

			auto invoker = get_async_invoker(std::forward<Handler>(handler), is_void_t{});
			invokers_.emplace(name, std::move(invoker));
			return true;
		}

		bool has_invoker(std::string const& name) const
		{
			return invokers_.find(name) != invokers_.end();
		}

		void apply_invoker(std::string const& name, connection_ptr conn, char const* data, size_t size) const
		{
			static auto cannot_find_invoker_error = codec_policy{}.pack(exception{ error_code::FAIL, "Cannot find handler!" });

			auto itr = invokers_.find(name);
			if (invokers_.end() == itr)
			{
				auto ctx = context_t::make_error_message(conn->head_, cannot_find_invoker_error);
				conn->response(ctx);
			}
			else
			{
				auto& invoker = itr->second;
				if (!invoker)
				{
					auto ctx = context_t::make_error_message(conn->head_, cannot_find_invoker_error);
					conn->response(ctx);
				}

				try
				{
					invoker(conn, data, size);
				}
				catch (exception const& error)
				{
					// response serialized exception to client
					auto args_not_match_error = codec_policy{}.pack(error);
					auto args_not_match_error_message = context_t::make_error_message(conn->head_,
						std::move(args_not_match_error));
					conn->response(args_not_match_error_message);
				}
			}
		}

	private:
		template <typename Func, typename ArgsTuple, size_t ... Is>
		static auto call_helper_impl(Func&& f, ArgsTuple&& args_tuple, std::false_type, std::index_sequence<Is...>)
		{
			return f(std::forward<std::tuple_element_t<Is, std::remove_reference_t<ArgsTuple>>>(std::get<Is>(args_tuple))...);
		}

		template <typename Func, typename ArgsTuple, size_t ... Is>
		static auto call_helper_impl(Func&& f, ArgsTuple&& args_tuple, std::true_type, std::index_sequence<Is...>)
		{
			f(std::forward<std::tuple_element_t<Is, std::remove_reference_t<ArgsTuple>>>(std::get<Is>(args_tuple))...);
		}

		template <typename Func, typename ArgsTuple, typename IsVoid>
		static auto call_helper(Func&& f, ArgsTuple&& args_tuple, IsVoid is_void)
		{
			using indices_type = std::make_index_sequence<std::tuple_size<std::remove_reference_t<ArgsTuple>>::value>;
			return call_helper_impl(std::forward<Func>(f), std::forward<ArgsTuple>(args_tuple), is_void, indices_type{});
		}

		template <typename Handler, typename PostFunc>
		static invoker_t get_invoker(Handler&& handler, PostFunc&& post_func, std::true_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			
			// void return type
			invoker_t invoker = [f = std::move(handler), post = std::move(post_func)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);
				call_helper(f, args_tuple, std::true_type{});
				auto ctx = context_t::make_message(conn->head_, context_t::message_t{}, [conn, &post] { post(conn); });
				conn->response(ctx);
			};
			
			return invoker;
		}

		template <typename Handler, typename PostFunc>
		static invoker_t get_invoker(Handler&& handler, PostFunc&& post_func, std::false_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler), post = std::move(post_func)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);
				auto result = call_helper(f, args_tuple, std::false_type{});
				auto message = cp.pack(result);
				auto ctx = context_t::make_message(conn->head_, std::move(message),
					[conn, &post, r = std::move(result)]
				{
					post(conn, r);
				});
				conn->response(ctx);
			};
			
			return invoker;
		}

		template <typename Handler>
		static invoker_t get_invoker(Handler&& handler, std::true_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);
				call_helper(f, args_tuple, std::true_type{});
				auto ctx = context_t::make_message(conn->head_, context_t::message_t{});
				conn->response(ctx);
			};

			return invoker;
		}

		template <typename Handler>
		static invoker_t get_invoker(Handler&& handler, std::false_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);
				auto result = call_helper(f, args_tuple, std::false_type{});
				auto message = cp.pack(result);
				auto ctx = context_t::make_message(conn->head_, std::move(message));
				conn->response(ctx);
			};

			return invoker;
		}

		template <typename Handler, typename PostFunc>
		static invoker_t get_async_invoker(Handler&& handler, PostFunc&& post_func, std::true_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler), post = std::move(post_func)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);
			
				std::async([conn, args = std::move(args_tuple), h = conn->head_, &f, &post]
				{
					call_helper(f, args, std::true_type{});
					auto ctx = context_t::make_message(h, context_t::message_t{}, [conn, &post] { post(conn); });
					conn->response(ctx);
				});
			};

			return invoker;
		}

		template <typename Handler, typename PostFunc>
		static invoker_t get_async_invoker(Handler&& handler, PostFunc&& post_func, std::false_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler), post = std::move(post_func)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);

				std::async([conn, args = std::move(args_tuple), h = conn->head_, &f, &post]
				{
					auto result = call_helper(f, args, std::false_type{});
					auto message = codec_policy{}.pack(result);
					auto ctx = context_t::make_message(h, std::move(message),
						[conn, &post, r = std::move(result)]
					{
						post(conn, r);
					});
					conn->response(ctx);
				});
			};

			return invoker;
		}

		template <typename Handler>
		static invoker_t get_async_invoker(Handler&& handler, std::true_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);

				std::async([conn, args = std::move(args_tuple), h = conn->head_, &f]
				{
					call_helper(f, args, std::false_type{});
					auto ctx = context_t::make_message(h, context_t::message_t{});
					conn->response(ctx);
				});
			};

			return invoker;
		}

		template <typename Handler>
		static invoker_t get_async_invoker(Handler&& handler, std::false_type)
		{
			using args_tuple_t = typename function_traits<Handler>::tuple_type;
			invoker_t invoker = [f = std::move(handler)]
				(connection_ptr conn, char const* data, size_t size)
			{
				codec_policy cp{};
				auto args_tuple = cp.template unpack<args_tuple_t>(data, size);

				std::async([conn, args = std::move(args_tuple), h = conn->head_, &f]
				{
					auto result = call_helper(f, args, std::false_type{});
					auto message = codec_policy{}.pack(result);
					auto ctx = context_t::make_message(h, std::move(message));
					conn->response(ctx);
				});
			};

			return invoker;
		}

	private:
		// mutable std::mutex		mutex_;
		invoker_map_t				invokers_;
	};
} }