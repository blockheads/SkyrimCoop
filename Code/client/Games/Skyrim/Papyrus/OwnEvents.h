#pragma once

#include <Games/EngineNames.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

// Which engine events are this game's own player's (WorldEvents' hook on SendEvent, 104800), and the quests a guest's
// own scripts started (PlayerNatives), with no engine (docs/design/world-state-sync.md, "A guest's own quests";
// docs/design/cutscenes.md, "Whose moment"). Checked by ClientTests on Linux.
namespace OwnEvents
{
inline constexpr uint32_t kThisPlayer = 0x14;

// What a VM handle names (SkyrimScript::HandlePolicy::GetHandleForObject, 53508): a form is 0x0000'FFFF'<form id>, an
// active effect 0x0002'<unique id>'<the actor it is on>, an alias 0x0000'<alias id>'<its quest's form id>.
[[nodiscard]] constexpr bool IsAboutPlayer(uint64_t aHandle) noexcept
{
    const auto cKind = static_cast<uint16_t>(aHandle >> 48);
    const auto cTag = static_cast<uint16_t>(aHandle >> 32);
    const bool cForm = cKind == 0 && cTag == 0xFFFF;
    const bool cEffect = cKind == 2;
    return (cForm || cEffect) && static_cast<uint32_t>(aHandle) == kThisPlayer;
}
// The quest a quest's or an alias's handle names (its low half), none (0) for other kinds.
[[nodiscard]] constexpr uint32_t QuestOf(uint64_t aHandle) noexcept
{
    return static_cast<uint16_t>(aHandle >> 48) == 0 ? static_cast<uint32_t>(aHandle) : 0;
}

// An event the engine sends to a handle's scripts.
struct SentEvent
{
    uint64_t Handle = 0;
    std::string_view Event;
    bool FromScriptExtender = false; // sent from SKSE's code: input, menus, mod events
};

// Whether this game's own player or UI caused it: SKSE's events, the game's load, the player's own scripts; a quest its
// own scripts started (aIsOwnQuest(form id)) or one of its aliases; a timer or registration its own scripts set
// (aRegisteredBy(handle, event) is this game's player).
template <class TIsOwnQuest, class TRegisteredBy>
[[nodiscard]] bool IsOwnEvent(const SentEvent& acEvent, TIsOwnQuest&& aIsOwnQuest, TRegisteredBy&& aRegisteredBy)
{
    if (acEvent.FromScriptExtender || SameName(acEvent.Event, "OnPlayerLoadGame") || IsAboutPlayer(acEvent.Handle))
        return true;
    if (const uint32_t cQuest = QuestOf(acEvent.Handle); cQuest && aIsOwnQuest(cQuest))
        return true;
    return !acEvent.Event.empty() && aRegisteredBy(acEvent.Handle, acEvent.Event) == kThisPlayer;
}

enum class Delivery : uint8_t
{
    kRun,          // as the engine sends it
    kAsThisPlayer, // run, its scripts (and what they start) this game's player's
    kDrop,         // not this game's to run
};

// What happens to an event. On the host (and outside a session) every event runs; in a session the ones its own player
// or UI caused, sent by the engine and not a script's native, run as its player. On a guest only its own run, as its
// player: the host's game runs the world's scripts. aIsOwn() is asked only if it matters.
template <class TIsOwn>
[[nodiscard]] Delivery Deliver(bool aIsGuest, bool aInSession, bool aFromNative, TIsOwn&& aIsOwn)
{
    if (!aIsGuest)
        return aInSession && !aFromNative && aIsOwn() ? Delivery::kAsThisPlayer : Delivery::kRun;
    return aIsOwn() ? Delivery::kAsThisPlayer : Delivery::kDrop;
}

// The quests this guest's own scripts started (form ids). Its lock is a leaf: WorldEvents reads it with engine locks
// held.
class OwnQuests
{
  public:
    // A quest's start (TESQuest::EnsureQuestStarted, 25003) on a guest, in a stack whose cause is aCause: one this
    // game's player's scripts started, not running before, is the guest's own. True when it became one.
    bool Started(uint32_t aQuest, bool aWasRunning, bool aStarted, bool aIsGuest, uint32_t aCause) noexcept;
    [[nodiscard]] bool Contains(uint32_t aQuest) noexcept;
    // Joining or leaving: none.
    void Clear() noexcept;

  private:
    std::mutex m_lock;
    std::unordered_set<uint32_t> m_quests;
    std::atomic<bool> m_any{false}; // until then a read skips the lock
};

// The quests one per player (docs/design/system-quests.md): each game runs its own instance for its own player. And the
// globals their scripts hold, each game's own value. Its lock is a leaf: the hooks read it with engine locks held.
class SystemQuestList
{
  public:
    // True when it is new here.
    bool Learn(uint32_t aQuest) noexcept;
    [[nodiscard]] bool Any() const noexcept
    {
        return m_any.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool Contains(uint32_t aQuest) noexcept;
    [[nodiscard]] std::vector<uint32_t> All() noexcept;
    // aQuest's globals, once read from its scripts; the ones it has none read for yet.
    void SetGlobals(uint32_t aQuest, const std::vector<uint32_t>& acGlobals) noexcept;
    [[nodiscard]] std::vector<uint32_t> WithoutGlobals() noexcept;
    [[nodiscard]] bool IsGlobal(uint32_t aGlobal) noexcept;
    [[nodiscard]] std::vector<uint32_t> Globals() noexcept;
    // A new game, or a load starting: none (the save brings its own).
    void Clear() noexcept;

  private:
    std::mutex m_lock;
    std::map<uint32_t, std::optional<std::vector<uint32_t>>> m_quests; // quest -> its globals, once read
    std::unordered_set<uint32_t> m_globals;
    std::atomic<bool> m_any{false};
};
} // namespace OwnEvents
