#pragma once

#include <utility>

template <typename F>
class Deferred final {
	F f;

public:
	Deferred(F&& f)
		: f{ f }
	{}
	~Deferred() { f(); }
};

template <typename F>
Deferred<F> defer(F&& f)
{
	return Deferred<F>(std::forward<F>(f));
}

#define DEFER(f) const auto __defer_##__FILE__##__LINE__ = defer(f)
