#include <Bridges/DevToolsBridge.h>

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Games/TES.h>
#include <Interface/ConsoleLines.h>
#include <Misc/KillMoveOdds.h>
#include <PlayerCharacter.h>
#include <VersionDb.h>

void DevToolsBridge::RunConsole(const std::string& acLine) noexcept
{
    spdlog::info("DevToolsBridge: console {}", acLine);
    ConsoleLines::Execute(acLine.c_str());
}

uint32_t DevToolsBridge::KillHostiles() noexcept
{
    POINTER_SKYRIMSE(ProcessLists*, s_processLists, 400315);
    using TIsHostileToActor = bool(Actor*, Actor*);
    POINTER_SKYRIMSE(TIsHostileToActor, s_isHostileToActor, 37537); // Actor::IsHostileToActor
    ProcessLists* pLists = *s_processLists.Get();
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pLists || !pPlayer)
        return 0;
    std::vector<uint32_t> hostile;
    for (uint32_t i = 0; i < pLists->highActorHandleArray.length; ++i)
    {
        auto* pActor = Cast<Actor>(TESObjectREFR::GetByHandle(pLists->highActorHandleArray[i]));
        if (!pActor || pActor == pPlayer || pActor->IsDead())
            continue;
        // Players' characters and other games' actors are theirs to kill.
        const ActorExtension* pExtension = pActor->GetExtension();
        if (pExtension && (pExtension->IsPlayer() || pExtension->IsCopy()))
            continue;
        if (s_isHostileToActor.Get()(pActor, pPlayer))
            hostile.push_back(pActor->formID);
    }
    // Not while walking the list: a death can change it.
    for (const uint32_t cFormId : hostile)
        if (auto* pActor = Cast<Actor>(TESForm::GetById(cFormId)))
            pActor->Kill();
    return static_cast<uint32_t>(hostile.size());
}

bool DevToolsBridge::IsGodMode() noexcept
{
    using TIsInGodMode = bool();
    POINTER_SKYRIMSE(TIsInGodMode, s_isInGodMode, 40501);
    return s_isInGodMode.Get()();
}

void DevToolsBridge::SetGodMode(bool aOn) noexcept
{
    using TSetGodMode = void(bool);
    POINTER_SKYRIMSE(TSetGodMode, s_setGodMode, 40500); // PlayerCharacter::SetGodMode
    s_setGodMode.Get()(aOn);
}

void DevToolsBridge::SetKillMoveOdds(std::optional<uint8_t> aOdds) noexcept
{
    KillMoveOdds::Set(aOdds);
}
