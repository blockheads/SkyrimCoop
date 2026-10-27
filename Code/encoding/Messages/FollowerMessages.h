#pragma once
#include <Messages/Message.h>
#include <Wire/Serialization.h>

struct NotifyFollowerTrade final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifyFollowerTrade;
    NotifyFollowerTrade() : NotifyMessage(Opcode)
    {
    }
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
    bool operator==(const NotifyFollowerTrade& acOther) const noexcept
    {
        return NpcId == acOther.NpcId;
    }
};

struct FollowerTradeClosedRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kFollowerTradeClosedRequest;
    FollowerTradeClosedRequest() : RequestMessage(Opcode)
    {
    }
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
    bool operator==(const FollowerTradeClosedRequest& acOther) const noexcept
    {
        return NpcId == acOther.NpcId;
    }
};
