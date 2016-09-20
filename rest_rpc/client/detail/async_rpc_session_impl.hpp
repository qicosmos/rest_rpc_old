#pragma once

namespace timax { namespace rpc 
{
	rpc_session::rpc_session(rpc_manager& mgr, io_service_t& ios, tcp::endpoint const& endpoint)
		: rpc_mgr_(mgr)
		, hb_timer_(ios)
		, connection_(ios, endpoint)
		, status_(status_t::running)
		, is_write_in_progress_(false)
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

	void rpc_session::call(context_ptr& ctx)
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

	void rpc_session::start_rpc_service()
	{
		recv_head();
		setup_heartbeat_timer();
	}

	void rpc_session::stop_rpc_service(error_code error)
	{
		status_ = status_t::stopped;
		stop_rpc_calls(error);
	}

	void rpc_session::call_impl()
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

	void rpc_session::call_impl(context_ptr& ctx)
	{
		async_write(connection_.socket(), ctx->get_send_message(), boost::bind(&rpc_session::handle_send_single,
			this->shared_from_this(), ctx, boost::asio::placeholders::error));
	}

	void rpc_session::call_impl1()
	{
		context_ptr to_call = std::move(to_calls_.front());
		to_calls_.pop_front();

		async_write(connection_.socket(), to_call->get_send_message(),
			boost::bind(&rpc_session::handle_send_multiple, this->shared_from_this(), to_call, boost::asio::placeholders::error));
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

	void rpc_session::call_complete(uint32_t call_id, context_ptr& ctx)
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

	void rpc_session::setup_heartbeat_timer()
	{
		using namespace std::chrono_literals;

		hb_timer_.expires_from_now(1s);
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
			ctx->error(error);
		}

		rpc_mgr_.remove_session(connection_.endpoint());
	}

	void rpc_session::handle_send_single(context_ptr ctx, boost::system::error_code const& error)
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

	void rpc_session::handle_send_multiple(context_ptr ctx, boost::system::error_code const& error)
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
			auto ctx = std::make_shared<context_t>();
			call(ctx);
			setup_heartbeat_timer();
			// print queue size every seconds

			//lock_t lock{ mutex_ };
			//auto call_list_size = calls_.get_call_list_size();
			//auto call_map_size = calls_.get_call_map_size();
			//lock.unlock();
			//
			//std::cout << to_calls_.size() << " - " << call_list_size << " - " << call_map_size << std::endl;
		}
	}

	rpc_manager::rpc_manager(io_service_t& ios)
		: ios_(ios)
	{}

	void rpc_manager::call(context_ptr& ctx)
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