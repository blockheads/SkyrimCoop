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
// music, video), the party's (docs/design/cutscenes.md); or the world (a quest's stage), the host's to change.
enum class Call : uint8_t
{
    kNone,
    kControls,
    kScreen,
    kWorld,
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
};

// What one function reads and calls.
struct Facts
{
    bool Reads{};       // a per-player object
    bool ReadsScreen{}; // a screen object
    std::vector<uintptr_t> Callees;
};

// A function without unwind data (a leaf) is read until it returns, or this far.
inline constexpr uintptr_t kLeafBytes = 0x400;

// Reads the function at aFunction (in acModule) over each of its unwind entries: the objects its operands address,
// and the functions it calls or tail-jumps to. In a plugin (aThroughCells), an object or function is also reached
// through the cell holding its address.
[[nodiscard]] Facts Read(uintptr_t aFunction, const Range& acModule, bool aThroughCells, const Code& acCode,
                         const Objects& acObjects) noexcept;

// The native's own calls (not its callees': Actor.Kill reaches the controls deep in SetLifeState) of the engine's
// functions acCalls names, or a screen object it reads. The world comes first, then the controls, then the screen.
[[nodiscard]] Call ClassifyCall(const Facts& acFacts, const std::unordered_map<uintptr_t, Call>& acCalls) noexcept;

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
