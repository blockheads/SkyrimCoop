#pragma once

#include <Structs/ScriptValue.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

// PartyNatives' rules with no engine (docs/design/cutscenes.md): whose a controls or screen call is, and who holds
// which controls; and where a call changing a player's progression runs (docs/design/dragonborn.md). Checked by
// ClientTests on Linux; PartyNatives.cpp gathers the inputs and acts.
namespace PartyRules
{
// ControlMap's user event flags used here (BSInputEnableManager::UserEvent; PartyNatives.cpp asserts they agree).
inline constexpr uint32_t kMovement = 1 << 0;
inline constexpr uint32_t kLooking = 1 << 1;
inline constexpr uint32_t kFighting = 1 << 6;
inline constexpr uint32_t kSneaking = 1 << 7;
inline constexpr uint32_t kMainFour = 1 << 8;

// GuestScripts' tag of a stack running a party call another game sent (kMirrored | that game's player id): it runs
// here only. A holder of controls is this game's own scripts (kThisGame) or another game's mirrored tag.
inline constexpr uint32_t kMirrored = 0xFFFE0000;
inline constexpr uint32_t kThisGame = 0xFFFFFFFF;
[[nodiscard]] constexpr bool IsMirrored(uint32_t aTag) noexcept
{
    return (aTag & 0xFFFF0000) == kMirrored;
}
[[nodiscard]] constexpr uint32_t MirroredTag(uint32_t aPlayerId) noexcept
{
    return kMirrored | (aPlayerId & 0xFFFF);
}
[[nodiscard]] constexpr uint32_t PlayerOf(uint32_t aMirroredTag) noexcept
{
    return aMirroredTag & 0xFFFF;
}
// Whose a controls call's holds are, from its stack's cause.
[[nodiscard]] constexpr uint32_t HolderOf(uint32_t aCause) noexcept
{
    return IsMirrored(aCause) ? aCause : kThisGame;
}

enum class Kind : uint8_t
{
    kControls, // ControlMap::ToggleControls
    kScreen,   // fades, camera, shakes, music, video
    kShow,     // what a reference shows: an art object, an effect shader, a sub-graph animation
};

enum class Share : uint8_t
{
    kHere,   // this game's only: another game's call (mirrored), or a restriction
    kPlayer, // a player's own moment: runs on that player's game only
    kParty,  // runs here, then on every other game
};

// Before the call runs. aCause is GuestScripts' (a guest, this game's player 0x14, a mirrored tag, 0 for no player's
// action); aStory, a stage that player's action set runs it.
[[nodiscard]] Share Whose(uint32_t aCause, bool aStory) noexcept;
// The whole rule, once a controls call ran and asked ControlMap for aAsked: the party's only if it took movement. A
// show is the world's look: every game's, whoever's action ran it (docs/design/dragonborn.md).
[[nodiscard]] Share ShareOf(Kind aKind, uint32_t aCause, bool aStory, uint32_t aAsked) noexcept;

// In the story's party call, the stack's player (aCause's character; this game's player 0x14 for no cause) is each
// game's own: the absorb's streams reach every player (docs/design/dragonborn.md).
void MarkEachPlayer(LocalCall& aCall, uint32_t aCause) noexcept;

// A native that changes a player's progression: its words of power, souls, skills, perks, permanent values, factions
// and spells (docs/design/dragonborn.md, "The party's calls"). A party call of one runs on every game, each on its own
// player, as the engine's own native there.
struct Progress
{
    bool AnyScript = false; // any script's call but a body's (a word wall, a book); else only the story's (Whose)
    bool OnCopy = false;    // on the host a guest's character takes it too: the host's conditions read it there
};
// The natives that change it; none for others (a body's, an item's, a read).
[[nodiscard]] std::optional<Progress> ProgressOf(std::string_view aClass, std::string_view aFunction) noexcept;

// Whom a progression call names: this game's player (a global's is always), a guest's character on the host, or none.
enum class Target : uint8_t
{
    kNone,
    kThisPlayer,
    kGuestCharacter,
};

// Where it runs: the original here, the call on this game's own player too (it named a guest's character), and every
// other game.
struct Route
{
    bool Original = true;
    bool ThisPlayer = false;
    bool Relay = false;
};
// aBody: a body's script calls it (an effect, equipment, a script on the player, a quest its body started): that
// player's own. Only the host's scripts are the story; another game's call (mirrored) runs here only.
[[nodiscard]] Route RouteOf(const Progress& acProgress, Target aTarget, uint32_t aCause, bool aStory, bool aBody,
                            bool aHosting) noexcept;

// The call as this game's player takes it: each player's form (MarkEachPlayer) is 0x14.
[[nodiscard]] LocalCall OnThisPlayer(LocalCall aCall) noexcept;

// Who holds which ControlMap flags here: the engine keeps one bitmask, so each cutscene's holds are kept apart.
class ControlHolds
{
  public:
    void Take(uint32_t aHolder, uint32_t aFlags) noexcept;
    // aHolder gives back aFlags; returns those no other holder still holds, to give back for real.
    [[nodiscard]] uint32_t GiveBack(uint32_t aHolder, uint32_t aFlags) noexcept;

    struct Returned
    {
        uint32_t Holder;
        uint32_t Held; // what it still held
        uint32_t Free; // what comes back: held by no one left
        bool NoneElse; // no one left holds anything
    };
    // aHolder's game left (none: every other game's), its holds forgotten; what each held and what comes back.
    [[nodiscard]] std::vector<Returned> Leave(std::optional<uint32_t> aHolder) noexcept;

    [[nodiscard]] uint32_t HeldBy(uint32_t aHolder) const noexcept;
    [[nodiscard]] uint32_t Held() const noexcept;
    [[nodiscard]] uint32_t HeldExcept(uint32_t aHolder) const noexcept
    {
        uint32_t flags = 0;
        for (const auto& [cHolder, cHeld] : m_holds)
            if (cHolder != aHolder)
                flags |= cHeld;
        return flags;
    }

  private:
    std::map<uint32_t, uint32_t> m_holds;
};

// Game.EnablePlayerControls' arguments to give back aFree. Camera switch, activate and the journal are not in
// ControlMap: they come back only when no one holds anything.
struct EnableControls
{
    bool Movement, Fighting, CamSwitch, Looking, Sneaking, Menu, Activate, Journal;
    bool operator==(const EnableControls&) const = default;
};
[[nodiscard]] EnableControls EnableFor(uint32_t aFree, bool aNoneElse) noexcept;
} // namespace PartyRules
