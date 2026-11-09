#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The fake's behaviour graph rules, the same on every game as a behaviour file is (docs/design/graph-input-stream.md,
// Phase 1's seam). A graph is deterministic on its inputs: the variables written into it (its own channels, 39004's
// action writes, snapshot loads), the events sent to it, each update's step, its seed. Each game feeds its own.
struct FakeGraphRules
{
    // A state whose clip ends after Ms with no event (TurnCannedR180): if ExitVariable holds none of ExitValues the
    // exit is not taken and no other end comes (stuck); else the next machine starts at Starts[Integers[StartFrom]]
    // (a bound startStateId, 59467/59405), or Starts[0].
    struct Clip
    {
        std::string State;
        double Ms = 1000.0;
        std::vector<std::string> Starts;
        std::optional<size_t> StartFrom;
        std::optional<size_t> ExitVariable;
        std::vector<int32_t> ExitValues;
    };
    // hkbEventsFromRangeModifier: on each update in State, the graph goes to Above if Floats[Input] >= Bound, else to
    // Below (an empty one: stays). TurnRight_EventsFromRangeMod reads TurnDelta.
    struct Range
    {
        std::string State;
        size_t Input = 0;
        float Bound = 0.f;
        std::string Above;
        std::string Below;
    };
    // A blender whose active child is chosen as it activates, from Integers[ChooseFrom], and kept while it stays
    // active whatever the variable does (Faendal's turn blend and MT_TurnLeft360_Slow). Skyrim's graph save leaves the
    // choice out; Havok's internal state holds it (hkbBlenderGeneratorInternalState, 58373).
    struct Blend
    {
        std::string State;
        size_t ChooseFrom = 0;
        std::vector<std::string> Children;
    };
    std::vector<Clip> Clips;
    std::vector<Range> Ranges;
    std::vector<Blend> Blends;
    // Channels (63590): before each update the engine writes Floats[TurnDelta] from this game's actor, its heading's
    // change since its last update over the step (a copy's: its interpolated heading).
    std::optional<size_t> TurnDeltaChannel;

    [[nodiscard]] const Clip* ClipOf(const std::string& acState) const noexcept
    {
        for (const auto& cClip : Clips)
            if (cClip.State == acState)
                return &cClip;
        return nullptr;
    }
    [[nodiscard]] const Blend* BlendOf(const std::string& acState) const noexcept
    {
        for (const auto& cBlend : Blends)
            if (cBlend.State == acState)
                return &cBlend;
        return nullptr;
    }
};

// One actor's graph memory beyond its state, variables and seed (the actor's GraphState, GraphVariables, GraphRandom).
struct FakeGraphMemory
{
    std::optional<double> ClipStartedAt; // its clip's start, on the actor's AnimationMs
    bool Stuck = false;                  // its clip's exit failed at the end
    bool EndsSent = false;               // an owner's clip-end actions performed for this clip
    int32_t ReadAtStart = -1;            // the clip's StartFrom variable as it started, and as it ended
    int32_t ReadAtEnd = -1;
    float LastHeading = 0.f; // at its last update, for the TurnDelta channel
    bool HeadingKnown = false;
    std::vector<uint32_t> EventWords; // its variables as it took its last event (FakeEngine::GraphWords)
    std::string BlendState;           // the blend state its child was chosen in, and the child
    std::string Child;
    // Its owner's graph time: an owner's own steps; a copy's from the sync point it loaded, then its own steps.
    double Timeline = 0.0;
    // After each update and each event the graph took: (Timeline, state). The live check compares a copy's with its
    // owner's.
    struct Entry
    {
        double Ms = 0.0;
        std::string State;
        // "update 16.7", "event X" ("own ..." a copy's off its stream), "load", "reload" (a new graph), "sync point"
        std::string Input;
    };
    std::vector<Entry> History;
};

// The live check: at every point of a copy's history from its first sync point (its stream's start; all of it without
// one), but its own updates and events off the stream, its state is its owner's at the same owner graph time (the last
// owner entry at or before it). Empty when they
// agree; otherwise the first difference and the inputs before it.
std::string FirstGraphDifference(const std::vector<FakeGraphMemory::Entry>& acOwner,
                                 const std::vector<FakeGraphMemory::Entry>& acCopy);
