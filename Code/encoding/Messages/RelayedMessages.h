#pragma once

// The messages that carry a shared payload unchanged (Payloads.h), declared: a request and its notification each.
// Relayed.h defines them.
#include <Opcodes.h>

namespace Payloads
{
struct EquipmentSnapshot;
struct ActorPerks;
struct MeleeStagger;
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
struct Dismount;
struct AiState;
struct Dialogue;
struct Subtitle;
struct ActiveEffects;
struct EffectFlash;
struct RoutedCall;
struct CallEnded;
struct PlayerNativeResult;
struct BodyFrames;
struct BodyRests;
struct BodyPushes;
struct PartyGold;
struct SystemQuests;
struct Recipes;
struct BugReport;
struct DialogueHighlight;
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
using DismountRequest = Relayed<Payloads::Dismount, kDismountRequest>;
using NotifyDismount = Relayed<Payloads::Dismount, kNotifyDismount>;
using AiStateRequest = Relayed<Payloads::AiState, kAiStateRequest>;
using NotifyAiState = Relayed<Payloads::AiState, kNotifyAiState>;
using DialogueRequest = Relayed<Payloads::Dialogue, kDialogueRequest>;
using NotifyDialogue = Relayed<Payloads::Dialogue, kNotifyDialogue>;
using SubtitleRequest = Relayed<Payloads::Subtitle, kSubtitleRequest>;
using NotifySubtitle = Relayed<Payloads::Subtitle, kNotifySubtitle>;
using RequestActiveEffects = Relayed<Payloads::ActiveEffects, kRequestActiveEffects>;
using NotifyActiveEffects = Relayed<Payloads::ActiveEffects, kNotifyActiveEffects>;
using EffectFlashRequest = Relayed<Payloads::EffectFlash, kEffectFlashRequest>;
using NotifyEffectFlash = Relayed<Payloads::EffectFlash, kNotifyEffectFlash>;
using RoutedCallRequest = Relayed<Payloads::RoutedCall, kRoutedCallRequest>;
using NotifyRoutedCall = Relayed<Payloads::RoutedCall, kNotifyRoutedCall>;
using CallEndedRequest = Relayed<Payloads::CallEnded, kCallEndedRequest>;
using NotifyCallEnded = Relayed<Payloads::CallEnded, kNotifyCallEnded>;
using PlayerNativeResultRequest = Relayed<Payloads::PlayerNativeResult, kPlayerNativeResultRequest>;
using NotifyPlayerNativeResult = Relayed<Payloads::PlayerNativeResult, kNotifyPlayerNativeResult>;
using NotifyBodyFrames = Relayed<Payloads::BodyFrames, kNotifyBodyFrames>;
using NotifyBodyRests = Relayed<Payloads::BodyRests, kNotifyBodyRests>;
using BodyPushRequest = Relayed<Payloads::BodyPushes, kBodyPushRequest>;
using BodyFramesRequest = Relayed<Payloads::BodyFrames, kBodyFramesRequest>;
using BodyReleaseRequest = Relayed<Payloads::BodyFrames, kBodyReleaseRequest>;
using BodyPoseRequest = Relayed<Payloads::BodyRests, kBodyPoseRequest>;
using BodyRestsRequest = Relayed<Payloads::BodyRests, kBodyRestsRequest>;
using NotifyPartyGold = Relayed<Payloads::PartyGold, kNotifyPartyGold>;

using ActorPerksRequest = Relayed<Payloads::ActorPerks, kActorPerksRequest>;
using NotifyActorPerks = Relayed<Payloads::ActorPerks, kNotifyActorPerks>;

using MeleeStaggerRequest = Relayed<Payloads::MeleeStagger, kMeleeStaggerRequest>;
using NotifyMeleeStagger = Relayed<Payloads::MeleeStagger, kNotifyMeleeStagger>;

using SystemQuestsRequest = Relayed<Payloads::SystemQuests, kSystemQuestsRequest>;
using NotifySystemQuests = Relayed<Payloads::SystemQuests, kNotifySystemQuests>;
using RecipesRequest = Relayed<Payloads::Recipes, kRecipesRequest>;
using NotifyRecipes = Relayed<Payloads::Recipes, kNotifyRecipes>;
using BugReportRequest = Relayed<Payloads::BugReport, kBugReportRequest>;
using NotifyBugReport = Relayed<Payloads::BugReport, kNotifyBugReport>;
using DialogueHighlightRequest = Relayed<Payloads::DialogueHighlight, kDialogueHighlightRequest>;
using NotifyDialogueHighlight = Relayed<Payloads::DialogueHighlight, kNotifyDialogueHighlight>;
