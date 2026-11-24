#pragma once

// The messages that carry a shared payload unchanged (Payloads.h), declared: a request and its notification each.
// Relayed.h defines them.
#include <Opcodes.h>

namespace Payloads
{
struct GraphOpsBatch;
struct GraphSync;
struct GraphSyncPoint;
struct ActorPerks;
struct MeleeStagger;
struct TrapContact;
struct TrapHit;
struct WorldProjectile;
struct ActorValueChanges;
struct ActorMaxValueChanges;
struct DeathStateChange;
struct HealthChange;
struct InterruptCast;
struct ProjectileLaunch;
struct AddTarget;
struct RemoveSpell;
struct PlayerName;
struct ActorName;
struct Mount;
struct Claim;
struct ActorGiveBack;
struct Record;
struct RecordAsk;
struct Dialogue;
struct Subtitle;
struct ActiveEffects;
struct EffectFlash;
struct RoutedCall;
struct CallEnded;
struct PlayerNativeResult;
struct BodyStates;
struct PartyGold;
struct PartyItem;
struct PartyItems;
struct SystemQuests;
struct Recipes;
struct BugReport;
struct DevCommand;
struct DialogueHighlight;
struct DialogueSkip;
} // namespace Payloads

template <class TPayload, auto cOpcode> struct Relayed;

using RequestActorValueChanges = Relayed<Payloads::ActorValueChanges, kRequestActorValueChanges>;
using NotifyActorValueChanges = Relayed<Payloads::ActorValueChanges, kNotifyActorValueChanges>;
using RequestActorMaxValueChanges = Relayed<Payloads::ActorMaxValueChanges, kRequestActorMaxValueChanges>;
using NotifyActorMaxValueChanges = Relayed<Payloads::ActorMaxValueChanges, kNotifyActorMaxValueChanges>;
using RequestDeathStateChange = Relayed<Payloads::DeathStateChange, kRequestDeathStateChange>;
using NotifyDeathStateChange = Relayed<Payloads::DeathStateChange, kNotifyDeathStateChange>;
using RequestHealthChangeBroadcast = Relayed<Payloads::HealthChange, kRequestHealthChangeBroadcast>;
using NotifyHealthChangeBroadcast = Relayed<Payloads::HealthChange, kNotifyHealthChangeBroadcast>;
using InterruptCastRequest = Relayed<Payloads::InterruptCast, kInterruptCastRequest>;
using NotifyInterruptCast = Relayed<Payloads::InterruptCast, kNotifyInterruptCast>;
using ProjectileLaunchRequest = Relayed<Payloads::ProjectileLaunch, kProjectileLaunchRequest>;
using NotifyProjectileLaunch = Relayed<Payloads::ProjectileLaunch, kNotifyProjectileLaunch>;
using AddTargetRequest = Relayed<Payloads::AddTarget, kAddTargetRequest>;
using NotifyAddTarget = Relayed<Payloads::AddTarget, kNotifyAddTarget>;
using RemoveSpellRequest = Relayed<Payloads::RemoveSpell, kRemoveSpellRequest>;
using NotifyRemoveSpell = Relayed<Payloads::RemoveSpell, kNotifyRemoveSpell>;
using NotifyPlayerName = Relayed<Payloads::PlayerName, kNotifyPlayerName>;
using NotifyActorName = Relayed<Payloads::ActorName, kNotifyActorName>;
using NotifyMount = Relayed<Payloads::Mount, kNotifyMount>;
// To a game whose player boarded a horse nobody else drives: its player gets off.
using NotifyMountRefused = Relayed<Payloads::Mount, kNotifyMountRefused>;
// A player asks for a seat on a horse another player drives; NotifyBoard: the host gave it one (its game seats it).
using BoardRequest = Relayed<Payloads::Mount, kBoardRequest>;
using NotifyBoard = Relayed<Payloads::Mount, kNotifyBoard>;
// A passenger got off (the host's rule alone).
using UnboardRequest = Relayed<Payloads::Mount, kUnboardRequest>;
// A rider got off a horse that stays ridden (a passenger, or a driver handing over): its copy off on every other game.
using NotifyUnboard = Relayed<Payloads::Mount, kNotifyUnboard>;
// A rider got off (its mount's give-back follows, ActorGiveBackRequest); NotifyDismount: its copy off everywhere.
using DismountRequest = Relayed<Payloads::Mount, kDismountRequest>;
using NotifyDismount = Relayed<Payloads::Mount, kNotifyDismount>;
// An action's claim (HostClaims: a ride, a kill move); NotifyClaim: granted, to every game (the holder takes its taken
// members); NotifyClaimRefused: to every game, its claimer ends the action.
using ClaimRequest = Relayed<Payloads::Claim, kClaimRequest>;
using NotifyClaim = Relayed<Payloads::Claim, kNotifyClaim>;
using NotifyClaimRefused = Relayed<Payloads::Claim, kNotifyClaimRefused>;
// Its holder gives it back; NotifyActorGiveBack: the host's game takes it once its copy ran the holder's ops.
using ActorGiveBackRequest = Relayed<Payloads::ActorGiveBack, kActorGiveBackRequest>;
using NotifyActorGiveBack = Relayed<Payloads::ActorGiveBack, kNotifyActorGiveBack>;
// A part of an actor's record from its owner; NotifyRecord: every other game's CopyRecords takes it.
using RecordRequest = Relayed<Payloads::Record, kRecordRequest>;
using NotifyRecord = Relayed<Payloads::Record, kNotifyRecord>;
// A game asks for a part again; NotifyRecordAsk: the actor's owner sends it under a new version.
using RecordAskRequest = Relayed<Payloads::RecordAsk, kRecordAskRequest>;
using NotifyRecordAsk = Relayed<Payloads::RecordAsk, kNotifyRecordAsk>;
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
using BodyStatesRequest = Relayed<Payloads::BodyStates, kBodyStatesRequest>;
using NotifyBodyStates = Relayed<Payloads::BodyStates, kNotifyBodyStates>;
using NotifyPartyGold = Relayed<Payloads::PartyGold, kNotifyPartyGold>;
using NotifyPartyItem = Relayed<Payloads::PartyItem, kNotifyPartyItem>;
using NotifyPartyItems = Relayed<Payloads::PartyItems, kNotifyPartyItems>;

