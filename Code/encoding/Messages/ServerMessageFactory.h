#pragma once

#include <TiltedCore/Stl.hpp>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyActiveEffects.h>
#include <Messages/Message.h>
#include <MetaMessage.h>

#include <Messages/AuthenticationResponse.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/ServerReferencesMoveRequest.h>
#include <Messages/ServerTimeSettings.h>
#include <Messages/CharacterSpawnRequest.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyQuestUpdate.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifySpawnData.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyLockChange.h>
#include <Messages/AssignObjectsResponse.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifyObjectInventoryChanges.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyScriptAnimation.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifyNewPackage.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyPlayerRespawn.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyRelinquishControl.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/NotifyTimeScale.h>
#include <Messages/NotifyPartyTravel.h>
#include <Messages/SharedDialogue.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifyRemoveWaypoint.h>
#include <Messages/NotifySetTimeResult.h>
#include <Messages/WorldStateSync.h>

using TiltedPhoques::UniquePtr;

struct ServerMessageFactory
{
    UniquePtr<ServerMessage> Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept;

    template <class T> static auto Visit(T&& func)
    {
        auto s_visitor = CreateMessageVisitor<
            AuthenticationResponse, AssignCharacterResponse, ServerReferencesMoveRequest, ServerTimeSettings, CharacterSpawnRequest, NotifyInventoryChanges, NotifyFactionsChanges, NotifyRemoveCharacter, NotifyQuestUpdate,
            NotifyActorValueChanges, NotifyActorMaxValueChanges, NotifyHealthChangeBroadcast, NotifySpawnData, NotifyActivate, NotifyLockChange, AssignObjectsResponse, NotifyDeathStateChange, NotifyObjectInventoryChanges, NotifySpellCast,
            NotifyProjectileLaunch, NotifyInterruptCast, NotifyAddTarget, NotifyScriptAnimation, NotifyDrawWeapon, NotifyMount, NotifyNewPackage, NotifyRespawn, NotifySyncExperience, NotifyEquipmentChanges, NotifyChatMessageBroadcast, NotifyPlayerRespawn, NotifyDialogue,
            NotifySubtitle, NotifyRelinquishControl, NotifyPlayerLeft, NotifyPlayerJoined, NotifyDialogue, NotifySubtitle, NotifyPlayerLevel, NotifySettingsChange,
            NotifyWeatherChange, NotifySetWaypoint, NotifyRemoveWaypoint, NotifySetTimeResult, NotifyRemoveSpell, WorldStateSync, NotifyEquipmentSnapshot, NotifyActiveEffects, NotifyTimeScale, NotifyPartyTravel,
            NotifyDialogueStart, NotifyDialogueOpen, NotifyDialogueList, NotifyDialoguePicked, NotifyDialogueClosed, NotifyDialogueExperience,
            NotifyDialogueRefused, NotifyDialogueTraining>;

        return s_visitor(std::forward<T>(func));
    }
};
