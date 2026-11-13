#pragma once

#include <cstdint>
#include <span>
#include <string>

struct BShkbAnimationGraph;
struct hkEventContext;

// A behaviour graph's whole state through Havok's own save and restore (docs/re/systems/animation-replay-recovery.md,
// A graph's whole state): what a sync point carries (Games/Animation/GraphState.h).
namespace GraphInternalState
{
// The owner's (hkbBehaviorGraph::getInternalState 58372), with its BS nodes' runtime members.
[[nodiscard]] std::string Take(const BShkbAnimationGraph& acGraph);
// Onto a copy's graph: deactivated, then setInternalState (58373) on the owner's words. False: not restored.
[[nodiscard]] bool Give(BShkbAnimationGraph& aGraph, hkEventContext& aContext, const std::string& acState,
                        std::span<const uint32_t> aWords);
} // namespace GraphInternalState
