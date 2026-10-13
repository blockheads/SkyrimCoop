#pragma once

struct BShkbAnimationGraph;

// The variables a behaviour graph writes itself (its outputs), found in the graph's own nodes: bindings to
// output members, state machine sync variables, expression assignments. Read by the harness's graph_writers;
// docs/design/graph-replication.md.
namespace GraphVariableWriters
{
struct Writer
{
    TiltedPhoques::String Variable;  // by the writing behaviour's own variable names
    TiltedPhoques::String Behaviour; // the behaviour graph's name ("0_Master.hkb")
    TiltedPhoques::String Node;      // "Class Name.member", "Class Name syncVariable", "Class Name 'expr'"
    int32_t Index;                   // the variable index as the node holds it
};

// Walks every node of the graph and of the behaviours it references, active or not.
TiltedPhoques::Vector<Writer> Collect(const BShkbAnimationGraph* apGraph) noexcept;
} // namespace GraphVariableWriters
