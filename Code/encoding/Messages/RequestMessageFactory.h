#pragma once

#include <Messages/Message.h>
#include <MetaMessage.h>

#include <Messages/AuthenticationRequest.h>
#include <Messages/AnnounceCharacterRequest.h>
#include <Messages/RequestRemoveCharacter.h>
#include <Messages/Relayed.h>
#include <Messages/ReferencesMoveRequest.h>
#include <Messages/RequestInventoryChanges.h>
#include <Messages/RequestFactionsChanges.h>
#include <Messages/ActivateRequest.h>
#include <Messages/LockChangeRequest.h>
#include <Messages/ContainerChangeRequest.h>
#include <Messages/CellStateRequest.h>
#include <Messages/RequestRespawn.h>
#include <Messages/RequestEquipmentChanges.h>
#include <Messages/SendChatMessageRequest.h>
#include <Messages/SetTimeCommandRequest.h>
#include <Messages/PlayerLevelRequest.h>
#include <Messages/TimeScaleRequest.h>
#include <Messages/SharedDialogue.h>
#include <Messages/EnterCellRequest.h>
#include <Messages/RequestSetWaypoint.h>
#include <Messages/RequestRemoveWaypoint.h>
#include <Messages/JoinMessages.h>
#include <Messages/WaitMessages.h>
#include <Messages/DeathMessages.h>

struct RequestMessageFactory
{
    std::unique_ptr<RequestMessage> Extract(Wire::Buffer::Reader& aReader) const noexcept;

    // aFunc with each request's MetaMessage, in opcode order (Opcodes.h), until it returns true.
    template <class T> static void Visit(T&& aFunc)
    {
#define COOP_VISIT(aName)                                                                                              \
    if (::details::MetaMessage<aName> meta; aFunc(meta))                                                             \
        return;
        COOP_REQUESTS(COOP_VISIT)
#undef COOP_VISIT
    }
};
