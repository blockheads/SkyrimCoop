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
    X(RoutedCallRequest)            \
    X(CallEndedRequest)             \
    X(PlayerNativeResultRequest)    \
    X(EffectFlashRequest)           \
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
    X(PartyTravelRequest)            \
    X(FollowerTradeClosedRequest)   \
    X(ActorPerksRequest)            \
    X(MeleeStaggerRequest)           \
    X(ContainerTransferRequest)     \
    X(StoryEventRequest)            \
    X(SystemQuestsRequest)          \
    X(RecipesRequest)              \
    X(BugReportRequest)            \
    X(DialogueHighlightRequest)

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
    X(NotifyRoutedCall)            \
    X(NotifyCallEnded)             \
    X(NotifyPlayerNativeResult)    \
    X(NotifyEffectFlash)           \
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
    X(NotifyPartyGold)             \
    X(NotifyFollowerTrade)          \
    X(NotifyActorPerks)             \
    X(NotifyMeleeStagger)           \
    X(NotifySystemQuests)           \
    X(NotifyRecipes)               \
    X(NotifyBugReport)             \
    X(NotifyDialogueHighlight)     \
    X(NotifyPartyItem)             \
    X(NotifyPartyItems)

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

#define COOP_OPCODE_NAME(aName) #aName,

// A message's name by its opcode, "?" past the list (bug reports' message trail).
inline const char* RequestName(unsigned aOpcode) noexcept
{
    static constexpr const char* s_names[] = {COOP_REQUESTS(COOP_OPCODE_NAME)};
    return aOpcode < kRequestOpcodeMax ? s_names[aOpcode] : "?";
}

inline const char* NotifyName(unsigned aOpcode) noexcept
{
    static constexpr const char* s_names[] = {COOP_NOTIFICATIONS(COOP_OPCODE_NAME)};
    return aOpcode < kNotifyOpcodeMax ? s_names[aOpcode] : "?";
}

#undef COOP_OPCODE_NAME
