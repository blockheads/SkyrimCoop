#pragma once

#include <Wire/Buffer.h>

struct ActorValues
{
    ActorValues() = default;
    ~ActorValues() = default;

    bool operator==(const ActorValues& acRhs) const noexcept;
    bool operator!=(const ActorValues& acRhs) const noexcept;

    void Serialize(Wire::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(Wire::Buffer::Reader& aReader) noexcept;

    std::map<uint32_t, float> ActorValuesList{};
    std::map<uint32_t, float> ActorMaxValuesList{};
    std::map<uint32_t, float> ActorTemporaryValuesList{};
};
