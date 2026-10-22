#pragma once

#include <deque>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <Messages/NotifyEquipmentSnapshot.h>
#include <Messages/NotifyActiveEffects.h>
#include <Messages/Message.h>
#include <MetaMessage.h>

#include <Messages/AuthenticationResponse.h>
#include <Messages/AssignCharacterResponse.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/NotifyTimeSettings.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyActorValueChanges.h>
#include <Messages/NotifyActorMaxValueChanges.h>
#include <Messages/NotifyHealthChangeBroadcast.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyDeathStateChange.h>
#include <Messages/NotifySpellCast.h>
#include <Messages/NotifyInterruptCast.h>
#include <Messages/NotifyAddTarget.h>
#include <Messages/NotifyRemoveSpell.h>
#include <Messages/NotifyProjectileLaunch.h>
#include <Messages/NotifyDrawWeapon.h>
#include <Messages/NotifyMount.h>
#include <Messages/NotifyAiState.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifySyncExperience.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyChatMessageBroadcast.h>
#include <Messages/NotifyDialogue.h>
#include <Messages/NotifySubtitle.h>
#include <Messages/NotifyPlayerLeft.h>
#include <Messages/NotifyPlayerJoined.h>
#include <Messages/NotifyPlayerLevel.h>
#include <Messages/NotifySettingsChange.h>
#include <Messages/NotifyWeatherChange.h>
#include <Messages/NotifyTimeScale.h>
#include <Messages/NotifyPartyTravel.h>
#include <Messages/SharedDialogue.h>
#include <Messages/WorldState.h>
#include <Messages/NotifySetWaypoint.h>
#include <Messages/NotifyRemoveWaypoint.h>

struct NotifyMessageFactory
{
    std::unique_ptr<NotifyMessage> Extract(Wire::Buffer::Reader& aReader) const noexcept;

    template <class T> static auto Visit(T&& func)
    {
        auto s_visitor = CreateMessageVisitor<
            AuthenticationResponse, AssignCharacterResponse, NotifyReferencesMove, NotifyTimeSettings, NotifyCharacterSpawn, NotifyInventoryChanges, NotifyFactionsChanges, NotifyRemoveCharacter,
            NotifyActorValueChanges, NotifyActorMaxValueChanges, NotifyHealthChangeBroadcast, NotifyActivate, NotifyDeathStateChange, NotifySpellCast,
            NotifyProjectileLaunch, NotifyInterruptCast, NotifyAddTarget, NotifyDrawWeapon, NotifyMount, NotifyAiState, NotifyRespawn, NotifySyncExperience, NotifyEquipmentChanges, NotifyChatMessageBroadcast, NotifyDialogue,
            NotifySubtitle, NotifyPlayerLeft, NotifyPlayerJoined, NotifyPlayerLevel, NotifySettingsChange,
            NotifyWeatherChange, NotifySetWaypoint, NotifyRemoveWaypoint, NotifyRemoveSpell, NotifyEquipmentSnapshot, NotifyActiveEffects, NotifyTimeScale, NotifyPartyTravel,
            NotifyDialogueStart, NotifyDialogueOpen, NotifyDialogueList, NotifyDialoguePicked, NotifyDialogueClosed, NotifyDialogueExperience, NotifyGlobals, NotifyQuests, NotifyObjectChanges>;

        return s_visitor(std::forward<T>(func));
    }
};
