#pragma once
// boost libraries
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread/thread.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/steady_timer.hpp>

// standard libraries
#include <cstdint>
#include <atomic>
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <tuple>
#include <list>
#include <vector>
#include <array>
#include <utility>
#include <map>
#include <stdexcept>
#include <type_traits>

// third-party libraries
#include <kapok/Kapok.hpp>
#include <msgpack.hpp>
#include <thread_pool.hpp>

namespace timax { namespace rpc
{
	using tcp = boost::asio::ip::tcp;
	using io_service_t = boost::asio::io_service;
	using lock_t = std::unique_lock<std::mutex>;
	using deadline_timer_t = boost::asio::deadline_timer;
	using steady_timer_t = boost::asio::steady_timer;
	using duration_t = steady_timer_t::duration;
	using message_t = std::vector<char>;

	class connection;

	template <typename Decode>
	class server;

	static const auto asio_error = boost::asio::placeholders::error;
} }

// common headers
#include <rest_rpc/base/log.hpp>
#include <rest_rpc/base/function_traits.hpp>
#include <rest_rpc/base/consts.h>
#include <rest_rpc/base/common.h>
#include <rest_rpc/base/utils.hpp>
#include <rest_rpc/base/excetion.hpp>
#include <rest_rpc/base/codec.hpp>