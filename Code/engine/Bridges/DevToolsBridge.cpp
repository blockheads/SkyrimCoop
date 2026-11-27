#include <Bridges/DevToolsBridge.h>

#include <Actor.h>
#include <Forms/TESFaction.h>
#include <ActorExtension.h>
#include <TES.h>
#include <Interface/ConsoleLines.h>
#include <Misc/KillMoveOdds.h>
#include <PlayerCharacter.h>
#include <tools/DevCommands.h>
#include <VersionDb.h>

#include <cmath>

void DevToolsBridge::RunConsole(const std::string& acLine) noexcept
{
    spdlog::info("DevToolsBridge: console {}", acLine);
    ConsoleLines::Execute(acLine.c_str());
}

uint32_t DevToolsBridge::KillHostiles() noexcept
{
    ProcessLists* pLists = ProcessLists::Get();
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
        if (pActor->IsHostileToActor(pPlayer))
            hostile.push_back(pActor->formID);
    }
    // Not while walking the list: a death can change it.
    for (const uint32_t cFormId : hostile)
        if (auto* pActor = Cast<Actor>(TESForm::GetById(cFormId)))
            pActor->Kill();
    return static_cast<uint32_t>(hostile.size());
}

uint32_t DevToolsBridge::SpawnPartyHorses(uint32_t aAtFormId, uint32_t aCount) noexcept
{
    const auto* pAt = Cast<TESObjectREFR>(TESForm::GetById(aAtFormId));
    auto* pBase = Cast<TESBoundObject>(TESForm::GetById(DevCommands::cHorse));
    auto* pHorseFaction = Cast<TESFaction>(TESForm::GetById(DevCommands::cPlayerHorseFaction));
    auto* pPlayerFaction = TESForm::GetById(DevCommands::cPlayerFaction);
    ModManager* pManager = ModManager::Get();
    if (!pAt || !pBase || !pHorseFaction || !pPlayerFaction || !pManager)
        return 0;
    uint32_t placed = 0;
    for (uint32_t i = 0; i < aCount; ++i)
    {
        // In a row beside the actor, 200 units out to its right, 150 apart (heading: rotation z).
        const float cHeading = pAt->rotation.z;
        const float cOut = 200.f + 150.f * static_cast<float>(i);
        const NiPoint3 cPosition(glm::vec3(pAt->position.x + std::cos(cHeading) * cOut,
                                           pAt->position.y - std::sin(cHeading) * cOut, pAt->position.z));
        auto* pHorse = Cast<Actor>(TESObjectREFR::GetByHandle(
            pManager->Place(pBase, cPosition, pAt->rotation, pAt->GetParentCellEx(), pAt->GetWorldSpace())));
        if (!pHorse)
            continue;
        // As the stable's sale (TIF__00068CF4): AddToFaction is SetFactionRank 37677, SetFactionOwner SetOwner 20198.
        pHorse->SetFactionRank(pHorseFaction, 1);
        pHorse->SetOwner(pPlayerFaction);
        ++placed;
    }
    spdlog::info("DevToolsBridge: {} party horses placed at {:X}", placed, aAtFormId);
    return placed;
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
