#include "SkinReads.h"
#include "EngineCalls.h"

#include <Actor.h>
#include <NetImmerse/BSGeometry.h>
#include <NetImmerse/NiNode.h>
#include <NetImmerse/NiSkinInstance.h>
#include <RTTI.h>
#include <VersionDb.h>

#include <unordered_set>

namespace TestHarness
{
namespace
{
constexpr uint64_t cHashSeed = 0xCBF29CE484222325ull;

uint64_t Hash(uint64_t aSeed, const void* apData, size_t aSize)
{
    for (size_t i = 0; i < aSize; ++i)
        aSeed = (aSeed ^ static_cast<const uint8_t*>(apData)[i]) * 0x100000001B3ull;
    return aSeed;
}
} // namespace

nlohmann::json ReadSkins(uint32_t aFormId)
{
    auto* pActor = Cast<Actor>(TESForm::GetById(aFormId));
    NiNode* pRoot = pActor ? pActor->GetNiNode() : nullptr;
    if (!pRoot)
        return nullptr;
    std::vector<NiAVObject*> objects{pRoot};
    std::unordered_set<const void*> nodesHere, transformsHere;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        nodesHere.insert(objects[i]);
        transformsHere.insert(&objects[i]->world);
        if (auto* pNode = Cast<NiNode>(objects[i]))
            for (uint16_t child = 0; child < pNode->children.length; ++child)
                if (auto* pChild = pNode->children.data[child])
                    objects.push_back(pChild);
    }
    POINTER_SKYRIMSE(uint32_t, s_frame, 411489); // gFrameCounter
    auto skins = nlohmann::json::array();
    for (NiAVObject* pObject : objects)
    {
        auto* pGeometry = Cast<BSGeometry>(pObject);
        auto* pSkin = pGeometry ? static_cast<NiSkinInstance*>(pGeometry->skinInstance) : nullptr;
        if (!pSkin || !pSkin->skinData)
            continue;
        EnterCriticalSection(&pSkin->lock);
        const uint32_t cCount = pSkin->skinData->boneCount;
        uint32_t foreignBones = 0, ownTransforms = 0, transformsOfHere = 0, noTransform = 0;
        const bool cMatrices = pSkin->boneMatrices && pSkin->matrixCount * 48 <= pSkin->allocatedSize;
        std::vector<uint32_t> boneNodes, boneDrawn; // 0: not this 3D's, no matrix
        for (uint32_t i = 0; i < cCount; ++i)
        {
            // Read only when it is a node of this actor's 3D: a stale one may be freed.
            NiAVObject* pBone = pSkin->bones ? pSkin->bones[i] : nullptr;
            const auto* pTransform = pSkin->boneWorldTransforms ? pSkin->boneWorldTransforms[i] : nullptr;
            const bool cHere = pBone && nodesHere.contains(pBone);
            foreignBones += pBone && !cHere;
            noTransform += !pTransform;
            ownTransforms += cHere && pTransform == &pBone->world;
            transformsOfHere += pTransform && transformsHere.contains(pTransform);
            boneNodes.push_back(cHere ? uint32_t(Hash(cHashSeed, &pBone->world, sizeof(pBone->world))) | 1u : 0u);
            boneDrawn.push_back(cMatrices && i < pSkin->matrixCount ? uint32_t(Hash(cHashSeed, pSkin->boneMatrices + i * 12, 48)) | 1u : 0u);
        }
        skins.push_back({{"geometry", pObject->name ? pObject->name : ""},
                         {"bones", cCount},
                         {"foreign_bones", foreignBones},
                         {"own_transforms", ownTransforms},
                         {"transforms_here", transformsOfHere},
                         {"no_transform", noTransform},
                         {"root_here", nodesHere.contains(pSkin->rootParent)},
                         {"frames_behind", *s_frame.Get() - pSkin->frameId},
                         {"on_screen", EngineCalls::IsOnScreen(*pObject)},
                         {"bone_nodes", boneNodes},
                         {"bone_drawn", boneDrawn},
                         {"flags", pObject->flags}});
        LeaveCriticalSection(&pSkin->lock);
    }
    return {{"frame", *s_frame.Get()}, {"root_flags", pRoot->flags}, {"skins", skins}};
}
} // namespace TestHarness
