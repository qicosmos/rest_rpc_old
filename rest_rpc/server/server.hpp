#pragma once

namespace timax { namespace rpc 
{
	template <typename CodecPolicy>
	class server
	{
	public:
		using codec_policy = CodecPolicy;
		using connection_ptr = std::shared_ptr<connection>;
		using connection_weak = std::weak_ptr<connection>;
		using router_t = router<codec_policy>;
		using invoker_t = typename router_t::invoker_t;
		using sub_container = std::multimap<std::string, connection_weak>;

	public:
		server(uint16_t port, size_t pool_size, duration_t time_out = duration_t::max())
			: ios_pool_(pool_size)
			, acceptor_(ios_pool_.get_ios_wrapper().get_ios(), tcp::endpoint{tcp::v4(), port})
			, time_out_(time_out)
		{
			init_callback_functions();

			register_handler(SUB_TOPIC, [this](std::string const& topic) { return sub(topic); }, [this](auto conn, auto const& topic)
			{
				if (!topic.empty())
				{
					lock_t lock{ mutex_ };
					subscribers_.emplace(topic, conn);
				}
			});
		}

		~server()
		{
			stop();
		}

		void start()
		{
			ios_pool_.start();
			do_accept();
		}

		void stop()
		{
			ios_pool_.stop();
		}

		template <typename Handler, typename PostFunc>
		bool register_handler(std::string const& name, Handler&& handler, PostFunc&& post_func)
		{
			return router_.register_invoker(name, std::forward<Handler>(handler), std::forward<PostFunc>(post_func));
		}

		template <typename Handler, typename PostFunc>
		bool async_register_handler(std::string const& name, Handler&& handler, PostFunc&& post_func)
		{
			return router_.async_register_invoker(name, std::forward<Handler>(handler), std::forward<PostFunc>(post_func));
		}

		template <typename Result>
		void pub(std::string const& topic, Result const& result)
		{
			auto buffer = codec_policy{}.pack(result);

			lock_t lock{ mutex_ };
			auto range = subscribers_.equal_range(topic);
			if (range.first == range.second)
				return;

			std::list<connection_ptr> alives;
			std::for_each(range.first, range.second, [&alives](auto& elem)
			{
				auto conn = elem.second.lock();
				if (conn)
				{
					alives.push_back(conn);
				}
			});
			lock.unlock();

			for (auto& alive_conn : alives)
			{
				alive_conn->response(buffer);
			}
		}

		void remove_sub_conn(connection_ptr conn)
		{
			lock_t lock{ mutex_ };
			for (auto itr = subscribers_.begin(); itr != subscribers_.end(); )
			{
				if (itr->second.lock() == conn)
					itr = subscribers_.erase(itr);
				else
					++itr;
			}
		}

	private:
		void init_callback_functions()
		{
			connection::set_on_error([this](connection_ptr conn_ptr, boost::system::error_code const& error)
			{
				// TODO ...
				remove_sub_conn(conn_ptr);
			});

			connection::set_on_read([this](connection_ptr conn_ptr)
			{
				// first get the buffer
				auto read_buffer = conn_ptr->get_read_buffer();

				// second get the invoker name
				std::string name = read_buffer.data();

				// third find the invoker by name
				auto data = read_buffer.data() + name.length() + 1;
				auto size = read_buffer.size() - name.length() - 1;
				router_.apply_invoker(name, conn_ptr, data, size);
			});

			connection::set_on_read_pages([this](connection_ptr conn_ptr, std::vector<char> read_buffer)
			{
				std::string name = read_buffer.data();
				auto data = read_buffer.data() + name.length() + 1;
				auto size = read_buffer.size() - name.length() - 1;
				router_.apply_invoker(name, conn_ptr, data, size);
			});
		}

		std::string sub(std::string const& topic)
		{
			if (!router_.has_invoker(topic))
			{
				using namespace std::string_literals;

				std::string error_message = "Topic:"s + topic + " not exists!";
				exception e{ error_code::FAIL, std::move(error_message) };
				throw e;
			}

			return topic;
		}

		void do_accept()
		{
			auto new_connection = std::make_shared<connection>(
				ios_pool_.get_ios_wrapper(), time_out_);

			acceptor_.async_accept(new_connection->socket(), 
				[this, new_connection](boost::system::error_code const& error)
			{
				if (!error)
				{
					new_connection->start();
				}
				else
				{
					// TODO log error
				}

				do_accept();
			});
		}

	private:
		router_t						router_;
		io_service_pool					ios_pool_;
		tcp::acceptor					acceptor_;
		duration_t						time_out_;

		mutable std::mutex				mutex_;
		sub_container					subscribers_;
	};
} }