#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>

// Engine bug fix: a cell change in the middle of a frame (coc, a teleport out of an interior) can
// crash the renderer on the cell's freed portals (docs/re/crashes/2026-10-11-qasmoke-exit-portal-graph.md).
//
// Each frame, Main::Update fills the camera's BSPortalGraphEntry with the rooms around the camera
// (TES::sub_13408). Unloading an interior swaps the scene's portal graph
// (ShadowSceneNode::SetPortalGraph) and deletes the cell's portals, but leaves the entry's rooms
// as they were; the same frame's Main::Draw then culls through them and reads deleted portals.
// When the graph changes, the entry's rooms go with it: the next frame's refresh fills them again
// from the new cell.
//
// Layouts from CommonLibSSE (BSPortalGraphEntry.h, ShadowSceneNode.h), IDs from the Address Library.

namespace
{
struct ShadowSceneNode;

struct RoomList // BSTArray<NiPointer<BSMultiBoundRoom>>
{
    NiRefObject** pData;
    uint32_t Capacity;
    uint32_t Pad;
    uint32_t Size;
};

struct BSPortalGraphEntry
{
    void* pVTable;
    uint32_t RefCount;
    uint32_t Pad;
    void* pPortalGraph;    // 10
    RoomList Rooms;        // 18 multiBoundRoomAccumList
    NiRefObject* pRoot;    // 30 multiBoundRoomRoot
};
static_assert(offsetof(BSPortalGraphEntry, Rooms) == 0x18 && offsetof(BSPortalGraphEntry, pRoot) == 0x30);
static_assert(offsetof(RoomList, Size) == 0x10);

constexpr size_t cPortalGraph = 0x228;        // ShadowSceneNode::portalGraph (CommonLibSSE)
constexpr size_t cCameraEntry = 0x30190;      // the accumulating culler's BSPortalGraphEntry (TES::sub_13408)

using TSetPortalGraph = void(ShadowSceneNode*, void* apPortalGraph);
TSetPortalGraph* RealSetPortalGraph = nullptr;

// The camera's portal graph entry, or null if it is not one (checked by its vtable).
BSPortalGraphEntry* CameraEntry() noexcept
{
    POINTER_SKYRIMSE(uint8_t**, s_culler, 415022);      // -> the accumulating culling process
    POINTER_SKYRIMSE(void, s_entryVTable, 238263);      // BSPortalGraphEntry's vtable
    uint8_t** ppCuller = *s_culler.Get();
    const uint8_t* pCuller = ppCuller ? *ppCuller : nullptr;
    if (!pCuller)
        return nullptr;
    auto* pEntry = *reinterpret_cast<BSPortalGraphEntry* const*>(pCuller + cCameraEntry);
    return pEntry && pEntry->pVTable == s_entryVTable.Get() ? pEntry : nullptr;
}

void Release(NiRefObject* apObject) noexcept
{
    if (apObject)
        apObject->DecRef();
}

void HookSetPortalGraph(ShadowSceneNode* apThis, void* apPortalGraph)
{
    const bool cChanged = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(apThis) + cPortalGraph) != apPortalGraph;
    RealSetPortalGraph(apThis, apPortalGraph);
    if (!cChanged)
        return;

    BSPortalGraphEntry* pEntry = CameraEntry();
    if (!pEntry)
    {
        static bool s_warned = false;
        if (!std::exchange(s_warned, true))
            spdlog::warn("Portal graph fix: the camera's BSPortalGraphEntry was not found; stale rooms are left as they are");
        return;
    }
    spdlog::debug("Portal graph fix: graph changed, {} stale room(s) dropped", pEntry->Rooms.Size);

    // Its rooms (and their visibility) belong to the graph that is going away.
    using TClearVisibility = void(BSPortalGraphEntry*);
    POINTER_SKYRIMSE(TClearVisibility, s_clearVisibility, 76119);
    (s_clearVisibility.Get())(pEntry);

    for (uint32_t i = 0; i < pEntry->Rooms.Size; ++i)
        Release(std::exchange(pEntry->Rooms.pData[i], nullptr));
    pEntry->Rooms.Size = 0;

    // As BSPortalGraphEntry::SetPortalGraph: the new graph, no root room yet.
    pEntry->pPortalGraph = apPortalGraph;
    Release(std::exchange(pEntry->pRoot, nullptr));
}
} // namespace

static TiltedPhoques::Initializer s_portalGraphFix(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSetPortalGraph, s_setPortalGraph, 106385);
        RealSetPortalGraph = s_setPortalGraph.Get();
        TP_HOOK(&RealSetPortalGraph, HookSetPortalGraph);
    });
