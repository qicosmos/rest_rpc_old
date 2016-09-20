#pragma once

namespace timax { namespace rpc 
{
	template <typename CodecPolicy>
	rpc_session<CodecPolicy>::rpc_session(rpc_manager_t& mgr, io_service_t& ios, tcp::endpoint const& endpoint)
		: rpc_mgr_(mgr)
		, hb_timer_(ios)
		, connection_(ios, endpoint)
		, status_(status_t::running)
		, is_write_in_progress_(false)
	{
	}

	template <typename CodecPolicy>
	rpc_session<CodecPolicy>::~rpc_session()
	{
		stop_rpc_service(error_code::CANCEL);
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::start()
	{
		auto self = this->shared_from_this();
		connection_.start(
			[this, self]		// when successfully connected
		{ 
			start_rpc_service(); 
		},
			[this, self]		// when failed to connect
		{ 
			stop_rpc_calls(error_code::BADCONNECTION);
		});
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::call(context_ptr& ctx)
	{
		if (status_t::stopped == status_.load())
		{
			ctx->error(error_code::BADCONNECTION);
			return;
		}

		lock_t lock{ mutex_ };
		if (!is_write_in_progress_)
		{
			is_write_in_progress_ = true;
			calls_.push_call_response(ctx);
			lock.unlock();

			call_impl(ctx);
		}
		else
		{
			if (!calls_.push_call(ctx))
				ctx->error(error_code::UNKNOWN);
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::start_rpc_service()
	{
		recv_head();
		setup_heartbeat_timer();
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::stop_rpc_service(error_code error)
	{
		status_ = status_t::stopped;
		stop_rpc_calls(error);
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::call_impl()
	{
		lock_t lock{ mutex_ };
		if (calls_.call_list_empty())
		{
			is_write_in_progress_ = false;
			return;
		}
		else
		{
			calls_.task_calls_from_list(to_calls_);
			lock.unlock();

			call_impl1();
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::call_impl(context_ptr& ctx)
	{
		async_write(connection_.socket(), ctx->get_send_message(), boost::bind(&rpc_session::handle_send_single,
			this->shared_from_this(), ctx, boost::asio::placeholders::error));
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::call_impl1()
	{
		context_ptr to_call = std::move(to_calls_.front());
		to_calls_.pop_front();

		async_write(connection_.socket(), to_call->get_send_message(),
			boost::bind(&rpc_session::handle_send_multiple, this->shared_from_this(), to_call, boost::asio::placeholders::error));
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::recv_head()
	{
		async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)),
			boost::bind(&rpc_session::handle_recv_head, this->shared_from_this(), boost::asio::placeholders::error));
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::recv_body()
	{
		auto call_id = head_.id;
		lock_t locker{ mutex_ };
		auto call_ctx = calls_.get_call_from_map(call_id);
		locker.unlock();
		if (nullptr != call_ctx)
		{
			if (0 == head_.len)
			{
				call_complete(call_id, call_ctx);
			}
			else
			{
				async_read(connection_.socket(), call_ctx->get_recv_message(head_.len), boost::bind(&rpc_session::handle_recv_body,
					this->shared_from_this(), call_id, call_ctx, boost::asio::placeholders::error));
			}
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::call_complete(uint32_t call_id, context_ptr& ctx)
	{
		recv_head();

		auto rcode = static_cast<result_code>(head_.code);
		if (result_code::OK == rcode)
		{
			ctx->ok();
		}
		else
		{
			ctx->error(error_code::FAIL);
		}

		{
			lock_t lock{ mutex_ };
			calls_.remove_call_from_map(call_id);
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::setup_heartbeat_timer()
	{
		using namespace std::chrono_literals;

		hb_timer_.expires_from_now(1s);
		hb_timer_.async_wait(boost::bind(&rpc_session::handle_heartbeat, this->shared_from_this(), boost::asio::placeholders::error));
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::stop_rpc_calls(error_code error)
	{
		call_map_t to_responses;
		{
			lock_t locker{ mutex_ };
			calls_.task_calls_from_map(to_responses);
		}
		for (auto& elem : to_responses)
		{
			auto ctx = elem.second;
			ctx->error(error);
		}

		rpc_mgr_.remove_session(connection_.endpoint());
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::handle_send_single(context_ptr ctx, boost::system::error_code const& error)
	{
		if (!connection_.socket().is_open())
			return;

		ctx.reset();

		if (!error)
		{
			call_impl();
		}
		else
		{
			stop_rpc_service(error_code::BADCONNECTION);
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::handle_send_multiple(context_ptr ctx, boost::system::error_code const& error)
	{
		if (!connection_.socket().is_open())
			return;

		ctx.reset();

		if (!error)
		{
			if (to_calls_.empty())
			{
				call_impl();
			}
			else
			{
				call_impl1();
			}
		}
		else
		{
			stop_rpc_service(error_code::BADCONNECTION);
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::handle_recv_head(boost::system::error_code const& error)
	{
		if (!error)
		{
			recv_body();
		}
		else
		{
			// TODO log
			stop_rpc_service(error_code::BADCONNECTION);
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::handle_recv_body(uint32_t call_id, context_ptr ctx, boost::system::error_code const& error)
	{
		if (!error)
		{
			call_complete(call_id, ctx);
		}
		else
		{
			// TODO log
			stop_rpc_service(error_code::BADCONNECTION);
		}
	}

	template <typename CodecPolicy>
	void rpc_session<CodecPolicy>::handle_heartbeat(boost::system::error_code const& error)
	{
		if (!error)
		{
			//auto ctx = std::make_shared<context_t>();
			//call(ctx);
			setup_heartbeat_timer();
			// print queue size every seconds

			lock_t lock{ mutex_ };
			auto call_list_size = calls_.get_call_list_size();
			auto call_map_size = calls_.get_call_map_size();
			lock.unlock();
			
			std::cout << to_calls_.size() << " - " << call_list_size << " - " << call_map_size << std::endl;
		}
	}

	template <typename CodecPolicy>
	rpc_manager<CodecPolicy>::rpc_manager(io_service_t& ios)
		: ios_(ios)
	{}

	template <typename CodecPolicy>
	void rpc_manager<CodecPolicy>::call(context_ptr& ctx)
	{
		get_session(ctx->endpoint)->call(ctx);
	}

	template <typename CodecPolicy>
	typename rpc_manager<CodecPolicy>::session_ptr rpc_manager<CodecPolicy>::get_session(tcp::endpoint const& endpoint)
	{
		lock_t locker{ mutex_ };
		auto itr = sessions_.find(endpoint);
		if (itr == sessions_.end())
		{
			auto session = std::make_shared<rpc_session_t>(*this, ios_, endpoint);
			session->start();
			sessions_.emplace(endpoint, session);
			return session;
		}
		else
		{
			return itr->second;
		}
	}

	template <typename CodecPolicy>
	void rpc_manager<CodecPolicy>::remove_session(tcp::endpoint const& endpoint)
	{
		lock_t locker{ mutex_ };
		auto itr = sessions_.find(endpoint);
		if (itr != sessions_.end())
			sessions_.erase(itr);
	}
} }