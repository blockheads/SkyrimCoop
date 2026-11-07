#pragma once

// The engine's navmesh pathing structs the harness reads (CommonLibSSE-NG BSPathingLocation.h, BSNavmeshInfoMap.h,
// BSPathing.h; docs/re/systems/pathing.md).

#include "Games/Primitives.h"

// One navmesh triangle: its corners, the triangle across each edge (0xFFFF none) and flags.
struct BSNavmeshTriangle
{
    enum : uint16_t
    {
        kEdge0Link = 1 << 0, // the edge's neighbour is in another navmesh (extraEdgeInfo)
        kEdge1Link = 1 << 1,
        kEdge2Link = 1 << 2,
        kDeleted = 1 << 3,
    };
    uint16_t vertices[3];  // 00
    uint16_t triangles[3]; // 06
    uint16_t flags;        // 0C
    uint16_t traversal;    // 0E
};
static_assert(sizeof(BSNavmeshTriangle) == 0x10);

// What a linked edge leads to (BSNavmeshEdgeExtraInfo): a portal to a triangle, maybe in another navmesh, or a ledge.
struct BSNavmeshEdgeExtraInfo
{
    enum : uint32_t
    {
        kPortal = 0,
        kLedgeUp = 1,
        kLedgeDown = 2,
        kEnableDisablePortal = 3,
    };
    uint32_t type;        // 00
    uint32_t otherMeshId; // 04
    uint16_t triangle;    // 08
    int8_t edgeIndex;     // 0A
    uint8_t pad0B;
};
static_assert(sizeof(BSNavmeshEdgeExtraInfo) == 0xC);

// A door the navmesh crosses (PathingDoor, 0x78): its reference by handle.
struct PathingDoor
{
    void* vtable;       // 00
    uint32_t refCount;  // 08
    uint32_t unk0C;
    uint32_t unk10;     // 10
    uint32_t refHandle; // 14
};
static_assert(offsetof(PathingDoor, refHandle) == 0x14);

// A triangle on a door (BSNavmeshTriangleDoorPortal).
struct BSNavmeshDoorPortal
{
    PathingDoor* door;            // 00: BSTSmartPointer<BSPathingDoor>
    uint16_t owningTriangleIndex; // 08
    uint16_t pad0A;
    uint32_t pad0C;
};
static_assert(sizeof(BSNavmeshDoorPortal) == 0x10);

// A door on a triangle the search treats as closed (BSNavmeshClosedDoorInfo).
struct BSNavmeshClosedDoor
{
    PathingDoor* door;      // 00: BSTSmartPointer<BSPathingDoor>
    uint16_t triangleIndex; // 08
    uint16_t pad0A;
    uint32_t pad0C;
};
static_assert(sizeof(BSNavmeshClosedDoor) == 0x10);

// The engine's array header (BSTArray): data, capacity, size.
template <class T> struct BSNavmeshArray
{
    T* data;           // 00
    uint32_t capacity; // 08
    uint32_t pad0C;
    uint32_t size;     // 10
    uint32_t pad14;
};
static_assert(sizeof(BSNavmeshArray<int>) == 0x18);

struct BSNavmesh
{
    void* vtable;                                     // 000
    uint32_t refCount;                                // 008
    uint32_t pad00C;                                  // 00C
    BSNavmeshArray<NiPoint3> vertices;                // 010
    BSNavmeshArray<BSNavmeshTriangle> triangles;      // 028
    BSNavmeshArray<BSNavmeshEdgeExtraInfo> extraEdgeInfo; // 040
    BSNavmeshArray<BSNavmeshDoorPortal> doorPortals;  // 058
    BSNavmeshArray<BSNavmeshClosedDoor> closedDoors;  // 070
    BSNavmeshArray<uint8_t> coverArray;               // 088
    uint8_t meshGrid[0x30];                           // 0A0
    BSNavmeshArray<void*> obstacles;                  // 0D0
};
static_assert(offsetof(BSNavmesh, triangles) == 0x28);
static_assert(offsetof(BSNavmesh, closedDoors) == 0x70);
static_assert(offsetof(BSNavmesh, obstacles) == 0xD0);

struct BSNavmeshInfo
{
    uint32_t navMeshId;    // 00: the NavMesh record's form id
    uint8_t pad04[0x28 - 0x04];
    BSNavmesh* navMesh;    // 28: loaded with its cell
};
static_assert(offsetof(BSNavmeshInfo, navMesh) == 0x28);

// A place for a path: a point, the cell's navmeshes, and once resolved one navmesh's triangle.
struct BSPathingLocation
{
    NiPoint3 location;          // 00
    BSNavmeshInfo* navMeshInfo; // 10: set by ResolveToClosestNavmeshAndTriangle (90369)
    void* navMeshInfoArray;     // 18
    void* pathingCell;          // 20: BSTSmartPointer<BSPathingCell>, set by the ctor (90346)
    uint16_t triangle;          // 28: 0xFFFF unresolved
    uint8_t flags;              // 2A
    uint8_t clientData;         // 2B
};
static_assert(sizeof(BSPathingLocation) == 0x30);
static_assert(offsetof(BSPathingLocation, triangle) == 0x28);

