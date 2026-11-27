#include "RTTI.h"
#include <VersionDb.h>

const VersionDbPtr<internal::TDynamicCast> internal::DynamicCast(109689);

namespace internal
{
template <class T> RttiLocator<T>::RttiLocator(uint32_t aId)
{
    m_pRtti = std::make_unique<VersionDbPtr<void*>>(aId);
}

template <class T> const void* RttiLocator<T>::Get()
{
    return *m_pRtti;
}
} // namespace internal

template struct internal::RttiLocator<TESForm>;
internal::RttiLocator<TESForm> registerRtti_TESForm(392216);
template struct internal::RttiLocator<SpellItem>;
internal::RttiLocator<SpellItem> registerRtti_SpellItem(392217);
template struct internal::RttiLocator<AlchemyItem>;
internal::RttiLocator<AlchemyItem> registerRtti_AlchemyItem(392218);
template struct internal::RttiLocator<MagicItem>;
internal::RttiLocator<MagicItem> registerRtti_MagicItem(392219);
template struct internal::RttiLocator<TESBoundObject>;
internal::RttiLocator<TESBoundObject> registerRtti_TESBoundObject(392220);
template struct internal::RttiLocator<EffectSetting>;
internal::RttiLocator<EffectSetting> registerRtti_EffectSetting(392251);
template struct internal::RttiLocator<BGSPerk>;
internal::RttiLocator<BGSPerk> registerRtti_BGSPerk(392257);
template struct internal::RttiLocator<EnchantmentItem>;
internal::RttiLocator<EnchantmentItem> registerRtti_EnchantmentItem(392265);
template struct internal::RttiLocator<IngredientItem>;
internal::RttiLocator<IngredientItem> registerRtti_IngredientItem(392268);
template struct internal::RttiLocator<TESGlobal>;
internal::RttiLocator<TESGlobal> registerRtti_TESGlobal(392299);
template struct internal::RttiLocator<BSExtraData>;
internal::RttiLocator<BSExtraData> registerRtti_BSExtraData(392300);
template struct internal::RttiLocator<ExtraCount>;
internal::RttiLocator<ExtraCount> registerRtti_ExtraCount(392312);
template struct internal::RttiLocator<ExtraHealth>;
internal::RttiLocator<ExtraHealth> registerRtti_ExtraHealth(392317);
template struct internal::RttiLocator<ExtraCharge>;
internal::RttiLocator<ExtraCharge> registerRtti_ExtraCharge(392319);
template struct internal::RttiLocator<ExtraPoison>;
internal::RttiLocator<ExtraPoison> registerRtti_ExtraPoison(392332);
template struct internal::RttiLocator<ExtraFactionChanges>;
internal::RttiLocator<ExtraFactionChanges> registerRtti_ExtraFactionChanges(392347);
template struct internal::RttiLocator<ExtraEnchantment>;
internal::RttiLocator<ExtraEnchantment> registerRtti_ExtraEnchantment(392362);
template struct internal::RttiLocator<TESWorldSpace>;
internal::RttiLocator<TESWorldSpace> registerRtti_TESWorldSpace(392429);
template struct internal::RttiLocator<TESObjectCELL>;
internal::RttiLocator<TESObjectCELL> registerRtti_TESObjectCELL(392430);
template struct internal::RttiLocator<TESObjectREFR>;
internal::RttiLocator<TESObjectREFR> registerRtti_TESObjectREFR(392437);
template struct internal::RttiLocator<TESAmmo>;
internal::RttiLocator<TESAmmo> registerRtti_TESAmmo(392439);
template struct internal::RttiLocator<TESPackage>;
internal::RttiLocator<TESPackage> registerRtti_TESPackage(392440);
template struct internal::RttiLocator<TESTopicInfo>;
internal::RttiLocator<TESTopicInfo> registerRtti_TESTopicInfo(392441);
template struct internal::RttiLocator<TESTopic>;
internal::RttiLocator<TESTopic> registerRtti_TESTopic(392442);
template struct internal::RttiLocator<TESFaction>;
internal::RttiLocator<TESFaction> registerRtti_TESFaction(392444);
template struct internal::RttiLocator<TESActorBase>;
internal::RttiLocator<TESActorBase> registerRtti_TESActorBase(392446);
template struct internal::RttiLocator<BGSScene>;
internal::RttiLocator<BGSScene> registerRtti_BGSScene(392447);
template struct internal::RttiLocator<TESQuest>;
internal::RttiLocator<TESQuest> registerRtti_TESQuest(392448);
template struct internal::RttiLocator<BGSKeyword>;
internal::RttiLocator<BGSKeyword> registerRtti_BGSKeyword(392449);
template struct internal::RttiLocator<ExtraSoul>;
internal::RttiLocator<ExtraSoul> registerRtti_ExtraSoul(392476);
template struct internal::RttiLocator<NiAVObject>;
internal::RttiLocator<NiAVObject> registerRtti_NiAVObject(392520);
template struct internal::RttiLocator<NiNode>;
internal::RttiLocator<NiNode> registerRtti_NiNode(392532);
template struct internal::RttiLocator<Actor>;
internal::RttiLocator<Actor> registerRtti_Actor(392538);
template struct internal::RttiLocator<TESObjectWEAP>;
internal::RttiLocator<TESObjectWEAP> registerRtti_TESObjectWEAP(392539);
template struct internal::RttiLocator<TESObjectACTI>;
internal::RttiLocator<TESObjectACTI> registerRtti_TESObjectACTI(392541);
template struct internal::RttiLocator<TESNPC>;
internal::RttiLocator<TESNPC> registerRtti_TESNPC(392546);
template struct internal::RttiLocator<TESIdleForm>;
internal::RttiLocator<TESIdleForm> registerRtti_TESIdleForm(392571);
template struct internal::RttiLocator<TESObjectARMO>;
internal::RttiLocator<TESObjectARMO> registerRtti_TESObjectARMO(392577);
template struct internal::RttiLocator<TESLevItem>;
internal::RttiLocator<TESLevItem> registerRtti_TESLevItem(392593);
template struct internal::RttiLocator<TESRegion>;
internal::RttiLocator<TESRegion> registerRtti_TESRegion(392718);
template struct internal::RttiLocator<bhkWorldObject>;
internal::RttiLocator<bhkWorldObject> registerRtti_bhkWorldObject(392752);
template struct internal::RttiLocator<bhkRigidBody>;
internal::RttiLocator<bhkRigidBody> registerRtti_bhkRigidBody(392755);
template struct internal::RttiLocator<BSGeometry>;
internal::RttiLocator<BSGeometry> registerRtti_BSGeometry(392797);
template struct internal::RttiLocator<BGSLocation>;
internal::RttiLocator<BGSLocation> registerRtti_BGSLocation(392856);
template struct internal::RttiLocator<TESWeather>;
internal::RttiLocator<TESWeather> registerRtti_TESWeather(392883);
template struct internal::RttiLocator<MagicCaster>;
internal::RttiLocator<MagicCaster> registerRtti_MagicCaster(393096);
template struct internal::RttiLocator<BGSAction>;
internal::RttiLocator<BGSAction> registerRtti_BGSAction(393130);
template struct internal::RttiLocator<NiBillboardNode>;
internal::RttiLocator<NiBillboardNode> registerRtti_NiBillboardNode(393325);
template struct internal::RttiLocator<BSTempEffect>;
internal::RttiLocator<BSTempEffect> registerRtti_BSTempEffect(393851);
template struct internal::RttiLocator<ValueModifierEffect>;
internal::RttiLocator<ValueModifierEffect> registerRtti_ValueModifierEffect(394302);
template struct internal::RttiLocator<ActiveEffect>;
internal::RttiLocator<ActiveEffect> registerRtti_ActiveEffect(394303);
template struct internal::RttiLocator<ActorMagicCaster>;
internal::RttiLocator<ActorMagicCaster> registerRtti_ActorMagicCaster(394312);
template struct internal::RttiLocator<ModelReferenceEffect>;
internal::RttiLocator<ModelReferenceEffect> registerRtti_ModelReferenceEffect(394371);
template struct internal::RttiLocator<ReferenceEffect>;
internal::RttiLocator<ReferenceEffect> registerRtti_ReferenceEffect(394372);
template struct internal::RttiLocator<ShaderReferenceEffect>;
internal::RttiLocator<ShaderReferenceEffect> registerRtti_ShaderReferenceEffect(394391);
template struct internal::RttiLocator<Projectile>;
internal::RttiLocator<Projectile> registerRtti_Projectile(394480);
template struct internal::RttiLocator<PlayerCharacter>;
internal::RttiLocator<PlayerCharacter> registerRtti_PlayerCharacter(394747);
template struct internal::RttiLocator<bhkRigidBodyT>;
internal::RttiLocator<bhkRigidBodyT> registerRtti_bhkRigidBodyT(398641);
template struct internal::RttiLocator<BSShaderProperty>;
internal::RttiLocator<BSShaderProperty> registerRtti_BSShaderProperty(399917);
template struct internal::RttiLocator<BSLightingShaderProperty>;
internal::RttiLocator<BSLightingShaderProperty> registerRtti_BSLightingShaderProperty(400050);