using ActorPerksRequest = Relayed<Payloads::ActorPerks, kActorPerksRequest>;
using NotifyActorPerks = Relayed<Payloads::ActorPerks, kNotifyActorPerks>;

using MeleeStaggerRequest = Relayed<Payloads::MeleeStagger, kMeleeStaggerRequest>;
using NotifyMeleeStagger = Relayed<Payloads::MeleeStagger, kNotifyMeleeStagger>;

// A trap touched a guest's player, in its own game (the host's script decides what it does); the hit the host's script
// made of it, for that player's game (docs/design/traps.md).
using TrapContactRequest = Relayed<Payloads::TrapContact, kTrapContactRequest>;
using TrapHitRequest = Relayed<Payloads::TrapHit, kTrapHitRequest>;
using NotifyTrapHit = Relayed<Payloads::TrapHit, kNotifyTrapHit>;
// A projectile an object of the host's world fired (a dart trap), for every guest to show.
using WorldProjectileRequest = Relayed<Payloads::WorldProjectile, kWorldProjectileRequest>;
using NotifyWorldProjectile = Relayed<Payloads::WorldProjectile, kNotifyWorldProjectile>;

using SystemQuestsRequest = Relayed<Payloads::SystemQuests, kSystemQuestsRequest>;
using NotifySystemQuests = Relayed<Payloads::SystemQuests, kNotifySystemQuests>;
using RecipesRequest = Relayed<Payloads::Recipes, kRecipesRequest>;
using NotifyRecipes = Relayed<Payloads::Recipes, kNotifyRecipes>;
using BugReportRequest = Relayed<Payloads::BugReport, kBugReportRequest>;
using NotifyBugReport = Relayed<Payloads::BugReport, kNotifyBugReport>;
using DialogueHighlightRequest = Relayed<Payloads::DialogueHighlight, kDialogueHighlightRequest>;
using NotifyDialogueHighlight = Relayed<Payloads::DialogueHighlight, kNotifyDialogueHighlight>;
using DialogueSkipRequest = Relayed<Payloads::DialogueSkip, kDialogueSkipRequest>;
using NotifyDialogueSkip = Relayed<Payloads::DialogueSkip, kNotifyDialogueSkip>;
using GraphOpsRequest = Relayed<Payloads::GraphOpsBatch, kGraphOpsRequest>;
using NotifyGraphOps = Relayed<Payloads::GraphOpsBatch, kNotifyGraphOps>;
using GraphSyncRequest = Relayed<Payloads::GraphSync, kGraphSyncRequest>;
using NotifyGraphSync = Relayed<Payloads::GraphSync, kNotifyGraphSync>;
using GraphSyncPointRequest = Relayed<Payloads::GraphSyncPoint, kGraphSyncPointRequest>;
using NotifyGraphSyncPoint = Relayed<Payloads::GraphSyncPoint, kNotifyGraphSyncPoint>;
using DevCommandRequest = Relayed<Payloads::DevCommand, kDevCommandRequest>;
using NotifyDevCommand = Relayed<Payloads::DevCommand, kNotifyDevCommand>;
