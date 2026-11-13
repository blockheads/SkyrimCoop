#include <Systems/FaceGenSystem.h>

#include <Games/References.h>

#include <Forms/BGSHeadPart.h>
#include <Forms/TESNPC.h>

#include <Components.h>
#include <World.h>

#include <Structs/Tints.h>

#include <Games/Skyrim/NetImmerse/NiTriBasedGeom.h>
#include <Games/Skyrim/NetImmerse/NiRenderedTexture.h>
#include <Games/Skyrim/NetImmerse/BSShaderProperty.h>
#include <Games/Skyrim/NetImmerse/BSLightingShaderProperty.h>
#include <Games/Skyrim/NetImmerse/BSMaskedShaderMaterial.h>
#include <Games/Memory.h>
#include <RTTI.h>
#include <Ui/UiRenderer.h>
#include <VersionDb.h>

__declspec(noinline) NiTriBasedGeom* GetHeadTriBasedGeom(Actor* apActor, uint32_t aPartType)
{
    using TGetObjectByName = NiAVObject*(BSFaceGenNiNode*, const char**, char);
    POINTER_SKYRIMSE(TGetObjectByName, GetObjectByName, 76207);

    BSFaceGenNiNode* pFaceNode = apActor->GetFaceGenNiNode();
    TESNPC* pActorBase = Cast<TESNPC>(apActor->baseForm);

    if (pFaceNode && pActorBase)
    {
        BGSHeadPart* pFacePart = pActorBase->GetCurrentHeadPart(aPartType);

        if (pFacePart)
        {
            NiAVObject* pHeadNode = GetObjectByName(pFaceNode, &pFacePart->name.data, 1);

            if (pHeadNode)
            {
                NiTriBasedGeom* pGeometry = pHeadNode->CastToNiTriBasedGeom();

                if (pGeometry)
                {
                    return pGeometry;
                }
            }
        }
    }

    return nullptr;
}

struct TextureHolder;
using TCreateResourceView = Ni2DBuffer*(TextureHolder*, uint32_t, uint32_t);

using TCreateTexture = NiRenderedTexture*(BSFixedString& aName);
using TCreateTints = void(const GameArray<TintMask*>& acTints, NiRenderedTexture* apTexture);

namespace
{
// The name our painted texture carries: a head built again has the base's FaceTint file instead (TESNPC::BuildHead 24731).
constexpr std::string_view kPaintedName = "SkyrimCoop face tints";

// The face's Facegen material, whose tint texture shows the face tints (TESNPC 24728).
BSMaskedShaderMaterial* GetFaceMaterial(Actor* apActor) noexcept
{
    POINTER_SKYRIMSE(NiRTTI, NiMaskedShaderRTTI, 414675);

    auto pTriBasedGeom = GetHeadTriBasedGeom(apActor, 1);
    if (!pTriBasedGeom)
        return nullptr;

    BSShaderProperty* pShaderProperty = niptr_cast<BSShaderProperty>(pTriBasedGeom->effect);
    if (!pShaderProperty)
        return nullptr;

    BSLightingShaderProperty* pLightingShader = static_cast<BSLightingShaderProperty*>(pShaderProperty);
    if (pLightingShader->GetRTTI() != NiMaskedShaderRTTI.Get())
        return nullptr;

    return static_cast<BSMaskedShaderMaterial*>(pLightingShader->material);
}
} // namespace

bool FaceGenSystem::IsPainted(Actor* apActor) noexcept
{
    const BSMaskedShaderMaterial* cpMaterial = GetFaceMaterial(apActor);
    const NiTexture* cpTexture = cpMaterial ? cpMaterial->renderedTexture.object : nullptr;
    return cpTexture && cpTexture->name && cpTexture->name == kPaintedName;
}

FaceGenSystem::HeadRead FaceGenSystem::Read(Actor* apActor) noexcept
{
    HeadRead read;
    read.Geometry = GetHeadTriBasedGeom(apActor, 1) != nullptr;
    const BSMaskedShaderMaterial* cpMaterial = GetFaceMaterial(apActor);
    read.Material = cpMaterial != nullptr;
    const NiTexture* cpTexture = cpMaterial ? cpMaterial->renderedTexture.object : nullptr;
    if (cpTexture)
        read.Tint = cpTexture->name ? cpTexture->name : "(unnamed)";
    return read;
}

bool FaceGenSystem::Paint(Actor* apActor, const Tints& acTints) noexcept
{
    POINTER_SKYRIMSE(TCreateTexture, CreateTexture, 70717);
    POINTER_SKYRIMSE(TCreateResourceView, CreateResourceView, 77299);
    POINTER_SKYRIMSE(TCreateTints, CreateTints, 27040);
    POINTER_SKYRIMSE(TextureHolder, s_textureHolder, 411393);

    BSMaskedShaderMaterial* pMaterial = GetFaceMaterial(apActor);
    if (!pMaterial)
        return false;

    BSFixedString name(kPaintedName.data());
    auto pTexture = CreateTexture(name);
    pTexture->buffer = CreateResourceView(s_textureHolder.Get(), 512, 512);

    auto& tintsEntries = acTints.Entries;

    GameArray<TintMask*> tints;
    tints.capacity = tints.length = tintsEntries.size() & 0xFFFFFFFF;
    tints.data = (TintMask**)Memory::Allocate(sizeof(TintMask*) * tints.length);

    for (auto i = 0u; i < tints.length; ++i)
    {
        tints[i] = Memory::New<TintMask>();

        tints[i]->alpha = tintsEntries[i].Alpha;
        tints[i]->color = tintsEntries[i].Color;
        tints[i]->type = tintsEntries[i].Type;

        auto pNewTexture = Memory::New<TESTexture>();
        pNewTexture->Construct();
        pNewTexture->Init();

        pNewTexture->name.Set(tintsEntries[i].Name.c_str());

        tints[i]->texture = pNewTexture;
    }

    CreateTints(tints, pTexture);

    for (auto i = 0u; i < tints.length; ++i)
    {
        tints[i]->texture->~TESTexture();
        Memory::Free(tints[i]->texture);
        Memory::Free(tints[i]);
    }

    Memory::Free(tints.data);

    pMaterial->renderedTexture = pTexture;
    return true;
}

void FaceGenSystem::Setup(World& aWorld, const entt::entity aEntity, const Tints& acTints) noexcept
{
    if (acTints.Entries.empty())
        return;

    auto& component = aWorld.emplace_or_replace<FaceGenComponent>(aEntity);
    component.FaceTints = acTints;
}
