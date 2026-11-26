#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

// The clip sets a copy holds for itself (ActionClips' movement idle sets: no process slot) and their loads. A queued
// load names its requester's lock and callback (63997 takes the lock, then calls back), so both live in a holder that
// outlives the copy, and a holder goes only once none of its loads is queued. No load is cancelled: 63997 takes every
// finished load's lock before it reads a cancel (docs/re/systems/animation-file-loads.md).
template <class Slot, class Lock> class HeldClipLoads
{
public:
    struct Holder
    {
        Slot Data{};
        Lock LoadLock{};
        std::vector<std::pair<uint32_t, void*>> Queued; // each queued load's number and callback
        std::vector<void*> Called;                      // callbacks that ran, released at the next request
        uint32_t Latest = 0;                            // the newest load's number: only its set is kept
    };

    // Queues aFormId's load: aCallback(number) makes its callback, aQueue(Holder&, callback) queues it with the
    // holder's LoadLock and returns the load (null: nothing to load). Holders of forms aGone names, with no load
    // queued, are released (aRelease, aReleaseCallback for their callbacks).
    template <class Gone, class MakeCallback, class Queue, class ReleaseCallback, class Release>
    void* Request(uint32_t aFormId, Gone&& aGone, MakeCallback&& aCallback, Queue&& aQueue, ReleaseCallback&& aReleaseCallback,
                  Release&& aRelease)
    {
        std::vector<Holder*> gone;
        Holder* pHolder = nullptr;
        {
            std::scoped_lock _{m_lock};
            for (auto& [cId, pEntry] : m_held)
                if (cId != aFormId && aGone(cId))
                    gone.push_back(pEntry.get());
            auto& pEntry = m_held[aFormId];
            if (!pEntry)
                pEntry = std::make_unique<Holder>();
            pHolder = pEntry.get();
        }
        // Never under m_lock: a holder's lock is held by the file job while it calls Loaded.
        for (Holder* pGone : gone)
            Forget(*pGone, aReleaseCallback, aRelease);
        std::scoped_lock _{pHolder->LoadLock};
        ReleaseCalled(*pHolder, aReleaseCallback);
        const uint32_t cNumber = ++pHolder->Latest;
        void* pCallback = aCallback(cNumber);
        pHolder->Queued.emplace_back(cNumber, pCallback); // before the queue: the loader can call back at once
        void* pLoad = aQueue(*pHolder, pCallback);
        if (!pLoad)
        {
            std::erase_if(pHolder->Queued, [cNumber](const auto& acLoad) { return acLoad.first == cNumber; });
            pHolder->Called.push_back(pCallback);
        }
        return pLoad;
    }

    // Load aNumber's callback, under its holder's LoadLock (63997): aWith(Slot&) takes the set if it is the newest
    // load's. False: an older load's, or its holder is gone; the caller releases the set.
    template <class With> bool Loaded(uint32_t aFormId, uint32_t aNumber, With&& aWith)
    {
        Holder* pHolder = nullptr;
        {
            std::scoped_lock _{m_lock};
            const auto it = m_held.find(aFormId);
            if (it == m_held.end())
                return false;
            pHolder = it->second.get();
        }
        const auto cLoad = std::ranges::find(pHolder->Queued, aNumber, &std::pair<uint32_t, void*>::first);
        if (cLoad != pHolder->Queued.end())
        {
            pHolder->Called.push_back(cLoad->second); // not released here: the engine is still in its call
            pHolder->Queued.erase(cLoad);
        }
        if (aNumber != pHolder->Latest)
            return false;
        aWith(pHolder->Data);
        return true;
    }

    [[nodiscard]] size_t Size() const
    {
        std::scoped_lock _{m_lock};
        return m_held.size();
    }

private:
    template <class ReleaseCallback> static void ReleaseCalled(Holder& aHolder, ReleaseCallback& aReleaseCallback)
    {
        for (void* pCallback : std::exchange(aHolder.Called, {}))
            aReleaseCallback(pCallback);
    }

    // A gone copy's holder, once none of its loads is queued: the file job is past its lock (it unlocks before this
    // lock is free to take) and holds no pointer to it.
    template <class ReleaseCallback, class Release> void Forget(Holder& aHolder, ReleaseCallback& aReleaseCallback, Release& aRelease)
    {
        std::unique_ptr<Holder> pGone;
        {
            std::scoped_lock _{aHolder.LoadLock};
            if (!aHolder.Queued.empty())
                return;
            ReleaseCalled(aHolder, aReleaseCallback);
            aRelease(aHolder.Data);
            std::scoped_lock held{m_lock};
            for (auto it = m_held.begin(); it != m_held.end(); ++it)
                if (it->second.get() == &aHolder)
                {
                    pGone = std::move(it->second);
                    m_held.erase(it);
                    break;
                }
        }
    }

    mutable std::mutex m_lock; // never held while taking a holder's lock
    std::unordered_map<uint32_t, std::unique_ptr<Holder>> m_held;
};
