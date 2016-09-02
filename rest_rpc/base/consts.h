#pragma once

namespace timax { namespace rpc 
{
	static const char* SUB_TOPIC = "sub_topic";
	static const char* RESULT = "result";
	static const char* CODE = "code";

	static const int MAX_BUF_LEN = 1048576;
	static const int HEAD_LEN = 12;
} }