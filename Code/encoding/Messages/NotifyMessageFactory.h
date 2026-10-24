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
#include <Messages/Relayed.h>
#include <Messages/Message.h>
#include <MetaMessage.h>

#include <Messages/AuthenticationResponse.h>
#include <Messages/NotifyReferencesMove.h>
#include <Messages/NotifyTimeSettings.h>
#include <Messages/NotifyCharacterSpawn.h>
#include <Messages/NotifyInventoryChanges.h>
#include <Messages/NotifyFactionsChanges.h>
#include <Messages/NotifyRemoveCharacter.h>
#include <Messages/NotifyActivate.h>
#include <Messages/NotifyRespawn.h>
#include <Messages/NotifyEquipmentChanges.h>
#include <Messages/NotifyChatMessageBroadcast.h>
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
#include <Messages/JoinMessages.h>

struct NotifyMessageFactory
{
    std::unique_ptr<NotifyMessage> Extract(Wire::Buffer::Reader& aReader) const noexcept;

    // aFunc with each notification's MetaMessage, in opcode order (Opcodes.h), until it returns true.
    template <class T> static void Visit(T&& aFunc)
    {
#define COOP_VISIT(aName)                                                                                              \
    if (::details::MetaMessage<aName> meta; aFunc(meta))                                                             \
        return;
        COOP_NOTIFICATIONS(COOP_VISIT)
#undef COOP_VISIT
    }
};
