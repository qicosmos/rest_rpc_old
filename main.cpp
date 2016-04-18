#include <iostream>
#include <string>
#include <tuple>
#include <kapok/Kapok.hpp>

#include "router.hpp"

struct person
{
	int age;
	std::string name;

	META(age, name);
};

void add(int a, int b)
{
	std::cout << a + b << std::endl;
}
void hello()
{
	std::cout << "hello" << std::endl;
}

void test_one(double d)
{
	std::cout << d << std::endl;
}

void foo(std::string b, int a)
{
	std::cout << b << std::endl;
}

void fun(const person& ps)
{
	std::cout << ps.name << std::endl;
}

void fun1(const person& ps, int a)
{
	std::cout << ps.name << std::endl;
}

void register_handler()
{

}

template<typename T>
typename std::enable_if<is_basic_type<T>::value, std::string>::type to_str(T& t)
{
	return boost::lexical_cast<std::string>(t);
}

template<typename T>
typename std::enable_if<!is_basic_type<T>::value, std::string>::type to_str(T& t)
{
	Serializer sr;
	sr.Serialize(t);
	return sr.GetString();
}

template<typename T>
std::string make_request_json(const std::string& handler_name, T&& t)
{
	Serializer sr;
	sr.Serialize(std::forward<T>(t), handler_name.c_str());
	return sr.GetString();
}

std::string make_request_json(const std::string& handler_name)
{
	return make_request_json(handler_name, "");
}

template<typename... Args>
std::string make_request_json(const std::string& handler_name, Args&&... args)
{
	auto tp = std::make_tuple(std::forward<Args>(args)...);
	return make_request_json(handler_name, tp);
}

struct complex_t
{
	std::tuple<person, int> tp;
	//person p;
	//int a;
	META(tp);
};

int main()
{
	using namespace std;

	Serializer sr;
	person _person = { 20, "aa" };

	//complex_t cmp = { std::make_tuple(person{20,"aa"}, 3) };
	//sr.Serialize(_person);

	router r;
	//设置路由
	r.register_handler("fun1", &fun1);
	r.register_handler("fun", &fun);
	r.register_handler("add", &add);
	r.register_handler("about", &hello);
	r.register_handler("foo", &foo);
	r.register_handler("test_one", &test_one);
	
	try
	{
		//解析uri实现调用
		string s4 = make_request_json("fun1", _person, 1);
		string s5 = make_request_json("fun", _person);
		string s1 = "add/1/2";
		string s2 = make_request_json("about");
		string s3 = make_request_json("foo", "test", 1);
		std::string  str = "test";
		
		
		string s6 = make_request_json("test_one", 2.6);
		r.route(s4);
		r.route(s5);
		//r.route(s1);
		r.route(s2);
		r.route(s3);
		r.route(s6);
	}
	catch (std::runtime_error &error)
	{
		std::cerr << error.what() << std::endl;
	}

	return 0;
}

