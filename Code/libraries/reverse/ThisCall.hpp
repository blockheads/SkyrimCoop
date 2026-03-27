#pragma once

#include <AutoPtr.hpp>

namespace TiltedPhoques
{
    template<class TFunc, class TThis, class... TArgs>
    constexpr decltype(auto) ThisCall(TFunc* aFunction, TThis* apThis, TArgs&& ... args) noexcept
    {
#if TP_PLATFORM_64
        return aFunction(apThis, std::forward<TArgs>(args)...);
#else
        return aFunction(apThis, nullptr, std::forward<TArgs>(args)...);
#endif
    }

    template<class TFunc, class TThis, class... TArgs>
    constexpr decltype(auto) ThisCall(TFunc* aFunction, AutoPtr<TThis>& aThis, TArgs&& ... args) noexcept
    {
        return ThisCall(aFunction, aThis.Get(), args...);
    }

    template<class TFunc, class TThis, class... TArgs>
    constexpr decltype(auto) ThisCall(AutoPtr<TFunc>& aFunction, AutoPtr<TThis>& aThis, TArgs&& ... args) noexcept
    {
        return ThisCall(aFunction.Get(), aThis.Get(), args...);
    }

    template<class TFunc, class TThis, class... TArgs>
    constexpr decltype(auto) ThisCall(AutoPtr<TFunc>& aFunction, TThis* apThis, TArgs&& ... args) noexcept
    {
        return ThisCall(aFunction.Get(), apThis, args...);
    }
}

#if TP_PLATFORM_32
#define TP_THIS_FUNCTION(typeName, retName, className, ...) using typeName = retName (__fastcall)(className*, void*, ##__VA_ARGS__);
#define TP_MAKE_THISCALL(functionName, className, ...) __fastcall functionName(className* apThis, void *edx, ##__VA_ARGS__)
#else
// On x64, __fastcall is the default calling convention. GCC does not allow
// __fastcall in 'using' type alias declarations, so omit it on GCC x64.
#ifdef _MSC_VER
#define TP_THIS_FUNCTION(typeName, retName, className, ...) using typeName = retName (__fastcall)(className*, ##__VA_ARGS__);
#define TP_MAKE_THISCALL(functionName, className, ...) __fastcall functionName(className* apThis, ##__VA_ARGS__)
#else
#define TP_THIS_FUNCTION(typeName, retName, className, ...) using typeName = retName (className*, ##__VA_ARGS__);
#define TP_MAKE_THISCALL(functionName, className, ...) functionName(className* apThis, ##__VA_ARGS__)
#endif
#endif

