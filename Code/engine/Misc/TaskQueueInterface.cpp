#include <Misc/TaskQueueInterface.h>
#include <VersionDb.h>

TaskQueueInterface* TaskQueueInterface::Get() noexcept
{
    POINTER_SKYRIMSE(TaskQueueInterface*, s_singleton, 403759);
    return *s_singleton.Get();
}

void TaskQueueInterface::QueueRemoveFromWorld(NiAVObject* apObject) noexcept
{
    using TRemoveFromWorld = void(TaskQueueInterface*, NiAVObject*);
    POINTER_SKYRIMSE(TRemoveFromWorld, s_removeFromWorld, 36924);
    s_removeFromWorld.Get()(this, apObject);
}
