#pragma once

// The messages that carry a shared payload unchanged (Payloads.h), declared: a request and its notification each.
// Relayed.h defines them.
#include <Opcodes.h>

namespace Payloads
{
struct EquipmentSnapshot;
struct DrawWeapon;
struct ActorValueChanges;
struct ActorMaxValueChanges;
struct DeathStateChange;
struct HealthChange;
struct SpellCast;
struct InterruptCast;
struct ProjectileLaunch;
struct AddTarget;
struct RemoveSpell;
struct Mount;
struct AiState;
struct Dialogue;
struct Subtitle;
struct SyncExperience;
struct ActiveEffects;
struct EffectFlash;
struct PlayerNative;
struct PartyNative;
struct WorldCall;
} // namespace Payloads

template <class TPayload, auto cOpcode> struct Relayed;

using RequestEquipmentSnapshot = Relayed<Payloads::EquipmentSnapshot, kRequestEquipmentSnapshot>;
using NotifyEquipmentSnapshot = Relayed<Payloads::EquipmentSnapshot, kNotifyEquipmentSnapshot>;
using DrawWeaponRequest = Relayed<Payloads::DrawWeapon, kDrawWeaponRequest>;
using NotifyDrawWeapon = Relayed<Payloads::DrawWeapon, kNotifyDrawWeapon>;
using RequestActorValueChanges = Relayed<Payloads::ActorValueChanges, kRequestActorValueChanges>;
using NotifyActorValueChanges = Relayed<Payloads::ActorValueChanges, kNotifyActorValueChanges>;
using RequestActorMaxValueChanges = Relayed<Payloads::ActorMaxValueChanges, kRequestActorMaxValueChanges>;
using NotifyActorMaxValueChanges = Relayed<Payloads::ActorMaxValueChanges, kNotifyActorMaxValueChanges>;
using RequestDeathStateChange = Relayed<Payloads::DeathStateChange, kRequestDeathStateChange>;
using NotifyDeathStateChange = Relayed<Payloads::DeathStateChange, kNotifyDeathStateChange>;
using RequestHealthChangeBroadcast = Relayed<Payloads::HealthChange, kRequestHealthChangeBroadcast>;
using NotifyHealthChangeBroadcast = Relayed<Payloads::HealthChange, kNotifyHealthChangeBroadcast>;
using SpellCastRequest = Relayed<Payloads::SpellCast, kSpellCastRequest>;
using NotifySpellCast = Relayed<Payloads::SpellCast, kNotifySpellCast>;
using InterruptCastRequest = Relayed<Payloads::InterruptCast, kInterruptCastRequest>;
using NotifyInterruptCast = Relayed<Payloads::InterruptCast, kNotifyInterruptCast>;
using ProjectileLaunchRequest = Relayed<Payloads::ProjectileLaunch, kProjectileLaunchRequest>;
using NotifyProjectileLaunch = Relayed<Payloads::ProjectileLaunch, kNotifyProjectileLaunch>;
using AddTargetRequest = Relayed<Payloads::AddTarget, kAddTargetRequest>;
using NotifyAddTarget = Relayed<Payloads::AddTarget, kNotifyAddTarget>;
using RemoveSpellRequest = Relayed<Payloads::RemoveSpell, kRemoveSpellRequest>;
using NotifyRemoveSpell = Relayed<Payloads::RemoveSpell, kNotifyRemoveSpell>;
using MountRequest = Relayed<Payloads::Mount, kMountRequest>;
using NotifyMount = Relayed<Payloads::Mount, kNotifyMount>;
using AiStateRequest = Relayed<Payloads::AiState, kAiStateRequest>;
using NotifyAiState = Relayed<Payloads::AiState, kNotifyAiState>;
using DialogueRequest = Relayed<Payloads::Dialogue, kDialogueRequest>;
using NotifyDialogue = Relayed<Payloads::Dialogue, kNotifyDialogue>;
using SubtitleRequest = Relayed<Payloads::Subtitle, kSubtitleRequest>;
using NotifySubtitle = Relayed<Payloads::Subtitle, kNotifySubtitle>;
using SyncExperienceRequest = Relayed<Payloads::SyncExperience, kSyncExperienceRequest>;
using NotifySyncExperience = Relayed<Payloads::SyncExperience, kNotifySyncExperience>;
using RequestActiveEffects = Relayed<Payloads::ActiveEffects, kRequestActiveEffects>;
using NotifyActiveEffects = Relayed<Payloads::ActiveEffects, kNotifyActiveEffects>;
using EffectFlashRequest = Relayed<Payloads::EffectFlash, kEffectFlashRequest>;
using NotifyEffectFlash = Relayed<Payloads::EffectFlash, kNotifyEffectFlash>;
using PlayerNativeRequest = Relayed<Payloads::PlayerNative, kPlayerNativeRequest>;
using NotifyPlayerNative = Relayed<Payloads::PlayerNative, kNotifyPlayerNative>;
using PartyNativeRequest = Relayed<Payloads::PartyNative, kPartyNativeRequest>;
using WorldCallRequest = Relayed<Payloads::WorldCall, kWorldCallRequest>;
using NotifyWorldCall = Relayed<Payloads::WorldCall, kNotifyWorldCall>;
