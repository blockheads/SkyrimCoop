#pragma once

// Every message, in opcode order: the opcodes are the wire's, so a new message goes at the end of its list. Each
// entry makes the opcode k<Name> and the message factories' entry (RequestMessageFactory.h, NotifyMessageFactory.h).
#define COOP_REQUESTS(X)            \
    X(AuthenticationRequest)        \
    X(AnnounceCharacterRequest)       \
    X(ReferencesMoveRequest)        \
    X(RequestFactionsChanges)       \
    X(RequestActorValueChanges)     \
    X(RequestActorMaxValueChanges)  \
    X(RequestHealthChangeBroadcast) \
    X(ActivateRequest)              \
    X(LockChangeRequest)            \
    X(RequestDeathStateChange)      \
    X(RequestInventoryChanges)      \
    X(SpellCastRequest)             \
    X(InterruptCastRequest)         \
    X(AddTargetRequest)             \
    X(RemoveSpellRequest)           \
    X(ProjectileLaunchRequest)      \
    X(DrawWeaponRequest)            \
    X(MountRequest)                 \
    X(AiStateRequest)               \
    X(RequestRespawn)               \
    X(RequestEquipmentChanges)      \
    X(SendChatMessageRequest)       \
    X(DialogueRequest)              \
    X(SubtitleRequest)              \
    X(PlayerLevelRequest)           \
    X(RequestSetWaypoint)           \
    X(RequestRemoveWaypoint)        \
    X(SetTimeCommandRequest)        \
    X(RequestRemoveCharacter)       \
    X(RequestEquipmentSnapshot)     \
    X(EnterCellRequest)             \
    X(RequestActiveEffects)         \
    X(TimeScaleRequest)             \
    X(DialogueStartRequest)         \
    X(DialogueOpenedRequest)        \
    X(DialogueOptionsRequest)       \
    X(DialoguePickRequest)          \
    X(DialogueCloseRequest)         \
    X(DialogueJoinRequest)          \
    X(ContainerChangeRequest)       \
    X(CellStateRequest)             \
    X(DialogueStartFailedRequest)   \
    X(PlayerNativeRequest)          \
    X(PlayerNativeResultRequest)    \
    X(EffectFlashRequest)           \
    X(PartyNativeRequest)           \
    X(WorldCallRequest)             \
    X(DismountRequest)              \
    X(PlayerRecordRequest)          \
    X(BodyPushRequest)              \
    X(BodyFramesRequest)            \
    X(BodyReleaseRequest)           \
    X(BodyPoseRequest)              \
    X(WaitRequest)                  \
    X(WaitWorldRequest)             \
    X(PlayerDeathRequest)           \
    X(BodyRestsRequest)             \
    X(CrimeRequest)                 \
    X(DropItemRequest)              \
    X(PickUpRequest)                \
    X(TravelTimeRequest)            \
    X(PartyTravelRequest)

#define COOP_NOTIFICATIONS(X)      \
    X(AuthenticationResponse)      \
    X(NotifyReferencesMove)        \
    X(NotifyTimeSettings)          \
    X(NotifyCharacterSpawn)        \
    X(NotifyFactionsChanges)       \
    X(NotifyRemoveCharacter)       \
    X(NotifyActorValueChanges)     \
    X(NotifyActorMaxValueChanges)  \
    X(NotifyHealthChangeBroadcast) \
    X(NotifyActivate)              \
    X(NotifyDeathStateChange)      \
    X(NotifyInventoryChanges)      \
    X(NotifySpellCast)             \
    X(NotifyInterruptCast)         \
    X(NotifyAddTarget)             \
    X(NotifyRemoveSpell)           \
    X(NotifyProjectileLaunch)      \
    X(NotifyDrawWeapon)            \
    X(NotifyMount)                 \
    X(NotifyAiState)               \
    X(NotifyRespawn)               \
    X(NotifyEquipmentChanges)      \
    X(NotifyChatMessageBroadcast)  \
    X(NotifyDialogue)              \
    X(NotifySubtitle)              \
    X(NotifyPlayerLeft)            \
    X(NotifyPlayerJoined)          \
    X(NotifyPlayerLevel)           \
    X(NotifySettingsChange)        \
    X(NotifyWeatherChange)         \
    X(NotifySetWaypoint)           \
    X(NotifyRemoveWaypoint)        \
    X(NotifyEquipmentSnapshot)     \
    X(NotifyActiveEffects)         \
    X(NotifyTimeScale)             \
    X(NotifyPartyTravel)           \
    X(NotifyDialogueStart)         \
    X(NotifyDialogueOpen)          \
    X(NotifyDialogueList)          \
    X(NotifyDialoguePicked)        \
    X(NotifyDialogueClosed)        \
    X(NotifyGlobals)               \
    X(NotifyQuests)                \
    X(NotifyObjectChanges)         \
    X(NotifyDialogueRefused)       \
    X(NotifyPlayerNative)          \
    X(NotifyPlayerNativeResult)    \
    X(NotifyEffectFlash)           \
    X(NotifyPartyNative)           \
    X(NotifyWorldCall)             \
    X(NotifyDismount)              \
    X(NotifySaveChunk)             \
    X(NotifyPlayerPerished)        \
    X(NotifyBodyFrames)            \
    X(NotifyBodyRests)             \
    X(NotifyWaitState)             \
    X(NotifyWaitWorld)             \
    X(NotifyPartyDeath)            \
    X(NotifyPlayerRespawn)         \
    X(NotifyPartyReload)           \
    X(NotifyCrimeState)            \
    X(NotifyLockCrime)             \
    X(NotifyCrimeConfiscation)     \
    X(NotifyItemDropped)           \
    X(NotifyPickUp)                \
    X(NotifyPartyGold)

#define COOP_OPCODE(aName) k##aName,

enum RequestOpcode : unsigned char
{
    COOP_REQUESTS(COOP_OPCODE)
    kRequestOpcodeMax
};

enum NotifyOpcode : unsigned char
{
    COOP_NOTIFICATIONS(COOP_OPCODE)
    kNotifyOpcodeMax
};

#undef COOP_OPCODE
