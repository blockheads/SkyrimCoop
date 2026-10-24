#pragma once

// Joining from the main menu and the characters the host's save keeps (docs/design/join-flow.md, "As built").
#include "Message.h"
#include <Structs/PlayerRecord.h>

// A guest's character, for the host's save: on joining and on its changes.
struct PlayerRecordRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kPlayerRecordRequest;

    PlayerRecordRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const PlayerRecordRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Record == acRhs.Record; }

    PlayerRecord Record{};
};

// A piece of the host's save for a game joining from the main menu: the .ess and the .skse, compressed together.
struct NotifySaveChunk final : NotifyMessage
{
    static constexpr NotifyOpcode Opcode = kNotifySaveChunk;
    static constexpr uint32_t kMaxData = 32 * 1024;

    NotifySaveChunk()
        : NotifyMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const NotifySaveChunk& acRhs) const noexcept
    {
        return GetOpcode() == acRhs.GetOpcode() && Offset == acRhs.Offset && Total == acRhs.Total && Data == acRhs.Data;
    }

    uint32_t Offset{}; // where Data goes in the compressed save
    uint32_t Total{};  // the compressed save's size
    std::string Data;  // at most kMaxData bytes
};
