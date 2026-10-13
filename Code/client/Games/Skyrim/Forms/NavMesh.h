#pragma once

#include <Forms/TESForm.h>

// A cell's walkable floor as triangles (CommonLibSSE-NG NavMesh: TESForm, TESChildCell, BSNavmesh at 0x28).
struct BSNavmeshTriangle
{
    uint16_t vertices[3];
    uint16_t triangles[3]; // neighbours, 0xFFFF none
    uint16_t triangleFlags;
    uint16_t traversalFlags;
};
static_assert(sizeof(BSNavmeshTriangle) == 0x10);

struct NavMesh : TESForm
{
    uint8_t pad20[0x38 - 0x20];
    GameArray<NiPoint3> vertices;            // BSNavmesh +0x10
    GameArray<BSNavmeshTriangle> triangles; // BSNavmesh +0x28
};
static_assert(offsetof(NavMesh, vertices) == 0x38);
static_assert(offsetof(NavMesh, triangles) == 0x50);

// TESObjectCELL::navMeshes (BSTArray<BSTSmartPointer<NavMesh>>).
struct NavMeshArray
{
    GameArray<NavMesh*> navMeshes;
};
static_assert(sizeof(NavMeshArray) == 0x18);
