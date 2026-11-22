#pragma once

#include <Structs/GameId.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>

// The references a game created, by their created id on the wire (FormIds.h CreatedId): the host's own are their ids;
// a guest knows the copies it made of the host's (GuestEngine), and no reference of its own. Script values naming one
// cross by it (ScriptValues, GlobalCall::Read; one-simulator.md piece 7). One per World; any thread (the VM's too).
class CreatedIds
{
  public:
    // On while this game hosts: its created references are the session's.
    void SetHosting(bool aHosting) noexcept;
    // A guest's copy aFormId of the host's created reference aCreated; its end.
    void Link(uint32_t aFormId, const GameId& acCreated) noexcept;
    void Unlink(uint32_t aFormId) noexcept;
    void Clear() noexcept;

    // The created id of this game's reference; none for one no other game knows.
    [[nodiscard]] std::optional<GameId> ToSession(uint32_t aFormId) const noexcept;
    // This game's reference for a created id; 0: none here (yet).
    [[nodiscard]] uint32_t ToGame(const GameId& acCreated) const noexcept;

  private:
    mutable std::mutex m_lock;
    bool m_hosting = false;
    std::unordered_map<uint32_t, uint32_t> m_byForm; // a guest's copy -> the host's created id (its base part)
    std::unordered_map<uint32_t, uint32_t> m_byId;   // and back
};
