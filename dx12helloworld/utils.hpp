#ifndef _UTILS_HPP
#define _UTILS_HPP

#include <Windows.h>

void CE(HRESULT x);

// https://stackoverflow.com/questions/65315241/how-can-i-fix-requires-l-value
template <class T>
constexpr auto& keep(T&& x) noexcept {
  return x;
}

#endif
