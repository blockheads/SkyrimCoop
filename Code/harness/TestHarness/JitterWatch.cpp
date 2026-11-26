#include "TestHarness/JitterWatch.h"

#include "TestHarness/EngineCalls.h"

#include <Actor.h>
#include <core/FormIdComponent.h>
#include <movement/InterpolationComponent.h>
#include <characters/WaitingFor3D.h>
#include <Forms/TESObjectCELL.h>
#include <ActorExtension.h>
#include <Misc/BSFixedString.h>
#include <NetImmerse/NiCamera.h>
#include <NetImmerse/NiNode.h>
#include <PlayerCharacter.h>
#include <Sky/Sky.h>
#include <movement/InterpolationSystem.h>
#include <TimeManager.h>
#include <ports/IActorBridge.h>
#include <core/World.h>

#include <algorithm>
#include <cmath>

namespace TestHarness
{
namespace
{
// Less is noise; more in one frame is a teleport.
constexpr float cNoise = 0.02f;
constexpr float cTeleport = 200.f;
// More a frame than any actor runs.
constexpr float cRace = 32.f;
// Frames kept before and after a race began.
constexpr size_t cBefore = 8;
constexpr uint32_t cAfter = 24;

// A frame of an actor: where it is, where its engine steps it, what its owner last said.
nlohmann::json FrameOf(Actor& aActor)
{
    nlohmann::json entry{{"at", {aActor.position.x, aActor.position.y, aActor.position.z}}};
    if (const auto* pExtension = aActor.GetExtension(); pExtension && pExtension->HasCopyGoal)
        entry["goal"] = pExtension->CopyGoal;
    for (const auto cEntity : World::Get().view<FormIdComponent, InterpolationComponent>())
    {
        if (World::Get().get<FormIdComponent>(cEntity).Id != aActor.formID)
            continue;
        const auto& cShown = World::Get().get<InterpolationComponent>(cEntity);
        entry["points"] = cShown.TimePoints.size();
        entry["tick"] = InterpolationSystem::PlaybackTick(World::Get(), cEntity);
        for (const auto& cPoint : cShown.TimePoints)
            entry["owner"].push_back({cPoint.Tick, cPoint.Position.x, cPoint.Position.y, cPoint.OnGround});
        entry["spawn"] = World::Get().all_of<WaitingFor3D>(cEntity);
    }
    return entry;
}
} // namespace

void JitterWatch::Track::Add(const float* acValue) noexcept
{
    float step[3], length = 0.f, dot = 0.f, lastLength = 0.f;
    for (int i = 0; i < 3; ++i)
    {
        step[i] = Started ? acValue[i] - Last[i] : 0.f;
        length += step[i] * step[i];
        dot += step[i] * Step[i];
        lastLength += Step[i] * Step[i];
        Last[i] = acValue[i];
    }
    if (!Started)
        std::copy_n(acValue, 3, First);
    Started = true;
    length = std::sqrt(length);
    if (length > cTeleport)
    {
        std::fill_n(Step, 3, 0.f);
        return;
    }
    Path += length;
    Biggest = std::max(Biggest, length);
    if (length > cNoise && std::sqrt(lastLength) > cNoise && dot < 0.f)
        ++Reversals;
    std::copy_n(step, 3, Step);
}

nlohmann::json JitterWatch::Track::Json() const
{
    const float cNet = std::sqrt((Last[0] - First[0]) * (Last[0] - First[0]) + (Last[1] - First[1]) * (Last[1] - First[1]) + (Last[2] - First[2]) * (Last[2] - First[2]));
    return {{"path", Path}, {"net", cNet}, {"reversals", Reversals}, {"biggest", Biggest}};
}

void JitterWatch::Start() noexcept
{
    *this = {};
    m_running = true;
}

void JitterWatch::Poll()
{
    if (!m_running)
        return;
    static const BSFixedString s_pelvis("NPC Pelvis [Pelv]");
    ++m_frames;
    if (const TimeData* pTime = TimeData::Get(); pTime && pTime->GameHour)
    {
        const float cHour = pTime->GameHour->f;
        if (m_hour >= 0.f && cHour < m_hour && m_hour - cHour < 12.f)
        {
            ++m_hourBack;
            m_biggestHourBack = std::max(m_biggestHourBack, m_hour - cHour);
        }
        m_hour = cHour;
    }
    const float cSpeed = EngineCalls::GetGlobalTimeMultiplier();
    if (m_speed >= 0.f && cSpeed != m_speed)
        ++m_speedChanges;
    m_speed = cSpeed;
    if (const auto* pPlayer = PlayerCharacter::Get())
    {
        const float cPlace[3]{pPlayer->position.x, pPlayer->position.y, pPlayer->position.z};
        m_player.Add(cPlace);
    }

    if (const NiCamera* pCamera = NiCamera::WorldRoot())
    {
        const float cPlace[3]{pCamera->world.translate.x, pCamera->world.translate.y, pCamera->world.translate.z};
        m_camera.Add(cPlace);
    }
    if (const Sky* pSky = Sky::Get())
    {
        const void* cpWeather = pSky->GetWeather();
        if (m_weather && cpWeather != m_weather)
            ++m_weatherChanges;
        m_weather = cpWeather;
    }

    for (const uint32_t cFormId : World::Get().GetActorBridge().GetLoadedActorFormIds())
    {
        auto* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        NiNode* p3D = pActor && !pActor->IsDeleted() ? pActor->GetNiNode() : nullptr;
        if (!p3D)
            continue;
        auto& watched = m_actors[cFormId];
        ++watched.Frames;
        const float cPlace[3]{pActor->position.x, pActor->position.y, pActor->position.z};
        const float cPathBefore = watched.Place.Path;
        const uint32_t cReversalsBefore = watched.Place.Reversals;
        const bool cMoved = watched.Place.Started;
        const float cX = cPlace[0] - watched.Place.Last[0], cY = cPlace[1] - watched.Place.Last[1], cZ = cPlace[2] - watched.Place.Last[2];
        const float cStep = cMoved ? std::sqrt(cX * cX + cY * cY + cZ * cZ) : 0.f;
        watched.Place.Add(cPlace);
        const bool cUnbroken = watched.LastFrame + 1 == m_frames;
        const bool cStanding = !pActor->LiesInRagdoll() && !pActor->IsDead();
        watched.Racing = cStep > cRace && cStanding ? (cUnbroken ? watched.Racing : 0) + 1 : 0;
        watched.LastFrame = m_frames;
        const auto* pCopy = pActor->GetExtension();
        watched.Copy = pCopy && pCopy->IsCopy();
        watched.Player = pCopy && pCopy->IsOtherPlayer();
        const uint32_t cCell = pActor->parentCell ? pActor->parentCell->formID : 0;
        if (watched.Cell && cCell != watched.Cell)
            ++watched.CellChanges, watched.CellFrames = 0;
        watched.Cell = cCell;
        ++watched.CellFrames;
        watched.Sweep = std::max(watched.Sweep, watched.Racing);
        if (watched.Racing == 1)
            ++watched.Warps;
        if (watched.After < cAfter)
        {
            auto entry = FrameOf(*pActor);
            entry["frame"] = m_frames;
            entry["step"] = cStep;
            watched.Path.push_back(std::move(entry));
            if (watched.Warps)
                ++watched.After;
            else if (watched.Path.size() > cBefore)
                watched.Path.erase(0);
        }
        if (pActor->actorState.GetSitSleepState())
        {
            ++watched.Furniture;
            watched.FurniturePath += watched.Place.Path - cPathBefore;
            watched.FurnitureReversals += watched.Place.Reversals - cReversalsBefore;
        }
        const float cRoot[3]{p3D->world.translate.x, p3D->world.translate.y, p3D->world.translate.z};
        watched.Root.Add(cRoot);
        if (const NiAVObject* pPelvis = p3D->GetByName(s_pelvis))
        {
            const float cOff[3]{pPelvis->world.translate.x - cRoot[0], pPelvis->world.translate.y - cRoot[1], pPelvis->world.translate.z - cRoot[2]};
            watched.Pelvis.Add(cOff);
        }
        // In degrees, to read beside the units.
        const float cHeading[3]{pActor->rotation.z * 57.29578f, 0.f, 0.f};
        watched.Heading.Add(cHeading);
        if (const auto* pExtension = pActor->GetExtension(); pExtension && pExtension->HasCopyGoal)
        {
            ++watched.Stepped;
            const float cOffX = pExtension->CopyGoal[0] - cPlace[0], cOffY = pExtension->CopyGoal[1] - cPlace[1];
            watched.GoalOff = std::max(watched.GoalOff, std::sqrt(cOffX * cOffX + cOffY * cOffY));
        }
    }
}

nlohmann::json JitterWatch::Read() const
{
    nlohmann::json actors = nlohmann::json::object();
    for (const auto& [cFormId, cWatched] : m_actors)
    {
        actors[std::to_string(cFormId)] = {{"copy", cWatched.Copy},
                                           {"frames", cWatched.Frames},
                                           {"place", cWatched.Place.Json()},
                                           {"root", cWatched.Root.Json()},
                                           {"pelvis", cWatched.Pelvis.Json()},
                                           {"heading", cWatched.Heading.Json()},
                                           {"stepped", cWatched.Stepped},
                                           {"warps", cWatched.Warps},
                                           {"sweep", cWatched.Sweep},
                                           {"player", cWatched.Player},
                                           {"cell_changes", cWatched.CellChanges},
                                           {"cell_frames", cWatched.CellFrames},
                                           {"race", cWatched.Path},
                                           {"furniture", cWatched.Furniture},
                                           {"furniture_path", cWatched.FurniturePath},
                                           {"furniture_reversals", cWatched.FurnitureReversals},
                                           {"goal_off", cWatched.GoalOff}};
    }
    return {{"frames", m_frames},        {"hour_back", m_hourBack},      {"biggest_hour_back", m_biggestHourBack},
            {"speed_changes", m_speedChanges}, {"weather_changes", m_weatherChanges},
            {"player", m_player.Json()},   {"camera", m_camera.Json()},    {"actors", actors}};
}
} // namespace TestHarness
