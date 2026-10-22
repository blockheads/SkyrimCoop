#pragma once

struct BShkbAnimationGraph;

// The variables a behaviour graph writes itself (its outputs), found in the graph's own nodes: bindings to
// output members, state machine sync variables, expression assignments. Read by the harness's graph_writers;
// docs/design/graph-replication.md.
namespace GraphVariableWriters
{
struct Writer
{
    std::string Variable;  // by the writing behaviour's own variable names
    std::string Behaviour; // the behaviour graph's name ("0_Master.hkb")
    std::string Node;      // "Class Name.member", "Class Name syncVariable", "Class Name 'expr'"
    int32_t Index;                   // the variable index as the node holds it
};

// Walks every node of the graph and of the behaviours it references, active or not.
std::vector<Writer> Collect(const BShkbAnimationGraph* apGraph) noexcept;
} // namespace GraphVariableWriters
