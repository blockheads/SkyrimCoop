#pragma once

#include <Papyrus/CodeReach.h>
#include <Structs/ScriptValue.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

// PartyNatives' rules with no engine: where a native's call runs (RouteOf: here, on the host's game, on the game of the
// player whose action ran it, on every game; docs/design/party-calls.md), whose a controls or screen call is, and who
// holds which controls. Checked by ClientTests on Linux; PartyNatives.cpp gathers the inputs and acts.
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
inline constexpr uint32_t kThisPlayerFormId = 0x14;
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

using NativeReach::Call;
using NativeReach::Progress;
using NativeReach::Reach;

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
[[nodiscard]] Share ShareOf(Call aKind, uint32_t aCause, bool aStory, uint32_t aAsked) noexcept;

// In the story's party call, the stack's player (aCause's character; this game's player 0x14 for no cause) is each
// game's own: the absorb's streams reach every player (docs/design/dragonborn.md).
void MarkEachPlayer(LocalCall& aCall, uint32_t aCause) noexcept;

// Whom a call names: this game's player (a global's is always), a guest's character on the host, or none.
enum class Target : uint8_t
{
    kNone,
    kThisPlayer,
    kGuestCharacter,
};

// What the router knows of a native's call, before it runs.
struct CallFacts
{
    Reach Reach;
    bool PlayerNative = false; // a global acting on this game's player (NativeReach::ReachesThisPlayer)
    Target Target = Target::kNone;
    uint32_t TargetId = 0;   // its form id
    uint32_t Cause = 0;      // GuestScripts' cause: a guest, this game's player 0x14, a mirrored tag, 0 for none
    bool Story = false;      // a stage that player's action set runs it
    bool Body = false;       // a body's script calls it: an effect, equipment, a script on the player, a body's quest
    bool GuestCause = false; // the cause is a guest whose character is on this game (the host)
    bool Hosting = false;
    bool Guest = false;    // this game is a guest in a session
    bool OwnQuest = false; // its object is a quest of this guest's own
    bool PlayerInCall = false;  // this game's player (0x14) is its object or an argument
    bool InSystemQuest = false; // a system quest's script calls it: this game's instance, for its own player
    bool SystemObject = false;  // its object is a system quest or one of its globals: each game's own
};

enum class Destination : uint8_t
{
    kHere,      // this game's only
    kHost,      // a guest's world change: the host's game makes it, for that guest
    kCauseGame, // the game of the guest whose action ran it, on its own player
    kEveryGame, // here, and on every other game
};

struct Routing
{
    Destination To = Destination::kHere;
    bool Original = true;    // the native runs here too
    bool ThisPlayer = false; // on the host it runs on this game's player too (it named a guest's character)
    bool Mark = false;       // the player in it (MarkFor's) is each game's own (MarkEachPlayer)
    uint32_t MarkFor = 0;
    bool Ask = false;        // a question with no player's action: asked of the party's player nearest the asker (rule 3)
    bool operator==(const Routing&) const = default;
};
// Where a call runs (docs/design/party-calls.md). Another game's call (mirrored) runs here only.
[[nodiscard]] Routing RouteOf(const CallFacts& acFacts) noexcept;

// A system quest (docs/design/system-quests.md): one instance per game, for that game's own player. A script calling
// into aCallee's function (a quest, or one of its aliases' quest) makes it one when the caller is this game's
// player's script or effect, or another system quest's; not its own quest's call, nor a guest's own quest.
[[nodiscard]] bool LearnsSystemQuest(uint32_t aCallee, uint32_t aCallerQuest, bool aCallerOnPlayer, bool aCallerSystem,
                                     bool aCalleeOwn) noexcept;
// Whether a function of aClass is one the quest's own script declares, not the engine's base scripts' (Quest.SetStage
// is a script function over SetCurrentStageID: a call of it is the quest's world, not its instance's).
[[nodiscard]] bool IsQuestScriptFunction(std::string_view aClass) noexcept;
// Where a call into a system quest's function runs, from outside it: a player's own action on that player's game (a
// guest's, sent there from the host); the story's or no player's on every game, each on its own player. One returning
// a value is answered here, unless it goes to a guest's game.
[[nodiscard]] Routing RouteOfSystemCall(uint32_t aCause, bool aStory, bool aGuestCause, bool aHosting, bool aReturnsValue) noexcept;

// A player a question may be asked of (rule 3): 0x14 or a guest's character, with its distance to the reference the
// question measures from, or whether it is in the location the asking script names.
struct Asked
{
    uint32_t Player = 0;
    std::optional<float> Distance; // none: no such reference, or not in its worldspace or interior
    bool InPlace = false;
};
// Who answers: the nearest to the reference; else the first in the named place; else none (this game's player).
[[nodiscard]] std::optional<uint32_t> AskedPlayer(const std::vector<Asked>& acPlayers) noexcept;

// Whom a question asked of the party is put to, in order (rule 3): a yes/no question to every player, this game's
// first, until one says yes; any other (a distance, a location, a cell) to the one AskedPlayer picks (none: this
// game's player).
[[nodiscard]] std::vector<uint32_t> PlayersToAsk(bool aYesNo, std::optional<uint32_t> aPicked,
                                                 const std::vector<uint32_t>& acGuests) noexcept;

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

// The copies of other games' latent party calls running here (a story's Message.Show): each source's calls arrive in
// order with their key (the sender's stack id), and start waiting here in that order. When the original returns, its
// copy here ends with the same result (docs/design/party-calls.md, "A box's answer").
class LatentCopies
{
  public:
    // aSource's call with aKey was queued here.
    void Sent(uint32_t aSource, uint32_t aKey) noexcept;
    // A copy from aSource started waiting in aStack: the oldest of its calls not yet started. Its original's result
    // when that already came: the copy ends with it at once.
    [[nodiscard]] std::optional<LocalValue> Started(uint32_t aSource, uint32_t aStack) noexcept;
    // aSource's original with aKey returned acResult: the stack its copy waits in, if it does (forgotten); a copy not
    // started yet keeps the result for its start.
    [[nodiscard]] std::optional<uint32_t> Ended(uint32_t aSource, uint32_t aKey, const LocalValue& acResult) noexcept;
    // aStack returned here (its player answered, or the original's result came): forgotten.
    void Returned(uint32_t aStack) noexcept;
    // aSource left: its calls are forgotten.
    void Left(uint32_t aSource) noexcept;

  private:
    struct Queued
    {
        uint32_t Key;
        std::optional<LocalValue> Result; // its original already returned
    };
    std::map<uint32_t, std::vector<Queued>> m_queued;                   // source -> calls sent, not started
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> m_waiting;        // (source, key) -> the stack here
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
