#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// The clip sets a copy holds for itself (ActionClips' movement idle sets: no process slot) and their loads. A queued
// load holds its requester's lock until its callback (63997 takes it first), so the lock is the holder's, which outlives
// the copy, and a load still queued is cancelled (63971) before its holder goes, as MovementControllerNPC's (41692).
// docs/re/systems/animation-file-loads.md
template <class Slot, class Lock> class HeldClipLoads
{
public:
    struct Holder
    {
        Slot Data{};
        Lock LoadLock{};
        void* Load = nullptr; // the load under way, until its callback or a cancel
        bool Finished = false;
    };

    // Queues aFormId's load: aQueue(Holder&) passes the holder's LoadLock and returns the load (null: nothing to load).
    // A load under way for it is cancelled first; holders of forms aGone names are cancelled and released.
    template <class Gone, class Queue, class Cancel, class Release>
    void* Request(uint32_t aFormId, Gone&& aGone, Queue&& aQueue, Cancel&& aCancel, Release&& aRelease)
    {
        std::vector<std::unique_ptr<Holder>> gone;
        Holder* pHolder = nullptr;
        {
            std::scoped_lock _{m_lock};
            for (auto it = m_held.begin(); it != m_held.end();)
            {
                if (it->first != aFormId && aGone(it->first))
                {
                    gone.push_back(std::move(it->second));
                    it = m_held.erase(it);
                }
                else
                    ++it;
            }
            auto& pEntry = m_held[aFormId];
            if (!pEntry)
                pEntry = std::make_unique<Holder>();
            pHolder = pEntry.get();
        }
        // Never under m_lock: a cancel takes the manager's lock, whose holder may be calling Loaded.
        for (auto& pGone : gone)
            Forget(*pGone, aCancel, aRelease);
        std::scoped_lock _{pHolder->LoadLock};
        if (pHolder->Load)
            aCancel(pHolder->Load);
        pHolder->Load = nullptr;
        pHolder->Finished = false;
        void* pLoad = aQueue(*pHolder);
        if (!pHolder->Finished) // the loader can call back at once
            pHolder->Load = pLoad;
        return pLoad;
    }

    // A load's callback, under its holder's LoadLock (63997): aWith(Slot&) takes the set. False: its holder is gone.
    template <class With> bool Loaded(uint32_t aFormId, With&& aWith)
    {
        Holder* pHolder = nullptr;
        {
            std::scoped_lock _{m_lock};
            const auto it = m_held.find(aFormId);
            if (it == m_held.end())
                return false;
            pHolder = it->second.get();
        }
        pHolder->Load = nullptr;
        pHolder->Finished = true;
        aWith(pHolder->Data);
        return true;
    }

    [[nodiscard]] size_t Size() const
    {
        std::scoped_lock _{m_lock};
        return m_held.size();
    }

private:
    template <class Cancel, class Release> static void Forget(Holder& aHolder, Cancel& aCancel, Release& aRelease)
    {
        {
            std::scoped_lock _{aHolder.LoadLock};
            if (aHolder.Load)
                aCancel(aHolder.Load);
            aHolder.Load = nullptr;
        }
        aRelease(aHolder.Data);
    }

    mutable std::mutex m_lock; // never held across a load, a cancel or a callback
    std::unordered_map<uint32_t, std::unique_ptr<Holder>> m_held;
};
