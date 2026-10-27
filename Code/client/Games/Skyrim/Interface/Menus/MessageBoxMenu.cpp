#include <Games/Skyrim/Interface/Menus/MessageBoxMenu.h>

#include <Games/Skyrim/BSCore/BSSpinLock.h>
#include <Games/Skyrim/Interface/UI.h>
#include <VersionDb.h>

namespace MessageBoxes
{
namespace
{
// The box queue (BSTArray<MessageBoxData*>, the shown box last) and its lock (CommonLibSSE-NG GetQueue, GetQueueLock).
struct Queue
{
    MessageBoxData** pData; // 00
    uint32_t capacity;      // 08
    uint32_t pad0C;
    uint32_t size; // 10
};
static_assert(offsetof(Queue, size) == 0x10);

POINTER_SKYRIMSE(Queue, s_queue, 406362);
POINTER_SKYRIMSE(BSSpinLock, s_queueLock, 406365);
POINTER_SKYRIMSE(void, s_scriptCallbackVtable, 217737);

// RemoveMessageFromQueue (52284): out of the queue (it takes the lock), and destroyed.
using TRemove = void(void* apMenu, MessageBoxData* apData);
POINTER_SKYRIMSE(TRemove, s_remove, 52284);

// The queued box whose callback is Message.Show's for aStackId; its callback held.
MessageBoxData* FindForStack(uint32_t aStackId, bool& aLast) noexcept
{
    Queue* pQueue = s_queue.Get();
    BSSpinLock* pLock = s_queueLock.Get();
    if (!pQueue || !pLock)
        return nullptr;
    pLock->Lock();
    MessageBoxData* pFound = nullptr;
    for (uint32_t i = 0; i < pQueue->size && !pFound; ++i)
    {
        MessageBoxData* pData = pQueue->pData[i];
        auto* pCallback = pData ? pData->pCallback : nullptr;
        if (pCallback && *reinterpret_cast<void**>(pCallback) == s_scriptCallbackVtable.GetPtr() &&
            static_cast<ScriptMessageBoxCallback*>(pCallback)->stackId == aStackId)
        {
            pFound = pData;
            aLast = pQueue->size == 1;
            _InterlockedIncrement(reinterpret_cast<volatile long*>(&pCallback->refCount));
        }
    }
    pLock->Unlock();
    return pFound;
}
} // namespace

bool AnswerStack(uint32_t aStackId, uint8_t aResult) noexcept
{
    bool last = false;
    MessageBoxData* pData = FindForStack(aStackId, last);
    if (!pData)
        return false;
    IMessageBoxCallback* pCallback = pData->pCallback;
    s_remove.Get()(nullptr, pData);
    // No movie closes it: hidden here once nothing else is queued.
    if (last)
        if (auto* pQueue = UIMessageQueue::Get())
            pQueue->AddMessage(BSFixedString("MessageBoxMenu"), UIMessage::kHide, nullptr);
    pCallback->Run(aResult);
    if (_InterlockedDecrement(reinterpret_cast<volatile long*>(&pCallback->refCount)) == 0)
        (*reinterpret_cast<void (***)(IMessageBoxCallback*, uint32_t)>(pCallback))[0](pCallback,
                                                                                      1); // the engine's delete
    return true;
}
} // namespace MessageBoxes
