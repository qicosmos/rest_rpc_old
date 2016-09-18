#pragma once

namespace timax { namespace rpc 
{
	static const char* SUB_TOPIC = "sub_topic";
	static const char* SUB_CONFIRM = "sub_confirm";
	static const char* HEART_BEAT = "heart_beat";
	static const char* RESULT = "result";
	static const char* CODE = "code";

	static const int MAX_BUF_LEN = 1048576;
	static const int HEAD_LEN = 12;
	static const int PAGE_SIZE = 4096;
} }