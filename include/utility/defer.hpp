#pragma once

namespace bl {

template <typename F>
struct _privDefer {
	F f;
	_privDefer(F f) : f(f) {}
	~_privDefer() { f(); }
};

template <typename F>
_privDefer<F> _defer_make(F f) {
	return _privDefer<F>(f);
}

} // namespace bl

#define _BL_DEFER_1(x, y)	x##y
#define _BL_DEFER_2(x, y)	_BL_DEFER_1(x, y)
#define _BL_DEFER_3(x)		_BL_DEFER_2(x, __COUNTER__)
#define BL_DEFER(code)	auto _BL_DEFER_3(_defer_) = bl::_defer_make([&](){code;})
