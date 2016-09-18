#pragma once

namespace timax { namespace rpc 
{
	rpc_session::rpc_session(rpc_manager& mgr, io_service_t& ios, tcp::endpoint const& endpoint)
		: rpc_mgr_(mgr)
		, hb_timer_(ios)
		, connection_(ios, endpoint)
		, running_status_(status_t::stopped)
		, is_calling_(false)
	{
	}

	rpc_session::~rpc_session()
	{
		stop_rpc_service(error_code::CANCEL);
	}

	void rpc_session::start()
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

	void rpc_session::call(context_ptr ctx)
	{
		lock_t locker{ mutex_ };
		calls_.push_call(ctx);
		locker.unlock();
		cond_var_.notify_one();
	}

	void rpc_session::start_rpc_service()
	{
		auto expected = status_t::stopped;
		if (running_status_.compare_exchange_strong(expected, status_t::running))
		{
			recv_head();
			setup_heartbeat_timer();
			std::thread{ boost::bind(&rpc_session::send_thread, this->shared_from_this()) }.detach();
		}
	}

	void rpc_session::stop_rpc_service(error_code error)
	{
		auto expected = status_t::running;
		if (running_status_.compare_exchange_strong(expected, status_t::disable))
		{
			cond_var_.notify_one();
			stop_rpc_calls(error);
		}
	}

	void rpc_session::send_thread()
	{
		while (running_status_.load() == status_t::running)
		{
			lock_t locker{ mutex_ };
			cond_var_.wait(locker, [this]
			{
				return (!calls_.call_list_empty() && !is_calling_.load()) ||
					(running_status_.load() != status_t::running);
			});

			if (running_status_.load() != status_t::running)
				return;

			call_list_t to_calls;
			calls_.task_calls_from_list(to_calls);
			locker.unlock();

			is_calling_.store(true);
			call_impl(std::move(to_calls));
		}
	}

	void rpc_session::call_impl(call_list_t to_calls)
	{
		if (running_status_.load() == status_t::disable || to_calls.empty())
		{
			is_calling_.store(false);
		}
		else
		{
			auto to_call = to_calls.front();
			to_calls.pop_front();

			async_write(connection_.socket(), to_call->get_send_message(),
				boost::bind(&rpc_session::handle_send, this->shared_from_this(), std::move(to_calls), to_call, boost::asio::placeholders::error));
		}
	}

	void rpc_session::recv_head()
	{
		async_read(connection_.socket(), boost::asio::buffer(&head_, sizeof(head_t)),
			boost::bind(&rpc_session::handle_recv_head, this->shared_from_this(), boost::asio::placeholders::error));
	}

	void rpc_session::recv_body()
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

	void rpc_session::call_complete(uint32_t call_id, context_ptr ctx)
	{
		{
			lock_t locker{ mutex_ };
			calls_.remove_call_from_map(call_id);
		}

		auto rcode = static_cast<result_code>(head_.code);
		if (result_code::OK == rcode)
		{
			ctx->ok();
		}
		else
		{
			ctx->error(error_code::FAIL);
		}
	}

	void rpc_session::setup_heartbeat_timer()
	{
		hb_timer_.expires_from_now(boost::posix_time::seconds{ 15 });
		hb_timer_.async_wait(boost::bind(&rpc_session::handle_heartbeat, this->shared_from_this(), boost::asio::placeholders::error));
	}

	void rpc_session::stop_rpc_calls(error_code error)
	{
		call_map_t to_responses;
		{
			lock_t locker{ mutex_ };
			calls_.task_calls_from_map(to_responses);
		}
		for (auto& elem : to_responses)
		{
			auto ctx = elem.second;
			// TODO
			ctx->error(error);
		}

		rpc_mgr_.remove_session(connection_.endpoint());
	}

	void rpc_session::handle_send(call_list_t to_calls, context_ptr ctx, boost::system::error_code const& error)
	{
		if (!error)
		{
			//if (ctx->is_void)
			//{
			//	{
			//		lock_t locker{ mutex_ };
			//		calls_.remove_call_from_map(ctx->head.id);
			//	}
			//	ctx->ok();
			//}

			call_impl(std::move(to_calls));
		}
		else
		{
			// TODO log
			stop_rpc_service(error_code::BADCONNECTION);
		}
	}

	void rpc_session::handle_recv_head(boost::system::error_code const& error)
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

	void rpc_session::handle_recv_body(uint32_t call_id, context_ptr ctx, boost::system::error_code const& error)
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

	void rpc_session::handle_heartbeat(boost::system::error_code const& error)
	{
		if (!error)
		{
			lock_t locker{ mutex_ };
			if (calls_.call_list_empty())
				calls_.push_call(std::make_shared<context_t>());

			setup_heartbeat_timer();
		}
	}

	rpc_manager::rpc_manager(io_service_t& ios)
		: ios_(ios)
	{}

	void rpc_manager::call(context_ptr ctx)
	{
		get_session(ctx->endpoint)->call(ctx);
	}

	rpc_manager::session_ptr rpc_manager::get_session(tcp::endpoint const& endpoint)
	{
		lock_t locker{ mutex_ };
		auto itr = sessions_.find(endpoint);
		if (itr == sessions_.end())
		{
			auto session = std::make_shared<rpc_session>(*this, ios_, endpoint);
			session->start();
			sessions_.emplace(endpoint, session);
			return session;
		}
		else
		{
			return itr->second;
		}
	}

	void rpc_manager::remove_session(tcp::endpoint const& endpoint)
	{
		lock_t locker{ mutex_ };
		auto itr = sessions_.find(endpoint);
		if (itr != sessions_.end())
			sessions_.erase(itr);
	}
} }