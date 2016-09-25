#pragma once

namespace timax { namespace rpc 
{
	template <typename CodecPolicy>
	class sync_client
	{
		using codec_policy = CodecPolicy;
		using async_client_t = async_client<codec_policy>;
		using async_client_ptr = std::shared_ptr<async_client_t>;
		
	public:
		sync_client()
			: client_(std::make_shared<async_client_t>())
		{
		}

		template <typename Protocol, typename ... Args>
		auto call(tcp::endpoint const& endpoint, Protocol const& protocol, Args&& ... args)
		{
			using result_type = typename Protocol::result_type;
			return call_impl(std::is_void<result_type>{}, endpoint, protocol, std::forward<Args>(args)...);
		}

	private:
		template <typename Protocol, typename ... Args>
		auto call_impl(std::false_type, tcp::endpoint const& endpoint, Protocol const& protocol, Args&& ... args)
		{
			using result_type = typename Protocol::result_type;
			auto task = client_->call(endpoint, protocol, std::forward<Args>(args)...);
			return std::move(const_cast<result_type&>(task.get()));
		}

		template <typename Protocol, typename ... Args>
		auto call_impl(std::true_type, tcp::endpoint const& endpoint, Protocol const& protocol, Args&& ... args)
		{
			auto task = client_->call(endpoint, protocol, std::forward<Args>(args)...);
			task.wait();
		}

	private:
		async_client_ptr		client_;
	}; 
} }