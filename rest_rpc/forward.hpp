#pragma once

// standard libraries
#include <cstdint>
#include <atomic>
#include <chrono>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <functional>
#include <tuple>
#include <list>
#include <vector>
#include <array>
#include <utility>
#include <map>
#include <stdexcept>

// boost libraries
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread/thread.hpp>
#include <boost/enable_shared_from_this.hpp>

// third-party libraries
#include <kapok/kapok.hpp>

// common headers
#include <rest_rpc/base/log.hpp>
#include <rest_rpc/base/function_traits.hpp>
#include <rest_rpc/base/consts.h>
#include <rest_rpc/base/common.h>
#include <rest_rpc/base/utils.hpp>

namespace timax { namespace rpc 
{
	using tcp = boost::asio::ip::tcp;
	using io_service_t = boost::asio::io_service;

	class connection;
} }