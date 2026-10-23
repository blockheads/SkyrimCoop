#include <PlayerEvents.h>
#include <HookSwitch.h>

#include <Events/EventDispatcher.h>
#include <Events/PlayerFormChangeEvent.h>
#include <Forms/TESActorBase.h>
#include <Games/Events.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>
#include <World.h>

#include <atomic>

namespace PlayerEvents
{
namespace
{
// The player's base form (TESNPC "Player").
constexpr uint32_t cPlayerBaseId = 0x7;

std::atomic<uint32_t> s_users{0};

void Raise() noexcept
{
    if (s_users.load(std::memory_order_relaxed))
        World::Get().GetRunner().Trigger(PlayerFormChangeEvent{});
}

struct RaceSink final : BSTEventSink<TESSwitchRaceCompleteEvent>
{
    BSTEventResult OnEvent(const TESSwitchRaceCompleteEvent* apEvent, const EventDispatcher<TESSwitchRaceCompleteEvent>*) override
    {
        if (apEvent && apEvent->subject && apEvent->subject == PlayerCharacter::Get())
            Raise();
        return BSTEventResult::kOk;
    }
};

RaceSink s_race;

using TSetLevel = void(TESActorBaseData*, uint16_t);
TSetLevel* RealSetLevel = nullptr;

// TESActorBaseData::SetLevel (14385): every level write (a level-up after its LevelIncrease event, advlevel, setlevel).
void HookSetLevel(TESActorBaseData* apData, uint16_t aLevel)
{
    RealSetLevel(apData, aLevel);
    const auto* pForm = reinterpret_cast<const TESForm*>(reinterpret_cast<const uint8_t*>(apData) - offsetof(TESActorBase, actorData));
    if (pForm->formID == cPlayerBaseId)
        Raise();
}

Hooks::Initializer s_playerHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TSetLevel, s_setLevel, 14385);
        RealSetLevel = s_setLevel.Get();
        HOOK(&RealSetLevel, HookSetLevel);
    });
} // namespace

void Start() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || s_users++ != 0)
        return;
    pEvents->switchRaceCompleteEvent.RegisterSink(&s_race);
}

void Stop() noexcept
{
    auto* pEvents = EventDispatcherManager::Get();
    if (!pEvents || !s_users || --s_users != 0)
        return;
    pEvents->switchRaceCompleteEvent.UnRegisterSink(&s_race);
}
} // namespace PlayerEvents
