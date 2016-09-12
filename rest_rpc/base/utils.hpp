#pragma once

namespace timax{ namespace rpc 
{
	static bool retry(const std::function<bool()>& func, size_t max_attempts, size_t retry_interval = 0)
	{
		for (size_t i = 0; i < max_attempts; i++)
		{
			if (func())
				return true;

			if (retry_interval > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval));
		}

		return false;
	}

	template<typename T>
	static std::string get_json(result_code code, const T& r, std::string const& tag)
	{
		Serializer sr;
		response_msg<T> msg = { static_cast<int>(code), r };
		sr.Serialize(msg);

		std::string result = sr.GetString();

		if (!tag.empty())
		{
			auto pos = result.rfind('}');
			assert(pos != std::string::npos);
			result.insert(pos, tag);
		}

		return std::move(result);
	}

	static tcp::endpoint get_tcp_endpoint(std::string const& address, uint16_t port)
	{
		return{ boost::asio::ip::address::from_string(address), port };
	}
} }