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

// An independently compressed save block; empty Data reuses the advertised SHA-256 block.
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
        return GetOpcode() == acRhs.GetOpcode() && Offset == acRhs.Offset && Total == acRhs.Total && RawSize == acRhs.RawSize && Hash == acRhs.Hash && Data == acRhs.Data;
    }

    uint32_t Offset{}; // offset in the uncompressed packed save
    uint32_t Total{};  // the uncompressed packed save's size
    uint32_t RawSize{}; // the block's uncompressed size
    std::string Hash;   // the block's SHA-256, 32 binary bytes
    std::string Data;   // the block, snappy-compressed, at most kMaxData bytes
};
