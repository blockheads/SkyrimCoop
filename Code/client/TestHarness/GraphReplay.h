#pragma once

#include <nlohmann/json.hpp>

struct BShkbAnimationGraph;
struct BSFixedString;

// The graph input replay proofs (docs/design/graph-input-replay.md): an owner graph's inputs (variables, events, steps)
// recorded and replayed into a second graph from the owner's sync point, compared update by update. On one game, into a
// second actor of the same base; over the network, into the owner's copy on a guest, the harness carrying the ops.
namespace GraphReplay
{
// {"cmd": "graph_replay", "arm_like": ref}: the next two temporary actors of that reference's base become the owner and
// the shadow. {"record": id} (host) / {"replay": copy} (guest): the network proof's two ends; {"take": true} hands out the
// host's new ops, states and sync point, {"feed": taken} gives them to the guest, {"states_at": update} reads the owner's.
// {"cmd": "graph_replay"}: the comparison so far. {"stop": true}: ends it.
nlohmann::json Command(const nlohmann::json& acRequest) noexcept;
// From the SendEvent hook (63591), before the event is handled: records an owner's, and returns true to drop one the
// shadow's engine sent it.
bool DropEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent) noexcept;
} // namespace GraphReplay
