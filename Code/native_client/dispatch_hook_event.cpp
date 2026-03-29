#include "dispatch_hook_event.h"
#include <nlohmann/json.hpp>
#include "json_protocol.h"
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

void DispatchHookEvent(const nlohmann::json& aMsg,
                       GameReader& aReader, PointerTable& aPtrs,
                       WeatherService& aWeather, CalendarService& aCalendar,
                       QuestService& aQuest, CombatService& aCombat,
                       InventoryService& aInventory, ActorValueService& aActorValue,
                       MagicService& aMagic, CharacterService& aCharacter)
{
    const auto& op = aMsg[JsonField::kOp].get_ref<const std::string&>();

    // Core pointer table tracking for actor add/remove
    if (op == JsonOp::kActorAdded || op == JsonOp::kSpawnActor ||
        op == JsonOp::kCharacterCtor || op == JsonOp::kActorProcess) {
        uint32_t formId = aMsg.value(JsonField::kFormId, 0u);
        uint64_t rawPtr = aMsg.value(JsonField::kPtr, 0ull);
        if (formId != 0 && rawPtr != 0)
            aPtrs.Insert(formId, rawPtr);
    } else if (op == JsonOp::kActorRemoved) {
        uint32_t formId = aMsg.value(JsonField::kFormId, 0u);
        if (formId != 0)
            aPtrs.Remove(formId);
        else
            aPtrs.RemoveByPointer(aMsg.value(JsonField::kPtr, 0ull));
    }

    // Route to appropriate service based on op string
    if (op == JsonOp::kActorAdded || op == JsonOp::kActorRemoved ||
        op == JsonOp::kSetPosition || op == JsonOp::kSpawnActor ||
        op == JsonOp::kCharacterCtor || op == JsonOp::kDeathItems ||
        op == JsonOp::kPerformAction || op == JsonOp::kActorProcess ||
        op == JsonOp::kPlayAnimation || op == JsonOp::kMountPackage)
        aCharacter.OnHookEvent(aMsg);

    if (op == JsonOp::kWeatherChange || op == JsonOp::kSetWeather || op == JsonOp::kForceWeather)
        aWeather.OnHookEvent(aMsg);

    if (op == JsonOp::kSimulateTime)
        aCalendar.OnHookEvent(aMsg);

    if (op == JsonOp::kDamageActor || op == JsonOp::kProjectile || op == JsonOp::kAddTarget)
        aCombat.OnHookEvent(aMsg);

    if (op == JsonOp::kAddInventory || op == JsonOp::kPickUp || op == JsonOp::kDropObject ||
        op == JsonOp::kEquip || op == JsonOp::kUnequip || op == JsonOp::kRemoveInvItem ||
        op == JsonOp::kActivate || op == JsonOp::kLockChange)
        aInventory.OnHookEvent(aMsg);

    if (op == JsonOp::kRegenAttr)
        aActorValue.OnHookEvent(aMsg);

    if (op == JsonOp::kSpellCast || op == JsonOp::kInterruptCast ||
        op == JsonOp::kRemoveSpell || op == JsonOp::kApplyEffect)
        aMagic.OnHookEvent(aMsg);
}
