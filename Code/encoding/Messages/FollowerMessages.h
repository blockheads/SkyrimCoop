#pragma once
#include <Messages/Message.h>
#include <Wire/Serialization.h>

struct NotifyFollowerTrade final : MessageOf<NotifyFollowerTrade, kNotifyFollowerTrade>
{
    uint32_t NpcId{};
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Serialization::WriteVarInt(aWriter, NpcId);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        NotifyMessage::DeserializeRaw(aReader);
        NpcId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    }
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId); }
    bool operator==(const NotifyFollowerTrade&) const noexcept = default;
};

struct FollowerTradeClosedRequest final : MessageOf<FollowerTradeClosedRequest, kFollowerTradeClosedRequest>
{
    uint32_t NpcId{};
    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override
    {
        Serialization::WriteVarInt(aWriter, NpcId);
    }
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override
    {
        RequestMessage::DeserializeRaw(aReader);
        NpcId = static_cast<uint32_t>(Serialization::ReadVarInt(aReader));
    }
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(NpcId); }
    bool operator==(const FollowerTradeClosedRequest&) const noexcept = default;
};
