#pragma once

namespace timax { namespace rpc 
{
	class exception
	{
	public:
		MSGPACK_DEFINE(error_code_, error_message_);

	public:
		exception()
			: error_code_(0)
			, error_message_()
		{
		}

		exception(error_code ec, std::string em)
			: error_code_(static_cast<int16_t>(ec))
			, error_message_(std::move(em))
		{

		}

		error_code get_error_code() const
		{
			return static_cast<error_code>(error_code_);
		}

		std::string const& get_error_message() const
		{
			return error_message_;
		}

		void set_message(std::string message)
		{
			error_message_ = std::move(message);
		}

		void set_code(error_code ec)
		{
			error_code_ = static_cast<int16_t>(ec);
		}

	private:
		int16_t			error_code_;
		std::string		error_message_;
	};
} }