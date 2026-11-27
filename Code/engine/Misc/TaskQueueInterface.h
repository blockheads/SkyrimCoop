#pragma once

struct NiAVObject;

// CommonLibSSE-NG TaskQueueInterface: physics and 3D jobs queued for the main thread.
struct TaskQueueInterface
{
    // TaskQueueInterface::singleton (403759).
    [[nodiscard]] static TaskQueueInterface* Get() noexcept;
    // TaskQueueInterface::QueueRemoveFromWorld (36924): its bodies out of the physics world.
    void QueueRemoveFromWorld(NiAVObject* apObject) noexcept;
};
