#pragma once

#include "Buffer.h"
#include <string>

namespace Wire
{
    struct Serialization
    {
        static std::string ReadString(Buffer::Reader& aReader);
        static void WriteString(Buffer::Writer& aWriter, const std::string& acString);

        static uint64_t ReadVarInt(Buffer::Reader& aReader);
        static bool TryReadVarInt(Buffer::Reader& aReader, uint64_t& aValue) noexcept;
        static void WriteVarInt(Buffer::Writer& aWriter, uint64_t aValue);

        static bool ReadBool(Buffer::Reader& aReader);
        static void WriteBool(Buffer::Writer& aWriter, bool aValue);

        static double ReadDouble(Buffer::Reader& aReader);
        static void WriteDouble(Buffer::Writer& aWriter, double aValue);

        static float ReadFloat(Buffer::Reader& aReader);
        static void WriteFloat(Buffer::Writer& aWriter, float aValue);
    };
}
