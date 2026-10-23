#pragma once

#include <Messages/Message.h>
#include <MetaMessage.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/AssignCharacterRequest.h>
#include <Messages/CancelAssignmentRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/RequestEquipmentSnapshot.h>
#include <Messages/RequestActiveEffects.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/RequestActorValueChanges.h>
#include <Messages/RequestActorMaxValueChanges.h>
#include <Messages/RequestHealthChangeBroadcast.h>
#include <Messages/ActivateRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/RequestDeathStateChange.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/CellStateRequest.h>
#include <Messages/SpellCastRequest.h>
#include <Messages/InterruptCastRequest.h>
#include <Messages/AddTargetRequest.h>
#include <Messages/RemoveSpellRequest.h>
#include <Messages/ProjectileLaunchRequest.h>
#include <Messages/DrawWeaponRequest.h>
#include <Messages/MountRequest.h>
#include <Messages/AiStateRequest.h>
#include <Messages/PlayerNativeRequest.h>
#include <Messages/PlayerNativeResultRequest.h>
#include <Messages/EffectFlashRequest.h>
#include <Messages/PartyNativeRequest.h>
#include <Messages/WorldCallRequest.h>
#include <Messages/RequestRespawn.h>
#include <Messages/SyncExperienceRequest.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/SetTimeCommandRequest.h>
#include <Messages/PlayerRespawnRequest.h>
#include <Messages/DialogueRequest.h>
#include <Messages/SubtitleRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/TimeScaleRequest.h>
#include <Messages/SharedDialogue.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>

struct RequestMessageFactory
{
    std::unique_ptr<RequestMessage> Extract(Wire::Buffer::Reader& aReader) const noexcept;

    template <class T> static auto Visit(T&& func)
    {
        auto s_visitor = CreateMessageVisitor<
            AuthenticationRequest, AssignCharacterRequest, CancelAssignmentRequest, ReferencesMoveRequest, RequestInventoryChanges, RequestFactionsChanges,
            RequestActorValueChanges, RequestActorMaxValueChanges, RequestHealthChangeBroadcast, ActivateRequest, LockChangeRequest, RequestDeathStateChange, 
            SpellCastRequest, ProjectileLaunchRequest, InterruptCastRequest, AddTargetRequest, DrawWeaponRequest, MountRequest, AiStateRequest, RequestRespawn, SyncExperienceRequest,
            RequestEquipmentChanges, SendChatMessageRequest, PlayerRespawnRequest, DialogueRequest, SubtitleRequest, PlayerLevelRequest, RequestSetWaypoint,
            RequestRemoveWaypoint, RemoveSpellRequest, SetTimeCommandRequest, RequestRemoveCharacter, RequestEquipmentSnapshot, EnterCellRequest, RequestActiveEffects, TimeScaleRequest,
            DialogueStartRequest, DialogueOpenedRequest, DialogueOptionsRequest, DialoguePickRequest, DialogueCloseRequest,
            DialogueJoinRequest, ContainerChangeRequest, CellStateRequest, DialogueStartFailedRequest, PlayerNativeRequest, PlayerNativeResultRequest, EffectFlashRequest, PartyNativeRequest, WorldCallRequest>;

        return s_visitor(std::forward<T>(func));
    }
};
