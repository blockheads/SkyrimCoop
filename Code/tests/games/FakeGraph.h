#pragma once

#include <Structs/GraphOps.h>
#include <cstdint>
#include <map>
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
        // BSSynchronizedClipGenerator (63050): its clip advances only while its scene answers that every actor is at
        // its mark. In a pair's instance that takes a moment (FakeEngine::PairMarkMs); outside one its default scene
        // answers yes at once (63048). Outside the graph: a copy on its stream takes its owner's answer.
        bool Synchronized = false;
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
    // Generated: its children follow Integers[ChooseFrom] while it stays active, picked in each generate pass (59540:
    // sortedChildren, numActiveChildren) and taken by the next update's active-node walk, which runs only while the
    // graph's stateOrTransitionChanged is set (the pick sets it; not node state).
    struct Blend
    {
        std::string State;
        size_t ChooseFrom = 0;
        std::vector<std::string> Children;
        bool Generated = false;
        // BSCyclicBlendTransitionGenerator (63132): this event freezes the blend's parameter, a plain member of the
        // node, at the variable's value then; its picks read the member while frozen. No internal state carries it.
        std::string FreezeOn;
        // Its cross-blend event (63133), taken while frozen: it follows the variable again, blending from the frozen
        // pose for this long through an effect and a second blender linked then (pointers: in no sync point).
        std::string CrossBlendOn;
        double CrossBlendMs = 0.0;
    };
    // hkbDampingModifier (TurnDeltaDamped from TurnDelta): its member moves toward Floats[Input] by Gain x its last
    // update's step in each generate pass (63568: modify), which writes it to Floats[Output]; each update writes the
    // member out again (its output binding). So it moves once per generate pass, not per update.
    struct Damp
    {
        size_t Input = 0;
        size_t Output = 1;
        float Gain = 10.f;
    };
    // hkbPoseMatchingGenerator (59583): while State is active its child follows the candidate its game's own ragdoll
    // lies closest to (the actor's LyingPose), read each update. Outside the graph: no variable, event or seed holds it.
    // Activating, it forgets its match and plays no child until it has one (GetUpPoseMatcher, duo 2026-11-11).
    struct Match
    {
        std::string State;
        std::vector<std::string> Children;
    };
    std::vector<Clip> Clips;
    std::vector<Range> Ranges;
    std::vector<Blend> Blends;
    std::vector<Match> Matches;
    std::optional<Damp> Damping;
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
    // A match node's id (Havok numbers every node of a graph, 58369): past the fake's four fixed ones.
    [[nodiscard]] std::optional<uint16_t> MatchNode(const std::string& acState) const noexcept
    {
        for (size_t i = 0; i < Matches.size(); ++i)
            if (Matches[i].State == acState)
                return uint16_t(4 + i);
        return std::nullopt;
    }
    // A synchronized clip's node id: past the match nodes'.
    [[nodiscard]] std::optional<uint16_t> SceneNode(const std::string& acState) const noexcept
    {
        for (size_t i = 0; i < Clips.size(); ++i)
            if (Clips[i].State == acState && Clips[i].Synchronized)
                return uint16_t(100 + i);
        return std::nullopt;
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
    // +0x246: it updated this frame (63587 sets it as it steps, clears it as a far graph's countdown runs), so the
    // generate pass runs (63589). A forced graph never clears it.
    bool Updated = false;
    double LastStepMs = 0.0; // its last update's step, which its modifiers keep for modify
    float Damped = 0.f;      // the damping modifier's member (Damp)
    std::optional<uint32_t> FrozenParameter; // a blend's parameter member, frozen (Blend::FreezeOn)
    double CrossBlendLeftMs = 0.0;   // cross-blending (Blend::CrossBlendOn) while above 0
    std::string PendingChild;        // a Generated blend's child its generate pass picked (its node state)
    std::optional<GraphPick> Picked; // its match node's match in its last update: an owner's recorder sends new ones
    std::map<uint16_t, int32_t> Matches; // each match node's match since it activated
    std::vector<GraphPick> OwnersPicks; // a copy's replay: its owner's picks for the update that follows
    bool ActiveNodesChanged = false; // stateOrTransitionChanged: the next update walks the active nodes
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
