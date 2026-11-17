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

// A guest to the host: the saves its store holds now (their ids), after one came in or was dropped.
struct SaveHeldRequest final : RequestMessage
{
    static constexpr RequestOpcode Opcode = kSaveHeldRequest;
    static constexpr size_t kMaxSaves = 16;

    SaveHeldRequest()
        : RequestMessage(Opcode)
    {
    }

    void SerializeRaw(Wire::Buffer::Writer& aWriter) const noexcept override;
    void DeserializeRaw(Wire::Buffer::Reader& aReader) noexcept override;

    bool operator==(const SaveHeldRequest& acRhs) const noexcept { return GetOpcode() == acRhs.GetOpcode() && Saves == acRhs.Saves; }

    std::vector<std::string> Saves; // SHA-256 ids, 32 binary bytes each
};

// A piece of a save on its way (docs/design/join-flow.md, "Save sync"): the save's image encoded against Base.
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
        return GetOpcode() == acRhs.GetOpcode() && Save == acRhs.Save && Base == acRhs.Base && Size == acRhs.Size && Total == acRhs.Total &&
               Offset == acRhs.Offset && Ahead == acRhs.Ahead && Data == acRhs.Data;
    }

    std::string Save;  // the image's SHA-256 id, 32 binary bytes
    std::string Base;  // the id of the image it is encoded against; empty: against none
    uint32_t Size{};   // the image's size
    uint32_t Total{};  // the encoded save's size
    uint32_t Offset{}; // this piece's offset in the encoded save
    bool Ahead{};      // sent while playing, for a later load; false: this game loads it now
    std::string Data;  // at most kMaxData bytes
};
