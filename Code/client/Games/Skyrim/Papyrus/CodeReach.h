#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// NativeReach's reading of code with no engine or Windows: what a function's instructions address and call, which of
// the party's engine calls a native makes, and whether its calls reach this game's player
// (docs/re/systems/papyrus-guest-scripts.md, "Natives on the player"; docs/design/cutscenes.md). The process is asked
// through Code: NativeReach.cpp answers from the running game, ClientTests from bytes.
namespace NativeReach
{
// What a native's own code calls: the controls (ControlMap::ToggleControls) or the screen (fades, camera, shakes,
// music, video), the party's (docs/design/cutscenes.md); the world (a quest's stage), the host's to change; what a
// reference shows (an art object, an effect shader, a sub-graph animation), every game's (docs/design/dragonborn.md); or
// what moves or drives an actor's body (a move, an idle, AI driving), each player's own in the story's scenes.
enum class Call : uint8_t
{
    kNone,
    kControls,
    kScreen,
    kWorld,
    kShow,
    kProgress, // a player's progression (docs/design/dragonborn.md, "The party's calls")
    kItem,     // an actor's items: each player's own, never the party's (docs/design/party-model.md, "Items")
    kQuestion, // where a reference is: asked of the party with no player's action behind it (party-model.md rule 3)
    kBody,     // moves, poses or drives an actor's body (cutscenes.md step 2)
    kPlayerMenu, // a menu on another actor (barter): opened on the game of the player whose action asked for it
};

// How the party shares a change to a player's progression (docs/design/dragonborn.md, "The party's calls"): it runs
// on every game, each on its own player, as the engine's own native there.
struct Progress
{
    bool AnyScript = false; // any script's call but a body's (a word wall, a book); else only the story's
    bool OnCopy = false;    // on the host a guest's character takes it too: the host's conditions read it there
    bool operator==(const Progress&) const = default;
};

// What a native's code calls, and for a progression change how the party shares it.
struct Reach
{
    Call Kind = Call::kNone;
    Progress Progress{};
    bool operator==(const Reach&) const = default;
};

struct Range
{
    uintptr_t Begin;
    uintptr_t End;
    [[nodiscard]] bool Contains(uintptr_t aAddress) const noexcept
    {
        return aAddress >= Begin && aAddress < End;
    }
};

// What reading code asks of the process.
class Code
{
  public:
    virtual ~Code() = default;

