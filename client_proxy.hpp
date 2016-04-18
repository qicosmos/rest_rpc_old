#pragma once
#include <string>
#include "router.hpp"
class client_proxy
{
public:
	client_proxy(const router& router) : router_(router) {}

	template<typename... Args>
	void call(const char* handler_name, Args&&... args)
	{
		std::string json_str = make_request_json(handler_name, std::forward<Args>(args)...);
		router_.route(json_str.c_str());
	}

private:
	template<typename T>
	std::string make_request_json(const char* handler_name, T&& t)
	{
		sr_.Serialize(std::forward<T>(t), handler_name);
		return sr_.GetString();
	}

	std::string make_request_json(const char* handler_name)
	{
		return make_request_json(handler_name, "");
	}

	template<typename... Args>
	std::string make_request_json(const char* handler_name, Args&&... args)
	{
		auto tp = std::make_tuple(std::forward<Args>(args)...);
		return make_request_json(handler_name, tp);
	}

	router router_;
	Serializer sr_;
};

