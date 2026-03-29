#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Recording
{

inline std::string Base64Encode(const uint8_t* apData, size_t aSize)
{
    static constexpr char s_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string result;
    result.reserve(((aSize + 2) / 3) * 4);

    for (size_t i = 0; i < aSize; i += 3)
    {
        const uint32_t b0 = apData[i];
        const uint32_t b1 = (i + 1 < aSize) ? apData[i + 1] : 0;
        const uint32_t b2 = (i + 2 < aSize) ? apData[i + 2] : 0;

        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;

        result += s_table[(triple >> 18) & 0x3F];
        result += s_table[(triple >> 12) & 0x3F];
        result += (i + 1 < aSize) ? s_table[(triple >> 6) & 0x3F] : '=';
        result += (i + 2 < aSize) ? s_table[triple & 0x3F] : '=';
    }

    return result;
}

inline std::vector<uint8_t> Base64Decode(const std::string& aEncoded)
{
    static constexpr uint8_t s_decode[256] = {
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
         52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
        255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
         15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
        255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
         41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
    };

    if (aEncoded.empty())
        return {};

    std::vector<uint8_t> result;
    result.reserve((aEncoded.size() / 4) * 3);

    for (size_t i = 0; i < aEncoded.size(); i += 4)
    {
        const uint32_t a = s_decode[static_cast<uint8_t>(aEncoded[i])];
        const uint32_t b = (i + 1 < aEncoded.size()) ? s_decode[static_cast<uint8_t>(aEncoded[i + 1])] : 0;
        const uint32_t c = (i + 2 < aEncoded.size()) ? s_decode[static_cast<uint8_t>(aEncoded[i + 2])] : 0;
        const uint32_t d = (i + 3 < aEncoded.size()) ? s_decode[static_cast<uint8_t>(aEncoded[i + 3])] : 0;

        const uint32_t triple = (a << 18) | (b << 12) | (c << 6) | d;

        result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));

        if (i + 2 < aEncoded.size() && aEncoded[i + 2] != '=')
            result.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));

        if (i + 3 < aEncoded.size() && aEncoded[i + 3] != '=')
            result.push_back(static_cast<uint8_t>(triple & 0xFF));
    }

    return result;
}

} // namespace Recording
