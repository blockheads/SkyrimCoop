#include "dispatch_hook_event.h"
#include "protocol.h"
#include "game_bridge/game_reader.h"
#include "game_bridge/pointer_table.h"
#include "Services/WeatherService.h"
#include "Services/CalendarService.h"
#include "Services/QuestService.h"
#include "Services/CombatService.h"
#include "Services/InventoryService.h"
#include "Services/ActorValueService.h"
#include "Services/MagicService.h"
#include "Services/CharacterService.h"

void DispatchHookEvent(const HookEventPacket& aHookPkt,
                       GameReader& aReader, PointerTable& aPtrs,
                       WeatherService& aWeather, CalendarService& aCalendar,
                       QuestService& aQuest, CombatService& aCombat,
                       InventoryService& aInventory, ActorValueService& aActorValue,
                       MagicService& aMagic, CharacterService& aCharacter)
{
    // Core pointer table tracking for actor add/remove
    switch (static_cast<HookOpcode>(aHookPkt.header.opcode)) {
    case HOOK_ACTOR_ADDED: {
        if (aHookPkt.argCount >= 1 && aHookPkt.args[0] != 0) {
            uint64_t actorPtr = aHookPkt.args[0];
            uint32_t formId = aReader.ReadFormId(actorPtr);
            if (formId != 0) {
                aPtrs.Insert(formId, actorPtr);
            }
        }
        break;
    }
    case HOOK_ACTOR_REMOVED: {
        if (aHookPkt.argCount >= 1 && aHookPkt.args[0] != 0) {
            uint64_t actorPtr = aHookPkt.args[0];
            uint32_t formId = aReader.ReadFormId(actorPtr);
            if (formId != 0) {
                aPtrs.Remove(formId);
            } else {
                aPtrs.RemoveByPointer(actorPtr);
            }
        }
        break;
    }
    default:
        break;
    }

    // Dispatch to all services
    aWeather.OnHookEvent(aHookPkt);
    aCalendar.OnHookEvent(aHookPkt);
    aQuest.OnHookEvent(aHookPkt);
    aCombat.OnHookEvent(aHookPkt);
    aInventory.OnHookEvent(aHookPkt);
    aActorValue.OnHookEvent(aHookPkt);
    aMagic.OnHookEvent(aHookPkt);
    aCharacter.OnHookEvent(aHookPkt);
}
