#include <DragonSouls.h>

#include <Actor.h>
#include <HookSwitch.h>

#include <utility>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <VersionDb.h>

namespace DragonSouls
{
namespace
{
thread_local bool t_silent = false;

// ActorValueUpdateSinks[133], from Actor::SendActorValueUpdate (38470).
using TSoulSink = void(Actor*, uint32_t aValue, float aOld, float aDelta, Actor* apCause);
TSoulSink* RealSoulSink = nullptr;

void HookSoulSink(Actor* apActor, uint32_t aValue, float aOld, float aDelta, Actor* apCause)
{
    if (!t_silent)
        RealSoulSink(apActor, aValue, aOld, aDelta, apCause);
}

Hooks::Initializer s_hooks([]() {
    const HookGate cGate(__FILE__);
    HOOK_ID(38505, &RealSoulSink, HookSoulSink);
});
} // namespace

ScopedSilence::ScopedSilence() noexcept
    : m_outer(std::exchange(t_silent, true))
{
}

ScopedSilence::~ScopedSilence() noexcept
{
    t_silent = m_outer;
}
} // namespace DragonSouls
