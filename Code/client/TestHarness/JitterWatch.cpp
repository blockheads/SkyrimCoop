#include "JitterWatch.h"

#include "EngineCalls.h"

#include <Actor.h>
#include <Games/ActorExtension.h>
#include <Misc/BSFixedString.h>
#include <NetImmerse/NiCamera.h>
#include <NetImmerse/NiNode.h>
#include <PlayerCharacter.h>
#include <Sky/Sky.h>
#include <TimeManager.h>
#include <Bridges/IActorBridge.h>
#include <World.h>

#include <algorithm>
#include <cmath>

namespace TestHarness
{
namespace
{
// Less is noise; more in one frame is a teleport.
constexpr float cNoise = 0.02f;
constexpr float cTeleport = 200.f;
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
        watched.Place.Add(cPlace);
        if (pActor->actorState.GetSitSleepState())
        {
            ++watched.Furniture;
            watched.FurniturePath += watched.Place.Path - cPathBefore;
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
            const float cX = pExtension->CopyGoal[0] - cPlace[0], cY = pExtension->CopyGoal[1] - cPlace[1];
            watched.GoalOff = std::max(watched.GoalOff, std::sqrt(cX * cX + cY * cY));
        }
    }
}

nlohmann::json JitterWatch::Read() const
{
    nlohmann::json actors = nlohmann::json::object();
    for (const auto& [cFormId, cWatched] : m_actors)
    {
        auto* pActor = Cast<Actor>(TESForm::GetById(cFormId));
        actors[std::to_string(cFormId)] = {{"copy", pActor && pActor->GetExtension() && pActor->GetExtension()->IsCopy()},
                                           {"frames", cWatched.Frames},
                                           {"place", cWatched.Place.Json()},
                                           {"root", cWatched.Root.Json()},
                                           {"pelvis", cWatched.Pelvis.Json()},
                                           {"heading", cWatched.Heading.Json()},
                                           {"stepped", cWatched.Stepped},
                                           {"furniture", cWatched.Furniture},
                                           {"furniture_path", cWatched.FurniturePath},
                                           {"goal_off", cWatched.GoalOff}};
    }
    return {{"frames", m_frames},        {"hour_back", m_hourBack},      {"biggest_hour_back", m_biggestHourBack},
            {"speed_changes", m_speedChanges}, {"weather_changes", m_weatherChanges},
            {"player", m_player.Json()},   {"camera", m_camera.Json()},    {"actors", actors}};
}
} // namespace TestHarness
