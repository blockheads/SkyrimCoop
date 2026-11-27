#pragma once

#include <VersionDb.h>

namespace internal
{
template <class T> struct RttiLocator
{
    RttiLocator(uint32_t aId);
    static const void* Get();

    static inline std::unique_ptr<VersionDbPtr<void*>> m_pRtti;
};

using TDynamicCast = void*(void* apThis, uint32_t a1, const void* acpBaseRTTI, const void* acpTargetRTTI, uint32_t a4);
extern const VersionDbPtr<TDynamicCast> DynamicCast;
} // namespace internal

// The engine's dynamic cast (109689) between the types listed below. A type not listed fails to link: add its two
// lines here and in RTTI.cpp, its ID from CommonLibSSE-NG's Offsets_RTTI.h (RTTI_<name>).
template <class T, class U> T* Cast(U* apPtr)
{
    return reinterpret_cast<T*>(internal::DynamicCast.Get()((void*)apPtr, 0, internal::RttiLocator<std::remove_cv_t<U>>::Get(), internal::RttiLocator<std::remove_cv_t<T>>::Get(), 0));
}

struct TESForm;
extern template struct internal::RttiLocator<TESForm>;
struct SpellItem;
extern template struct internal::RttiLocator<SpellItem>;
struct AlchemyItem;
extern template struct internal::RttiLocator<AlchemyItem>;
struct MagicItem;
extern template struct internal::RttiLocator<MagicItem>;
struct TESBoundObject;
extern template struct internal::RttiLocator<TESBoundObject>;
struct EffectSetting;
extern template struct internal::RttiLocator<EffectSetting>;
struct BGSPerk;
extern template struct internal::RttiLocator<BGSPerk>;
struct EnchantmentItem;
extern template struct internal::RttiLocator<EnchantmentItem>;
struct IngredientItem;
extern template struct internal::RttiLocator<IngredientItem>;
struct TESGlobal;
extern template struct internal::RttiLocator<TESGlobal>;
struct BSExtraData;
extern template struct internal::RttiLocator<BSExtraData>;
struct ExtraCount;
extern template struct internal::RttiLocator<ExtraCount>;
struct ExtraHealth;
extern template struct internal::RttiLocator<ExtraHealth>;
struct ExtraCharge;
extern template struct internal::RttiLocator<ExtraCharge>;
struct ExtraPoison;
extern template struct internal::RttiLocator<ExtraPoison>;
struct ExtraFactionChanges;
extern template struct internal::RttiLocator<ExtraFactionChanges>;
struct ExtraEnchantment;
extern template struct internal::RttiLocator<ExtraEnchantment>;
struct TESWorldSpace;
extern template struct internal::RttiLocator<TESWorldSpace>;
struct TESObjectCELL;
extern template struct internal::RttiLocator<TESObjectCELL>;
struct TESObjectREFR;
extern template struct internal::RttiLocator<TESObjectREFR>;
struct TESAmmo;
extern template struct internal::RttiLocator<TESAmmo>;
struct TESPackage;
extern template struct internal::RttiLocator<TESPackage>;
struct TESTopicInfo;
extern template struct internal::RttiLocator<TESTopicInfo>;
struct TESTopic;
extern template struct internal::RttiLocator<TESTopic>;
struct TESFaction;
extern template struct internal::RttiLocator<TESFaction>;
struct TESActorBase;
extern template struct internal::RttiLocator<TESActorBase>;
struct BGSScene;
extern template struct internal::RttiLocator<BGSScene>;
struct TESQuest;
extern template struct internal::RttiLocator<TESQuest>;
struct BGSKeyword;
extern template struct internal::RttiLocator<BGSKeyword>;
struct ExtraSoul;
extern template struct internal::RttiLocator<ExtraSoul>;
struct NiAVObject;
extern template struct internal::RttiLocator<NiAVObject>;
struct NiNode;
extern template struct internal::RttiLocator<NiNode>;
struct Actor;
extern template struct internal::RttiLocator<Actor>;
struct TESObjectWEAP;
extern template struct internal::RttiLocator<TESObjectWEAP>;
struct TESObjectACTI;
extern template struct internal::RttiLocator<TESObjectACTI>;
struct TESNPC;
extern template struct internal::RttiLocator<TESNPC>;
struct TESIdleForm;
extern template struct internal::RttiLocator<TESIdleForm>;
struct TESObjectARMO;
extern template struct internal::RttiLocator<TESObjectARMO>;
struct TESLevItem;
extern template struct internal::RttiLocator<TESLevItem>;
struct TESRegion;
extern template struct internal::RttiLocator<TESRegion>;
struct bhkWorldObject;
extern template struct internal::RttiLocator<bhkWorldObject>;
struct bhkRigidBody;
extern template struct internal::RttiLocator<bhkRigidBody>;
struct BSGeometry;
extern template struct internal::RttiLocator<BSGeometry>;
struct BGSLocation;
extern template struct internal::RttiLocator<BGSLocation>;
struct TESWeather;
extern template struct internal::RttiLocator<TESWeather>;
struct MagicCaster;
extern template struct internal::RttiLocator<MagicCaster>;
struct BGSAction;
extern template struct internal::RttiLocator<BGSAction>;
struct NiBillboardNode;
extern template struct internal::RttiLocator<NiBillboardNode>;
struct BSTempEffect;
extern template struct internal::RttiLocator<BSTempEffect>;
struct ValueModifierEffect;
extern template struct internal::RttiLocator<ValueModifierEffect>;
struct ActiveEffect;
extern template struct internal::RttiLocator<ActiveEffect>;
struct ActorMagicCaster;
extern template struct internal::RttiLocator<ActorMagicCaster>;
struct ModelReferenceEffect;
extern template struct internal::RttiLocator<ModelReferenceEffect>;
struct ReferenceEffect;
extern template struct internal::RttiLocator<ReferenceEffect>;
struct ShaderReferenceEffect;
extern template struct internal::RttiLocator<ShaderReferenceEffect>;
struct Projectile;
extern template struct internal::RttiLocator<Projectile>;
struct PlayerCharacter;
extern template struct internal::RttiLocator<PlayerCharacter>;
struct bhkRigidBodyT;
extern template struct internal::RttiLocator<bhkRigidBodyT>;
struct BSShaderProperty;
extern template struct internal::RttiLocator<BSShaderProperty>;
struct BSLightingShaderProperty;
extern template struct internal::RttiLocator<BSLightingShaderProperty>;
