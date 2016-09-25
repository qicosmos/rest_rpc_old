#pragma once

namespace timax { namespace rpc 
{
	struct context_t
	{
		using post_func_t = std::function<void()>;
		using message_t = std::vector<char>;

		context_t() = default;

		template <typename Message>
		context_t(head_t const& h, Message&& msg, post_func_t postf)
			: context_t(h, std::move(message_t{ msg.begin(), msg.end() }), std::move(postf))
		{
		}

		context_t(head_t const& h, message_t msg, post_func_t postf)
			: head(h)
			, message(std::move(msg))
			, post_func(std::move(postf))
		{
			head.len = static_cast<uint32_t>(message.size());
		}

		void apply_post_func() const
		{
			if (post_func)
				post_func();
		}

		auto get_message() const
			-> std::vector<boost::asio::const_buffer>
		{
			if (message.empty())
				return{ boost::asio::buffer(&head, sizeof(head_t)) };

			return{ boost::asio::buffer(&head, sizeof(head_t)), boost::asio::buffer(message) };
		}

		template <typename Message>
		static auto make_error_message(head_t const& h, Message&& msg, post_func_t postf = nullptr)
		{
			auto ctx = make_message(h, std::forward<Message>(msg), std::move(postf));
			ctx->head.code = static_cast<int16_t>(result_code::FAIL);
			return ctx;
		}

		template <typename Message>
		static auto make_message(head_t const& h, Message&& msg, post_func_t postf = nullptr)
		{
			return std::make_shared<context_t>(h, std::forward<Message>(msg), std::move(postf));
		}

		head_t			head;
		message_t		message;
		post_func_t		post_func;
	};
} }