// FindTriangleForLocationFilter: the triangle lookup asks it whether a candidate counts.
struct FindTriangleForLocationFilter
{
    virtual ~FindTriangleForLocationFilter() = default;
    virtual bool IsValidTri(NiPoint3* apLocation, void* apNavMesh, uint16_t aTriangle, NiPoint3* apPointOnTriangle) = 0;
};

// The request a path search answers (PathingRequest, 0x100 from Actor::RequestPathToReference 37893; ctor 30875):
// refcounted, freed by its deleting destructor (vtable slot 0) when the count drops to 0.
struct PathingRequest
{
    void (**vtable)(PathingRequest*, uint32_t); // 00
    volatile int32_t refCount;                  // 08
    uint8_t pad0C[0x100 - 0x0C];
};
static_assert(offsetof(PathingRequest, refCount) == 0x08);
static_assert(sizeof(PathingRequest) == 0x100);

// The search's answer (BSPathingSolution, 0xB0, refcounted): navmesh-level nodes, then the triangles' path.
// A path's points (CommonLib BSPathing.h; BSPathingSolution::dtor 90962 steps 0x38 and 0x48).
struct BSVirtualPathingNode
{
    BSPathingLocation location; // 00
    void* actionAtNode;         // 30
};
static_assert(sizeof(BSVirtualPathingNode) == 0x38);

struct BSPathingNode
{
    BSPathingLocation location; // 00
    void* actionAtNode;         // 30
    NiPoint3 tangent;           // 38
};
static_assert(sizeof(BSPathingNode) == 0x48);

struct BSPathingSolution
{
    uint32_t refCount;                    // 00
    uint32_t pad04;                       // 04
    uint32_t virtualNodesCapacity;       // 08: BSTSmallArray<BSVirtualPathingNode, 2>
    uint8_t virtualNodes[0x80 - 0x0C];    // 0C: pointer at 10, or inline
    uint32_t virtualNodeCount;            // 80
    uint8_t pad84[0x88 - 0x84];           // 84
    int32_t firstLoadedVirtualNodeIndex;  // 88
    int32_t lastLoadedVirtualNodeIndex;   // 8C
    BSPathingNode* pathingNodes;          // 90: BSTArray<BSPathingNode>, the loaded stretch
    uint32_t pathingNodesCapacity;        // 98
    uint8_t pad9C[0xA0 - 0x9C];           // 9C
    uint32_t pathingNodeCount;            // A0
    uint8_t padA4[0xA8 - 0xA4];           // A4
    bool incompletePath;                  // A8
    bool requestGoalUnused;               // A9
};
static_assert(offsetof(BSPathingSolution, virtualNodeCount) == 0x80);
static_assert(offsetof(BSPathingSolution, pathingNodeCount) == 0xA0);
static_assert(offsetof(BSPathingSolution, incompletePath) == 0xA8);
static_assert(offsetof(BSPathingSolution, pathingNodes) == 0x90);
static_assert(sizeof(BSPathingSolution) == 0xB0);

// One reference's cut of navmeshes (the obstacle map's value): per cut, the navmeshes it touches.
struct NavmeshObstacleCut
{
    uint8_t pad00[0xA0];
    BSNavmeshArray<BSNavmeshInfo*> navMeshes; // A0
};
static_assert(offsetof(NavmeshObstacleCut, navMeshes) == 0xA0);

struct NavmeshObstacle
{
    using TRelease = void(NavmeshObstacle*);
    TRelease** vtable;                        // 00: slot 1 frees it once its count drops to 0 (PrintObstacles 30514)
    volatile long refCount;                   // 08
    uint8_t pad0C[0x18 - 0x0C];
    BSNavmeshArray<NavmeshObstacleCut*> cuts; // 18
};
static_assert(offsetof(NavmeshObstacle, refCount) == 0x08);
static_assert(offsetof(NavmeshObstacle, cuts) == 0x18);

struct NavmeshObstacleEntry
{
    uint32_t refFormId;                 // 00: the obstacle reference (LookupFormByID)
    uint32_t pad04;
    NavmeshObstacle* obstacle;          // 08
    NavmeshObstacleEntry* next;         // 10: null in an empty slot
};
static_assert(sizeof(NavmeshObstacleEntry) == 0x18);

// NavMeshObstacleManager (singleton 400442; ctor 30441, "Current Obstacles" 30514): references cutting navmeshes.
struct NavMeshObstacleManager
{
    void* vtable;                      // 00
    uint8_t pad08[0x1C - 0x08];
    uint32_t capacity;                 // 1C: BSTHashMap<refFormId, obstacle> at 10
    uint8_t pad20[0x38 - 0x20];
    NavmeshObstacleEntry* entries;     // 38
};
static_assert(offsetof(NavMeshObstacleManager, capacity) == 0x1C);
static_assert(offsetof(NavMeshObstacleManager, entries) == 0x38);
