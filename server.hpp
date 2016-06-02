#pragma once
#include <thread>
#include <mutex>

#include "connection.hpp"
#include "io_service_pool.hpp"
#include "router.hpp"

using boost::asio::ip::tcp;

class server : private boost::noncopyable
{
public:
	server(short port, size_t size, size_t timeout_milli = 0) : io_service_pool_(size), timeout_milli_(timeout_milli),
		acceptor_(io_service_pool_.get_io_service(), tcp::endpoint(tcp::v4(), port))
	{
#ifdef PUB_SUB
		register_handler(SUB_TOPIC, &server::sub, this);
#endif
		router::get().set_callback(std::bind(&server::callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
		do_accept();
	}

	~server()
	{
		io_service_pool_.stop();
		thd_->join();
	}

	void run()
	{
		thd_ = std::make_shared<std::thread>([this] {io_service_pool_.run(); });
	}

	template<typename Function>
	void register_handler(std::string const & name, const Function& f) 
	{
		router::get().register_handler(name, f);
	}

	template<typename Function, typename Self>
	void register_handler(std::string const & name, const Function& f, Self* self) 
	{
		router::get().register_handler(name, f, self);
	}

	void remove_handler(std::string const& name) 
	{
		router::get().remove_handler(name);
	}

private:
	void do_accept()
	{
		conn_.reset(new connection(io_service_pool_.get_io_service(), timeout_milli_));
		acceptor_.async_accept(conn_->socket(), [this](boost::system::error_code ec)
		{
			if (ec)
			{
				//todo log
			}
			else
			{
				conn_->start();
			}

			do_accept();
		});
	}

private:
	std::string sub(const std::string& topic)
	{
		return topic;
	}

	void pub(const std::string& topic, const std::string& result)
	{
#ifdef PUB_SUB
		decltype(conn_map_.equal_range(topic)) temp;
		std::unique_lock<std::mutex> lock(mtx_);

		auto range = conn_map_.equal_range(topic);
		if (range.first == range.second)
			return;

		temp = range;
		lock.unlock();

		for (auto it = range.first; it != range.second;)
		{
			auto ptr = it->second.lock();
			if (!ptr)
				it = conn_map_.erase(it);
			else
			{
				ptr->response(result.c_str());
				++it;
			}
		}

		lock.lock(); //clear invalid connection

		for (auto it = conn_map_.cbegin(); it != conn_map_.end();)
		{
			auto ptr = it->second.lock();
			if (!ptr)
			{
				it = conn_map_.erase(it);
			}
			else
			{
				++it;
			}
		}
#endif
	}

	//this callback from router, tell the server which connection sub the topic and the result of handler
	void callback(const std::string& topic, const char* result, std::shared_ptr<connection> conn, bool has_error = false)
	{
#ifdef PUB_SUB
		if (!has_error)
		{
			SPD_LOG_ERROR(result);
			return;
		}

		if (topic == SUB_TOPIC)
		{
			rapidjson::Document doc;
			doc.Parse(result);
			auto handler_name = doc[RESULT].GetString();
			std::weak_ptr<connection> wp(conn);
			conn_map_.emplace(handler_name, wp);
			conn->response(result);
			return;
		}

		if (!conn_map_.empty())
		{
			pub(topic, result);
			conn->read_head();
		}
#else
		conn->response(result);
#endif
	}

	io_service_pool io_service_pool_;
	tcp::acceptor acceptor_;
	std::shared_ptr<connection> conn_;
	std::shared_ptr<std::thread> thd_;
	std::size_t timeout_milli_;
#ifdef PUB_SUB
	std::multimap<std::string, std::weak_ptr<connection>> conn_map_;
	std::mutex mtx_;
#endif
};

