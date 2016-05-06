#pragma once
class noncopyable
{
protected:
	noncopyable(const noncopyable&) = delete;
	noncopyable operator = (const noncopyable&) = delete;

	noncopyable() = default;
	virtual ~noncopyable() {};
};
