#pragma once

#include <nlohmann/json.hpp>

// The graph input stream's live check (docs/design/graph-input-stream.md): each streamed graph's states hashed after
// every update, by its owner's update index, on both games; the harness compares a copy's with its owner's.
namespace GraphStreamCheck
{
// Chains a layer under the stream's 63587 hook: after the hooks are in (the harness's first poll).
void Install() noexcept;
// {"cmd": "graph_stream"}: each streamed actor here (form, actor id, owner or copy, on the stream) and, with "take",
// its hashes since the last take as [update, hash]. {"states_at": {"form": f, "update": u}}: the states hashed then.
nlohmann::json Command(const nlohmann::json& acRequest) noexcept;
} // namespace GraphStreamCheck
