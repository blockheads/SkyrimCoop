#pragma once

#include <nlohmann/json.hpp>

struct BShkbAnimationGraph;
struct BSFixedString;

// The graph input replay proof (docs/design/graph-input-replay.md): two fresh actors of one base on this game; the
// first's graph inputs (variables, events, steps) are recorded and replayed into the second's, compared update by update.
namespace GraphReplay
{
// {"cmd": "graph_replay", "arm_like": ref}: the next two temporary actors of that reference's base become the owner and
// the shadow. {"cmd": "graph_replay"}: the comparison so far. {"stop": true}: ends it.
nlohmann::json Command(const nlohmann::json& acRequest) noexcept;
// From the SendEvent hook (63591), before the event is handled: records an owner's, and returns true to drop one the
// shadow's engine sent it.
bool DropEvent(BShkbAnimationGraph* apGraph, const BSFixedString* apEvent) noexcept;
} // namespace GraphReplay
