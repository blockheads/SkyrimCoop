#pragma once

struct HookEventPacket;
class GameReader;
class PointerTable;
class WeatherService;
class CalendarService;
class QuestService;
class CombatService;
class InventoryService;
class ActorValueService;
class MagicService;
class CharacterService;

// Dispatch a hook event to the pointer table and all services.
// Extracted from main.cpp so it can be shared between the native client
// and the ReplayHarness test infrastructure.
void DispatchHookEvent(const HookEventPacket& aHookPkt,
                       GameReader& aReader, PointerTable& aPtrs,
                       WeatherService& aWeather, CalendarService& aCalendar,
                       QuestService& aQuest, CombatService& aCombat,
                       InventoryService& aInventory, ActorValueService& aActorValue,
                       MagicService& aMagic, CharacterService& aCharacter);
