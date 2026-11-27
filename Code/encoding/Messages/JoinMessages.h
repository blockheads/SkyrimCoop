#pragma once

// Joining from the main menu and the characters the host's save keeps (docs/design/join-flow.md, "As built").
#include "Message.h"
#include <Structs/PlayerRecord.h>

// A guest's character, for the host's save: on joining and on its changes.
struct PlayerRecordRequest final : MessageOf<PlayerRecordRequest, kPlayerRecordRequest>
{
    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Record); }
    bool operator==(const PlayerRecordRequest&) const noexcept = default;

    PlayerRecord Record{};
};

// A guest to the host: the saves its store holds now (their ids), after one came in or was dropped.
struct SaveHeldRequest final : MessageOf<SaveHeldRequest, kSaveHeldRequest>
{
    static constexpr size_t kMaxSaves = 16;


    template <class TArchive> void Fields(TArchive& aArchive) noexcept { aArchive(Wire::List(Saves, kMaxSaves)); }
    bool operator==(const SaveHeldRequest&) const noexcept = default;

    std::vector<std::string> Saves; // SHA-256 ids, 32 binary bytes each
};

// A piece of a save on its way (docs/design/join-flow.md, "Save sync"): the save's image encoded against Base.
struct NotifySaveChunk final : MessageOf<NotifySaveChunk, kNotifySaveChunk>
{
    static constexpr uint32_t kMaxData = 32 * 1024;


    template <class TArchive> void Fields(TArchive& aArchive) noexcept
    {
        aArchive(Save, Base, Size, Total, Offset, Ahead, Data);
    }
    bool operator==(const NotifySaveChunk&) const noexcept = default;

    std::string Save;  // the image's SHA-256 id, 32 binary bytes
    std::string Base;  // the id of the image it is encoded against; empty: against none
    uint32_t Size{};   // the image's size
    uint32_t Total{};  // the encoded save's size
    uint32_t Offset{}; // this piece's offset in the encoded save
    bool Ahead{};      // sent while playing, for a later load; false: this game loads it now
    std::string Data;  // at most kMaxData bytes
};

// The host's game cannot save yet (a menu that allows no save, such as a conversation, is open): the join waits for it.
struct NotifySaveWaiting final : MessageOf<NotifySaveWaiting, kNotifySaveWaiting>
{
    template <class TArchive> void Fields(TArchive&) noexcept
    {
    }
    bool operator==(const NotifySaveWaiting&) const noexcept = default;
};
