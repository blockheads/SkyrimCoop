#include <TiltedOnlinePCH.h>

#include <Events/EventDispatcher.h>

namespace details
{
void InternalRegisterSink(void* apEventDispatcher, void* apSink) noexcept
{
    using TRegisterSink = void(void*, void* apSink);

    // SkyrimVM ctor RegisterSinks
    POINTER_SKYRIMSE(TRegisterSink, s_registerSink, 54425);

    s_registerSink(apEventDispatcher, apSink);
}

void InternalUnRegisterSink(void* apEventDispatcher, void* apSink) noexcept
{
    using TUnRegisterSink = void(void*, void* apSink);

    // SkyrimVM dtor UnRegisterSinks
    POINTER_SKYRIMSE(TUnRegisterSink, s_unregisterSink, 54522);

    s_unregisterSink(apEventDispatcher, apSink);
}

void InternalPushEvent(void* apEventDispatcher, void* apEvent) noexcept
{
    using TPushEvent = void(void*, void* apSink);

    // "Failed to setup moving reference because it has no parent cell or no 3D" after interlocked
    POINTER_SKYRIMSE(TPushEvent, s_pushEvent, 19364);

    s_pushEvent(apEventDispatcher, apEvent);
}
} // namespace details

EventDispatcherManager* EventDispatcherManager::Get() noexcept
{
    using TGetEventDispatcherManager = EventDispatcherManager*();

    POINTER_SKYRIMSE(TGetEventDispatcherManager, s_getEventDispatcherManager, 14298);

    return s_getEventDispatcherManager.Get()();
}
