// Copyright (C) 2022 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.
#pragma once

#include <cctype>
#include <string>
#include <cstring>
#include <charconv>
#include <cstdlib>
#include <type_traits>
#include <TiltedCore/Stl.hpp>

namespace ServerConsole
{
// taken from
// https://stackoverflow.com/questions/29169153/how-do-i-verify-a-string-is-valid-double-even-if-it-has-a-point-in-it
inline bool IsNumber(const std::string_view s)
{
    return !s.empty() && std::find_if(s.begin(), s.end(), [](char c) { return !(std::isdigit(c) || c == '.'); }) == s.end();
}

template <typename T> T ConvertStringValue(const char* szValue, T acDefault)
{
    // on error, you get the default
    T nValue = acDefault;
#if defined(__GNUC__) && __GNUC__ < 11
    // GCC < 11 lacks std::from_chars for floating-point types
    if constexpr (std::is_floating_point_v<T>)
    {
        char* pEnd = nullptr;
        double result = std::strtod(szValue, &pEnd);
        if (pEnd != szValue)
            nValue = static_cast<T>(result);
    }
    else
    {
        std::from_chars(szValue, szValue + std::strlen(szValue), nValue);
    }
#else
    std::from_chars(szValue, szValue + std::strlen(szValue), nValue);
#endif
    return nValue;
}

bool CheckIsValidUTF8(const TiltedPhoques::String& string);
} // namespace ServerConsole
