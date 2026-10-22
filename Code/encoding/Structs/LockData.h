#pragma once

#include <Wire/Buffer.h>

struct LockData
{
    LockData() = default;
    ~LockData() = default;

    bool operator==(const LockData& acRhs) const noexcept;
    bool operator!=(const LockData& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    bool IsLocked{};
    uint8_t LockLevel{};
};
