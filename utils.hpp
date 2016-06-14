#pragma once
#include <functional>
#include <chrono>
#include <thread>
#include "common.h"
#include <kapok/Kapok.hpp>

static bool retry(const std::function<bool()>& func, size_t max_attempts, size_t retry_interval = 0) 
{
	for (size_t i = 0; i < max_attempts; i++)
	{
		if (func())
			return true;

		if(retry_interval>0)
			std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval));
	}
	
	return false;
}

template<typename T>
static std::string get_json(result_code code, const T& r)
{
	response_msg<T> msg = { static_cast<int>(code), r };

	Serializer sr;
	sr.Serialize(msg);
	return sr.GetString();
}

