#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// A behaviour graph's whole state, as Havok's own restore takes it (hkbBehaviorGraph::getInternalState 58372 /
// setInternalState 58373): each active node's id, node info and internal state, so a copy's graph activates the
// children its owner's nodes chose (docs/design/graph-input-stream.md, Sync points).
namespace GraphState
{
// hkbGeneratorSyncInfo: a node info's first 0x50 bytes (hkbNodeInternalStateInfo +0x10).
struct SyncInfo
{
    struct Point
    {
        int32_t Id = 0;
        float Time = 0.f;
    };
    Point Points[8]{};        // 00
    float BaseFrequency = 0.f; // 40
    float LocalTime = 0.f;     // 44
    float PlaybackSpeed = 0.f; // 48
    int8_t NumSyncPoints = 0;  // 4C
    bool IsCyclic = false;     // 4D
    bool IsMirrored = false;   // 4E
    bool IsAdditive = false;   // 4F
};
static_assert(sizeof(SyncInfo) == 0x50);

struct Node
{
    uint16_t Id = 0;    // the node's id: numbered alike from the same behaviour files (setNodeIds 58369)
    uint32_t Check = 0; // its class's and name's: a copy applies it only to the same node
    SyncInfo Sync;
    bool Activated = false;           // hkbNodeInternalStateInfo::hasActivateBeenCalled
    std::optional<std::string> State; // its internal state's bytes (NodeMemory), when its class has one
    std::string Runtime;              // its plain members' bytes: BS nodes' runtime ones, and what other nodes write
};

// BSCyclicBlendTransitionGenerator's mode (+0xA8): cross-blending (2) or asked to (3), it runs a transition effect
// between two blenders its event handler linked (63133): pointers, which no point carries. A point waits it out.
[[nodiscard]] constexpr bool CarriesCyclicMode(uint8_t aMode) noexcept
{
    return aMode != 2 && aMode != 3;
}

[[nodiscard]] uint32_t Check(std::string_view aClass, std::string_view aName) noexcept;
[[nodiscard]] std::string Encode(const std::vector<Node>& acNodes);
[[nodiscard]] bool Decode(std::string_view aBytes, std::vector<Node>& aNodes);
} // namespace GraphState
