#pragma once

#include "../../forward.hpp"

namespace timax { namespace rpc 
{
	class sub_session : public std::enable_shared_from_this<sub_session>
	{
		using function_t = std::function<void(char const*, size_t)>;

	public:
		template <typename Message>
		sub_session(io_service_t& ios, tcp::endpoint const& endpoint, Message const& topic, function_t&& func)
			: hb_timer_(ios)
			, connection_(ios, endpoint)
			, topic_(topic.begin(), topic.end())
			, function_(std::move(func))
		{
			send_head_ = recv_head_ = { 0 };
		}

		void start()
		{
			running_flag_.store(true);
			auto self = this->shared_from_this();
			connection_.start(
				[self, this]()
			{
				request_sub();
			},
				[self, this]()
			{});
		}

		void stop()
		{
			running_flag_.store(false);
		}

	private:
		auto request_sub_message()
			-> std::vector<boost::asio::const_buffer>
		{
			auto const& request_rpc = sub_topic.name();
			send_head_.len = static_cast<uint32_t>(request_rpc.size() + topic_.size() + 1);
			return 
			{
				boost::asio::buffer(&send_head_, sizeof(head_t)),
				boost::asio::buffer(request_rpc.c_str(), request_rpc.size() + 1),
				boost::asio::buffer(topic_)
			};
		}

		void request_sub()
		{
			if (running_flag_.load())
			{
				auto requet_message = request_sub_message();
				async_write(connection_.socket(), requet_message, boost::bind(&sub_session::handle_request_sub, 
					this->shared_from_this(), boost::asio::placeholders::error));
			}
		}

		void begin_sub_procedure()
		{
			//setup_heartbeat_timer();			// setup heart beat
			recv_sub_head();
		}

		void setup_heartbeat_timer()
		{
			using namespace std::chrono_literals;
			hb_timer_.expires_from_now(15s);
			hb_timer_.async_wait(boost::bind(&sub_session::handle_heartbeat, this->shared_from_this(), boost::asio::placeholders::error));
		}

		void recv_sub_head()
		{
			async_read(connection_.socket(), boost::asio::buffer(&recv_head_, sizeof(head_t)), boost::bind(
				&sub_session::handle_sub_head, this->shared_from_this(), boost::asio::placeholders::error));
		}

	private:
		void handle_request_sub(boost::system::error_code const& error)
		{
			if (!connection_.socket().is_open())
				return;

			if (!error && running_flag_.load())
			{
				async_read(connection_.socket(), boost::asio::buffer(&recv_head_, sizeof(head_t)), boost::bind(
					&sub_session::handle_response_sub_head, this->shared_from_this(), boost::asio::placeholders::error));
			}
		}

		void handle_response_sub_head(boost::system::error_code const& error)
		{
			if (!connection_.socket().is_open())
				return;

			if (!error && running_flag_.load())
			{
				if (recv_head_.len > 0)
				{
					response_.resize(recv_head_.len);
					async_read(connection_.socket(), boost::asio::buffer(response_), boost::bind(
						&sub_session::handle_response_sub_body, this->shared_from_this(), boost::asio::placeholders::error));
				}
			}
		}

		void handle_response_sub_body(boost::system::error_code const& error)
		{
			if (!connection_.socket().is_open())
				return;

			if (!error && running_flag_.load())
			{
				if (result_code::OK == static_cast<result_code>(recv_head_.code))
				{
					begin_sub_procedure();
				}
			}
		}

		void handle_sub_head(boost::system::error_code const& error)
		{
			if (!connection_.socket().is_open())
				return;

			if (!error && running_flag_.load())
			{
				if (recv_head_.len > 0)
				{
					// in this case, we got sub message
					response_.resize(recv_head_.len);
					async_read(connection_.socket(), boost::asio::buffer(response_), boost::bind(
						&sub_session::handle_sub_body, this->shared_from_this(), boost::asio::placeholders::error));
				}
				else
				{
					// in this case we got heart beat back
					recv_sub_head();
				}
			}
		}

		void handle_sub_body(boost::system::error_code const& error)
		{
			if (!connection_.socket().is_open())
				return;

			if (!error && running_flag_.load())
			{
				if (function_)
					function_(response_.data(), response_.size());

				recv_sub_head();
			}
		}

		void handle_heartbeat(boost::system::error_code const& error)
		{
			if (!error && running_flag_.load())
			{
				send_head_ = { 0 };
				async_write(connection_.socket(), boost::asio::buffer(&send_head_, sizeof(head_t)),
					boost::bind(&sub_session::handle_send_hb, this->shared_from_this(), boost::asio::placeholders::error));

				setup_heartbeat_timer();
			}
		}

		void handle_send_hb(boost::system::error_code const& error)
		{

		}

	private:
		steady_timer_t						hb_timer_;
		async_connection					connection_;

		head_t								send_head_;
		head_t								recv_head_;
		std::vector<char> const				topic_;
		std::vector<char>					response_;
		function_t							function_;
		std::atomic<bool>					running_flag_;
	};


	template <typename CodecPolicy>
	class sub_manager
	{
	public:
		using codec_policy = CodecPolicy;
		using sub_session_t = sub_session;
		using sub_session_ptr = std::shared_ptr<sub_session_t>;
		using topics_map_t = std::map<std::string, sub_session_ptr>;
		using endpoint_map_t = std::map<tcp::endpoint, topics_map_t>;

	public:
		sub_manager(io_service_t& ios)
			: ios_(ios)
		{
		}

		template <typename Protocol, typename Func>
		void sub(tcp::endpoint const& endpoint, Protocol const& protocol, Func&& func)
		{
			using endpoint_value_type = typename endpoint_map_t::value_type;

			auto session_ptr = make_sub_session(endpoint, protocol, std::forward<Func>(func));

			lock_t lock{ mutex_ };
			auto endpoint_itr = topics_.find(endpoint);
			if (topics_.end() == endpoint_itr)
			{
				topics_map_t topic_map;
				session_ptr->start();
				topic_map.emplace(protocol.name(), session_ptr);
				topics_.emplace(endpoint, std::move(topic_map));
			}
			else
			{
				auto topic_itr = endpoint_itr->second.find(protocol.name());
				if (endpoint_itr->second.end() != topic_itr)
				{
					throw exception{ error_code::UNKNOWN, "Sub topic already existed!" };
				}
				else
				{
					session_ptr->start();
					endpoint_itr->second.emplace(protocol.name(), session_ptr);
				}
			}
		}

	private:
		template <typename Protocol, typename Func>
		sub_session_ptr make_sub_session(tcp::endpoint const& endpoint, Protocol const& protocol, Func&& func)
		{
			auto topic = protocol.pack(codec_policy{});
			auto sub_proc_func = [f = std::forward<Func>(func), &protocol](char const* data, size_t size)
			{
				codec_policy cp{};
				auto result = protocol.unpack(cp, data, size);
				f(result);
			};

			return std::make_shared<sub_session_t>(ios_, endpoint, topic, std::move(sub_proc_func));
		}

	private:
		io_service_t&			ios_;
		endpoint_map_t			topics_;
		std::mutex				mutex_;
	};
} }