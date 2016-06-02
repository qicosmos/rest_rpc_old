# rest_rpc
modern, simple, easy to use rpc framework.
##Motivation
目前传统的c++ RPC框架一般都是基于protobuf或者是thrift，都需要用专门的代码生成器来生成代码，这种方式存在以下这些问题：

- 使用麻烦。使用时需要先写一个DSL描述文件，然后用代码生成器来生成代码，如果model类很多的时候，工作就很繁琐，工作量也比较大。
- 维护性差。当某些model类需要修改时，必须重新定义和编译，做一些繁琐而重复的工作。
- 学习成本高。使用它们之前先要学习代码生成器如何使用，还要学习复杂的DSL语法定义规则，而这些语法规则并不是通用的，一段时间不用之后又要重新去学习。
- 不能快速响应API升级的需求。当API或者协议演进的时候，就不得不让客户更新SDK。比如，当多语言的客户端较多时，每加一个接口时都要更新一堆不同语言的SDK，这是升级维护的噩梦。

面对这些问题，[rest_rpc](https://github.com/topcpporg/rest_rpc)就应运而生了，她就是来解决这些问题的，[rest_rpc](https://github.com/topcpporg/rest_rpc)的主要特性：

- 简单、好用、灵活
- 让用户只关注于业务，业务之外的事由框架负责
- 不需要学习和编写DSL
- 不需要使用代码生成器
- 能快速响应API升级的需求
- 彻底消除了繁琐重复的model定义工作
- modern(c++14), 跨平台

##Getting started

- **从github上下载**[rest_rpc](https://github.com/topcpporg/rest_rpc)

    git clone https://github.com/topcpporg/rest_rpc.git

- **下载依赖的库**[Kapok](https://github.com/qicosmos/Kapok)和[spdlog](https://github.com/gabime/spdlog)

    git submodule update --init
- **下载boost库，需要boost1.55以上版本**
- **编译**
    
    需要支持C++14的编译器，gcc4.9以上，vs2015以上

    linux下直接使用cmake编译CMakelists.txt

    win下直接使用vs2015打开restrpc.vcxproj
    

##Tutorial
###服务端代码
rpc server提供两个服务，一个是add服务，实现一个简单的加法；一个是translate服务，实现将字符串从小写转换为大写。

    #include <string>
    #include "server.hpp"
    
    int add(int a, int b)
    {
    	return a + b;
    }
    
    struct messager
    {
    	std::string translate(const std::string& orignal)
    	{
    		std::string temp = orignal;
    		for (auto & c : temp) c = toupper(c);
    		return temp;
    	}
    };
    
    int main()
    {
    	server s(9000, std::thread::hardware_concurrency()); 

    	s.register_handler("add", &add);
		messager m;
    	s.register_handler("translate", &messager::translate, &m);
    
    	s.run();
    	return 0;
    }

rpc server需要做的事情很简单，它只需要定义rpc服务接口即可，这也是rest rpc的设计理念，让用户只专注于业务。业务接口几乎不受限制，可以是普通函数，也可以是函数对象或者成员函数，函数的参数可以是基本类型也可以是结构体，灵活好用。需要注意的是函数参数不能是指针类型。

###客户端代码
rpc client连接到服务器之后，直接调用通用接口既可以完成rpc调用，调用方式接近本地调用。

    #include "client_proxy.hpp"

    int main()
    {
    	try
    	{
    		boost::asio::io_service io_service;
    		client_proxy client(io_service);
    		client.connect("127.0.0.1", "9000");
    		
    		std::string result = client.call("translate", "test");
    		//{"code":0,"result":"TEST"}
    
    		io_service.run();
    	}
    	catch (const std::exception& e)
    	{
    		std::cerr << "Exception: " << e.what() << std::endl;
    	}
    
    	return 0;
    }
client调用同步接口返回的字符串是标准的json格式，对应的结构体是一个泛型结构体

    template<typename T>
    struct response_msg
    {
    	int code;
    	T result; //json格式字符串，基本类型或者是结构体.
    };
你可以将标准的json字符串反序列化为对应的对象。比如这个例子中我们调用translate RPC服务，返回的结果是一个字符串，我们可以通过Kapok很方便地反序列化。

    DeSerializer dr;
    dr.Parse(result);

	response_msg<std::string> response = {};
    dr.Deserialize(response);
    std::cout<<respnse.result<<std::endl; //will output upper word.
客户端的通用接口call还提供了异步接口，同步还是异步由用户自己选择，更多的示例可以参考[github上的代码](https://github.com/topcpporg/rest_rpc/blob/master/client_proxy/main.cpp)。

###异常处理
rest rpc目前定义了2种异常类型，参数异常和业务逻辑异常，下面是rpc调用的结果码。

    enum result_code
    {
    	OK = 0,
    	FAIL = 1,
    	EXCEPTION = 2,
    	ARGUMENT_EXCEPTION = 3
    };

- 服务端的异常处理

    服务端如果在调用RPC过程中发生了异常，服务器会将异常信息包装为response_msg<T>，序列化为json字符串回发到客户端。
- 客户端的异常处理

    客户端收到RCP调用返回的字符串结果后需要根据结果码判断本次调用是否是正常的，或者是有异常的。下面的例子展示了如何处理可能产生的异常。

	    template<typename T>
	    void handle_result(const char* result)
	    {
	    	DeSerializer dr;
	    	dr.Parse(result);
	    	Document& doc = dr.GetDocument();
	    	doc.Parse(result);

	    	if (doc[CODE].GetInt() == result_code::OK)
	    	{
	    		response_msg<T> response = {};
	    		dr.Deserialize(response);
	    		std::cout << response.result << std::endl;
	    	}
	    	else
	    	{
	    		//maybe exception, output the exception message.
	    		std::cout << doc[RESULT].GetString() << std::endl;
	    	}
	    }

##Missuses
使用rest rpc需要注意的一些问题：

- 服务端的rpc服务函数参数中不要出现指针，其他的任意参数都可以
- 服务端的rpc服务函数参数如果有结构体，结构体必须定义一个META宏，像这样：

	    template<typename T>
	    struct response_msg
	    {
	    	int code;
	    	T result;
	    	META(code, result);
	    };

    这个宏是序列化需要用到的，具体用法可以参考[Kapok](https://github.com/qicosmos/Kapok/blob/master/main.cpp)

- 如果需要传送二进制数据的话，需要先做一个转换，将二进****制流转换为base64或者16进制，框架已经提供了这两种方式的codec.
- 目前单次传送buf的最大长度为8192，如果超出这个长度，服务器会认为长度非法。如果你希望单次传送的缓冲区更大，自行修改常量即可。
- 最重要的问题是记住：**就像调用本地函数一样调用RPC接口，除了业务逻辑之外你真的不需要关注其他！**
##Dependencies
- 网络通信依赖了boost.asio
- json序列化/反序列化依赖了[Kapok](https://github.com/qicosmos/Kapok)
- 日志依赖了[spdlog](https://github.com/gabime/spdlog)
- 依赖了c++14

##FAQ
- **为什么已经有这么多RPC库了，还要重新开发一个RPC库？**

    是的，c++的RPC库确实是有不少了，几乎都是基于thrift或是protobuf开发的，这些库都存在一个问题，我需要学习DSL和IDL工具的使用，重复而繁琐。我希望有一个非常简单易用的RPC，除了业务逻辑，其他的都不让我操心，这就是我重新开发一个RPC库的动机。希望大家用用rest rpc，感受一下她的魅力，如果觉得好用的话请不要吝惜给star。
- **这个RPC库的性能如何？**

	rest rpc底层是用的asio，性能比较好，所以rest rpc的性能也是出色的(保守估计，qps可达10w以上)，性能测试的数据稍后会给出。
- **rest rpc的学习成本高吗？**

    学习成本很低，因为我们是以用户使用的便利性作为首要的设计理念，让用户仅仅需要关注业务逻辑。
    即使是库本身的核心代码也是非常之少，不过一千多行，非常轻量级。
- **rest rpc已经用于生产环境了吗？**

    目前还没有，还处于预览版阶段，欢迎大家试用，如果发现问题了请及时向我们的[c++社区(purecpp.org)](http://purecpp.org)反馈, 或者在[github](https://github.com/topcpporg/rest_rpc)上提issue.我们会很快在生产环境中使用rest rpc了，使用稳定之后会发布正式版，请大家持续关注。也欢迎喜欢rest rpc的开发者能加入进来贡献有深度的代码，逐渐完善rest rpc的功能。
- **rest rpc支持分布式吗？**

    目前还不支持分布式，不过这已经在我们的计划中了，很快就可以实现分布式了。

