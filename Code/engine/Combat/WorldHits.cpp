#include <Combat/WorldHits.h>

#include <Actor.h>
#include <Combat/HitData.h>
#include <Combat/HitOwnership.h>
#include <HookSwitch.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <RTTI.h>
#include <VersionDb.h>

#include <utility>

namespace
{
thread_local bool t_worldHit = false;
thread_local bool t_judgedHere = false;

// TESObjectREFR::ProcessHit (38586): a hit with no actor aggressor and no mark of ours is the world's, scoped around its
// DoDamage. Queued off the main thread it copies the hit whole (ProcessHit_QueueTask), the mark included.
using TProcessHit = uint64_t(TESObjectREFR*, HitData*);
TProcessHit* RealProcessHit = nullptr;

uint64_t HookProcessHit(TESObjectREFR* apTarget, HitData* apHit)
{
    if (apHit && t_judgedHere)
        apHit->flags |= HitData::kJudgedHere;
    t_worldHit = apHit && !(apHit->flags & HitData::kJudgedHere) && !Cast<Actor>(TESObjectREFR::GetByHandle(apHit->aggressor));
    const uint64_t cResult = RealProcessHit(apTarget, apHit);
    t_worldHit = false;
    return cResult;
}

// Actor::TakeImpactFromBody (38185): a body the actor's own move step met hits it. Judged in that actor's own game,
// not on another player's copy (HitRules::JudgesImpact).
using TImpactFromBody = uint64_t(Actor**, uint64_t, void*);
TImpactFromBody* RealImpactFromBody = nullptr;

uint64_t HookImpactFromBody(Actor** appThis, uint64_t a2, void* apContact)
{
    Actor* pActor = appThis ? *appThis : nullptr;
    if (pActor && !HitRules::JudgesImpact(HitRules::PartyOf(pActor)))
        return 1;
    const WorldHits::JudgedHere cJudged;
    return RealImpactFromBody(appThis, a2, apContact);
}

Hooks::Initializer s_worldHitHooks([] {
    const HookGate cGate(__FILE__);
    HOOK_ID(38586, &RealProcessHit, HookProcessHit);
    HOOK_ID(38185, &RealImpactFromBody, HookImpactFromBody);
});
} // namespace

bool WorldHits::Current() noexcept
{
    return t_worldHit;
}

WorldHits::JudgedHere::JudgedHere() noexcept
    : m_before(std::exchange(t_judgedHere, true))
{
}

WorldHits::JudgedHere::~JudgedHere() noexcept
{
    t_judgedHere = m_before;
}
