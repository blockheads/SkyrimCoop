// Copyright (C) 2022 TiltedPhoques SRL.
// For licensing information see LICENSE at the root of this distribution.
#pragma once

#include <cctype>
#include <cmath>
#include <string>
#include <cstring>
#include <charconv>
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
    std::from_chars(szValue, szValue + std::strlen(szValue), nValue);
    return nValue;
}

// Specialization for float (std::from_chars not available for float in GCC 10 MinGW)
template <> inline float ConvertStringValue<float>(const char* szValue, float acDefault)
{
    char* end = nullptr;
    float result = std::strtof(szValue, &end);
    // Check if conversion was successful (end pointer moved and no overflow)
    if (end != szValue && result != HUGE_VALF && result != -HUGE_VALF)
        return result;
    return acDefault;
}

// Specialization for double (std::from_chars not available for double in GCC 10 MinGW)
template <> inline double ConvertStringValue<double>(const char* szValue, double acDefault)
{
    char* end = nullptr;
    double result = std::strtod(szValue, &end);
    // Check if conversion was successful (end pointer moved and no overflow)
    if (end != szValue && result != HUGE_VAL && result != -HUGE_VAL)
        return result;
    return acDefault;
}

bool CheckIsValidUTF8(const TiltedPhoques::String& string);
} // namespace ServerConsole