    // The code readable at aAt, up to aMax bytes.
    [[nodiscard]] virtual std::span<const uint8_t> At(uintptr_t aAt, size_t aMax) const noexcept = 0;
    // The unwind entry holding aAddress (x64 exception data): its range, and where the function it belongs to starts
    // (its chain's root). None: no entry, a leaf.
    struct Entry
    {
        Range Part;
        uintptr_t Function;
    };
    [[nodiscard]] virtual std::optional<Entry> EntryOf(uintptr_t aAddress) const noexcept = 0;
    // The pointer a readable data cell holds (a plugin's REL::Relocation of an engine object or function).
    [[nodiscard]] virtual std::optional<uintptr_t> HeldBy(uintptr_t aCell) const noexcept = 0;
    [[nodiscard]] virtual bool IsCode(uintptr_t aAddress) const noexcept = 0;
};

// The engine's objects that matter: this game's per-player objects (the player, its stats, its screen), and the
// screen's objects no function names (the movie player).
struct Objects
{
    const std::unordered_set<uintptr_t>& PerPlayer;
    const std::unordered_set<uintptr_t>& Screen;
    const std::unordered_set<uintptr_t>& Player; // the cells holding the player's pointer (g_thePlayer)
};

// A call through a vtable at Offset in the native's self (r8 at its start) or this game's player: Slot is the
// vtable's byte offset, Second rdx's value when it is a constant.
struct VirtualCall
{
    bool OnPlayer{};
    uintptr_t Offset{};
    uint32_t Slot{};
    std::optional<uint64_t> Second;
    bool operator==(const VirtualCall&) const = default;
};

// What one function reads and calls.
struct Facts
{
    bool Reads{};       // a per-player object
    bool ReadsScreen{}; // a screen object
    std::vector<uintptr_t> Callees;
    std::vector<VirtualCall> Virtuals;
    std::vector<uint32_t> PlayerWrites; // offsets written in the player's object
    std::vector<uint32_t> SelfReads;    // offsets read from the native's self
    std::vector<uintptr_t> Addressed;   // the engine's objects it takes the address of (lea): a functor's vtable it queues
};

// The engine's functions a native's call ends in (a virtual one as the player's vtable resolves it), and the player's
// fields one writes: what each is. A function's Second, when set, is the second argument it must be called with.
struct Sink
{
    Reach Is;
    std::optional<uint64_t> Second;
};
struct Sinks
{
    std::unordered_map<uintptr_t, Sink> Functions;
    std::unordered_map<uint32_t, Sink> PlayerFields;
    std::unordered_map<uint32_t, Sink> SelfFields; // read by a getter: a native making no call
    std::unordered_map<uintptr_t, Sink> Objects;   // addressed: a functor's vtable (its call runs after the native)
};

// A function without unwind data (a leaf) is read until it returns, or this far.
inline constexpr uintptr_t kLeafBytes = 0x400;

// Reads the function at aFunction (in acModule) over each of its unwind entries: the objects its operands address,
// and the functions it calls or tail-jumps to. In a plugin (aThroughCells), an object or function is also reached
// through the cell holding its address.
[[nodiscard]] Facts Read(uintptr_t aFunction, const Range& acModule, bool aThroughCells, const Code& acCode,
                         const Objects& acObjects) noexcept;

// The native's own calls (not its callees': Actor.Kill reaches the controls deep in SetLifeState) of the engine's
// functions acSinks names, a player's field it writes, or a screen object it reads. A virtual call is resolved by
// aResolve (none: unknown). Progression comes first, then the world, items (an equip's HUD message is no screen's), the
// body, the controls, the screen, a show, a question (a getter's field: only for a native making no call).
template <class TResolve>
[[nodiscard]] Reach ClassifyCall(const Facts& acFacts, const Sinks& acSinks, TResolve&& aResolve) noexcept
{
    Reach progress, world, item, playerMenu, body, controls, screen, show, question;
    if (acFacts.ReadsScreen)
        screen.Kind = Call::kScreen;
    const auto cNote = [&](const Sink& acSink) {
        (acSink.Is.Kind == Call::kProgress ? progress
         : acSink.Is.Kind == Call::kWorld  ? world
         : acSink.Is.Kind == Call::kItem   ? item
         : acSink.Is.Kind == Call::kPlayerMenu ? playerMenu
         : acSink.Is.Kind == Call::kBody   ? body
         : acSink.Is.Kind == Call::kControls ? controls
         : acSink.Is.Kind == Call::kShow   ? show
         : acSink.Is.Kind == Call::kQuestion ? question
                                           : screen) = acSink.Is;
    };
    for (const uintptr_t cCallee : acFacts.Callees)
        if (const auto it = acSinks.Functions.find(cCallee); it != acSinks.Functions.end() && !it->second.Second)
            cNote(it->second);
    for (const VirtualCall& cCall : acFacts.Virtuals)
        if (const std::optional<uintptr_t> cTarget = aResolve(cCall))
            if (const auto it = acSinks.Functions.find(*cTarget);
                it != acSinks.Functions.end() && (!it->second.Second || it->second.Second == cCall.Second))
                cNote(it->second);
    for (const uint32_t cField : acFacts.PlayerWrites)
        if (const auto it = acSinks.PlayerFields.find(cField); it != acSinks.PlayerFields.end())
            cNote(it->second);
    for (const uintptr_t cObject : acFacts.Addressed)
        if (const auto it = acSinks.Objects.find(cObject); it != acSinks.Objects.end())
            cNote(it->second);
    if (acFacts.Callees.empty() && acFacts.Virtuals.empty())
        for (const uint32_t cField : acFacts.SelfReads)
            if (const auto it = acSinks.SelfFields.find(cField); it != acSinks.SelfFields.end())
                cNote(it->second);
    for (const Reach* cpReach : {&progress, &world, &item, &playerMenu, &body, &controls, &screen, &show, &question})
        if (cpReach->Kind != Call::kNone)
            return *cpReach;
    return {};
}
// No virtual call resolved.
[[nodiscard]] inline Reach ClassifyCall(const Facts& acFacts, const Sinks& acSinks) noexcept
{
    return ClassifyCall(acFacts, acSinks, [](const VirtualCall&) { return std::optional<uintptr_t>{}; });
}

// Calls followed from a native's function: deeper reaches unrelated engine code (papyrus_natives.py, depth 2-6).
inline constexpr int kDepth = 3;

// Whether aStart, or a function it calls within aDepth calls, reads a per-player object (aFactsOf(function)), following
// only calls aFollows (the engine's and the native's own plugin, never imports or the CRT). Each function once.
template <class TFactsOf, class TFollows>
[[nodiscard]] bool Reaches(uintptr_t aStart, TFactsOf&& aFactsOf, TFollows&& aFollows, int aDepth = kDepth)
{
    std::unordered_set<uintptr_t> seen{aStart};
    std::vector<uintptr_t> level{aStart};
    for (int depth = 0; depth <= aDepth && !level.empty(); ++depth)
    {
        std::vector<uintptr_t> next;
        for (const uintptr_t cFunction : level)
        {
            const Facts& cFacts = aFactsOf(cFunction);
            if (cFacts.Reads)
                return true;
            for (const uintptr_t cCallee : cFacts.Callees)
                if (aFollows(cCallee) && seen.insert(cCallee).second)
                    next.push_back(cCallee);
        }
        level = std::move(next);
    }
    return false;
}
} // namespace NativeReach
