#pragma once

struct ActorValues
{
    ActorValues() = default;
    ~ActorValues() = default;

    bool operator==(const ActorValues& acRhs) const noexcept;
    bool operator!=(const ActorValues& acRhs) const noexcept;

    void Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept;
    void Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept;

    std::map<uint32_t, float> ActorValuesList{};
    std::map<uint32_t, float> ActorMaxValuesList{};
};
