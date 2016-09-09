#pragma once

namespace timax { namespace rpc
{
	class rpc_manager;

	class rpc_session : public std::enable_shared_from_this<rpc_session>
	{
		enum class status_t
		{
			stopped,
			disable,
			running,
		};

	public:
		using context_t = rpc_call_container::context_t;
		using context_ptr = rpc_call_container::context_ptr;
		using call_list_t = rpc_call_container::call_list_t;
		using call_map_t = rpc_call_container::call_map_t;
			
	public:
		rpc_session(rpc_manager& mgr, io_service_t& ios, tcp::endpoint const& endpoint);
		~rpc_session();
		void start();
		void call(context_ptr ctx);

	private:
		void start_rpc_service();
		void stop_rpc_service(error_code error);
		void send_thread();
		void call_impl(call_list_t to_calls);
		void recv_head();
		void recv_body();
		void call_complete(uint32_t call_id, context_ptr ctx);
		void setup_heartbeat_timer();
		void stop_rpc_calls(error_code error);

	private:  // handlers
		void handle_send(call_list_t to_calls, context_ptr ctx, boost::system::error_code const& error);
		void handle_recv_head(boost::system::error_code const& error);
		void handle_recv_body(uint32_t call_id, context_ptr ctx, boost::system::error_code const& error);
		void handle_heartbeat(boost::system::error_code const& error);

	private:
		rpc_manager&						rpc_mgr_;
		deadline_timer_t					hb_timer_;
		async_connection					connection_;
		rpc_call_container					calls_;
		std::atomic<status_t>				running_status_;
		std::atomic<bool>					is_calling_;
		head_t								head_;
		mutable std::mutex					mutex_;
		mutable std::condition_variable		cond_var_;
	};

	class rpc_manager
	{
		friend class rpc_session;
	public:
		using session_map_t = std::map<tcp::endpoint, std::shared_ptr<rpc_session>>;
		using session_ptr = std::shared_ptr<rpc_session>;
		using context_ptr = rpc_session::context_ptr;

	public:
		explicit rpc_manager(io_service_t& ios);
		void call(context_ptr ctx);

	private:
		session_ptr get_session(tcp::endpoint const& endpoint);
		void remove_session(tcp::endpoint const& endpoint);

	private:
		io_service_t&						ios_;
		session_map_t						sessions_;
		mutable std::mutex					mutex_;
	};
} }