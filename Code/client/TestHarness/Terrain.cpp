#include "Terrain.h"

#include "EngineCalls.h"

#include <Actor.h>
#include <Bridges/IActorBridge.h>
#include <Forms/TESForm.h>
#include <World.h>

#include <cmath>
#include <set>

namespace TestHarness
{
namespace
{
constexpr int cMaxHits = 4;           // surfaces kept a column: floors and ledges stacked in a cave
constexpr float cHeadroomCheck = 200.f;
constexpr size_t cMaxColumnsAFrame = 400;

int Round(float aValue) noexcept
{
    return static_cast<int>(std::lround(aValue));
}

// Actors move: the ground is what stays (a copy beside the player is not a ceiling).
bool IsActor(uint32_t aReference) noexcept
{
    return aReference && Cast<Actor>(TESForm::GetById(aReference)) != nullptr;
}
} // namespace

uint32_t TerrainScan::Start(const nlohmann::json& acRequest) noexcept
{
    if (acRequest.contains("at"))
        m_at = acRequest["at"].get<std::array<float, 3>>();
    else
        World::Get().GetActorBridge().GetActorPosition(0x14, m_at[0], m_at[1], m_at[2]);
    const float cRadius = std::clamp(acRequest.value("radius", 1000.f), 100.f, 4000.f);
    m_cell = std::clamp(acRequest.value("cell", 50.f), 10.f, 500.f);
    m_up = acRequest.value("up", 600.f);
    m_down = acRequest.value("down", 1500.f);
    m_toward = acRequest.value("toward", 0u);
    m_cols = m_rows = 2 * static_cast<int>(std::ceil(cRadius / m_cell)) + 1;
    m_west = m_at[0] - m_cols * m_cell / 2.f;
    m_north = m_at[1] + m_rows * m_cell / 2.f;
    m_columns.assign(static_cast<size_t>(m_cols) * m_rows, {});
    m_next = 0;
    m_frames = 0;
    m_running = true;
    m_result = nullptr;
    return ++m_ticket;
}

// Every surface going down the cell's centre, then the headroom over each standable one. False when this frame's
// rays ran out part way (the column is cast again next frame).
bool TerrainScan::ScanColumn(size_t aIndex) noexcept
{
    const int cRow = static_cast<int>(aIndex) / m_cols, cCol = static_cast<int>(aIndex) % m_cols;
    const float x = m_west + (cCol + 0.5f) * m_cell, y = m_north - (cRow + 0.5f) * m_cell;
    const float cBottom = m_at[2] - m_down;
    std::vector<Hit> hits;
    float from = m_at[2] + m_up;
    while (hits.size() < cMaxHits && from > cBottom)
    {
        const float cFrom[3]{x, y, from}, cTo[3]{x, y, cBottom};
        const auto cRay = EngineCalls::CastRay(cFrom, cTo);
        if (!cRay)
            return false;
        if (!cRay->Hit)
            break;
        if (!IsActor(cRay->Reference))
            hits.push_back({cRay->Point[2], cRay->Normal[2], cRay->Reference, cRay->Layer, -1.f});
        from = cRay->Point[2] - 4.f; // on through it, for what lies under
    }
    for (Hit& hit : hits)
    {
        if (hit.NormalZ <= 0.5f)
            continue;
        hit.Headroom = -1.f;
        for (float from = hit.Z + 5.f; from < hit.Z + 5.f + cHeadroomCheck;)
        {
            const float cFrom[3]{x, y, from}, cTo[3]{x, y, hit.Z + 5.f + cHeadroomCheck};
            const auto cRay = EngineCalls::CastRay(cFrom, cTo);
            if (!cRay)
                return false;
            if (!cRay->Hit)
                break;
            if (!IsActor(cRay->Reference))
            {
                hit.Headroom = cRay->Point[2] - hit.Z;
                break;
            }
            from = cRay->Point[2] + 4.f; // through the actor
        }
    }
    m_columns[aIndex] = std::move(hits);
    return true;
}

void TerrainScan::Poll() noexcept
{
    if (!m_running)
        return;
    ++m_frames;
    for (size_t done = 0; done < cMaxColumnsAFrame && m_next < m_columns.size(); ++done)
    {
        if (!ScanColumn(m_next))
            return; // this frame's rays are spent
        ++m_next;
    }
    if (m_next < m_columns.size())
        return;
    m_running = false;
    m_result = Finish();
}

nlohmann::json TerrainScan::Finish() const
{
    std::set<uint32_t> references;
    auto columns = nlohmann::json::array();
    for (const auto& cColumn : m_columns)
    {
        auto hits = nlohmann::json::array();
        for (const Hit& cHit : cColumn)
        {
            hits.push_back({Round(cHit.Z), std::round(cHit.NormalZ * 100.f) / 100.f, cHit.Reference, cHit.Layer,
                            cHit.Headroom < 0.f ? -1 : Round(cHit.Headroom)});
            if (cHit.Reference)
                references.insert(cHit.Reference);
        }
        columns.push_back(hits);
    }
    const auto cCover = EngineCalls::RasterNavmesh(m_at, m_west, m_north, m_cell, m_cols, m_rows, m_toward);
    auto navZ = nlohmann::json::array();
    for (size_t i = 0; i < cCover.z.size(); ++i)
        navZ.push_back(cCover.piece[i] < 0 ? nlohmann::json(nullptr) : nlohmann::json(Round(cCover.z[i])));
    const auto cCapsule = EngineCalls::ReadPlayerCapsule();
    auto obstacles = nlohmann::json::array();
    for (const auto& cObstacle : EngineCalls::ReadObstacleRefs(cCover.navMesh))
        obstacles.push_back({{"ref", cObstacle.ref}, {"name", cObstacle.name}, {"base", cObstacle.base},
                             {"at", {Round(cObstacle.at[0]), Round(cObstacle.at[1]), Round(cObstacle.at[2])}},
                             {"on_navmesh", cObstacle.onNavmesh}});
    auto names = nlohmann::json::object();
    for (const uint32_t cReference : references)
    {
        const auto cLabel = EngineCalls::ReadRefLabel(cReference);
        names[std::to_string(cReference)] = {{"base", cLabel.base}, {"name", cLabel.name}};
    }
    std::array<float, 3> player{};
    World::Get().GetActorBridge().GetActorPosition(0x14, player[0], player[1], player[2]);
    nlohmann::json toward = nullptr;
    if (m_toward)
    {
        if (const auto cLabel = EngineCalls::ReadRefLabel(m_toward); cLabel.found)
            toward = {{"ref", m_toward}, {"name", cLabel.name}, {"at", {Round(cLabel.at[0]), Round(cLabel.at[1]), Round(cLabel.at[2])}}};
    }
    return {{"ok", true},
            {"done", true},
            {"frames", m_frames},
            {"at", {Round(m_at[0]), Round(m_at[1]), Round(m_at[2])}},
            {"cell", m_cell},
            {"cols", m_cols},
            {"rows", m_rows},
            {"west", m_west},
            {"north", m_north},
            {"columns", columns},
            {"navmesh",
             {{"id", cCover.navMesh}, {"z", navZ}, {"piece", cCover.piece}, {"sizes", cCover.pieceSizes},
              {"player", cCover.playerPiece}, {"target", cCover.targetPiece}}},
            {"capsule",
             {{"found", cCapsule.found}, {"radius", cCapsule.radius}, {"height", cCapsule.height},
              {"actor_height", cCapsule.actorHeight}, {"scale", cCapsule.scale}, {"max_slope", cCapsule.maxSlope}}},
            {"player", {Round(player[0]), Round(player[1]), Round(player[2])}},
            {"toward", toward},
            {"obstacles", obstacles},
            {"references", names}};
}

nlohmann::json TerrainScan::Result(uint32_t aTicket) const
{
    if (aTicket != m_ticket || aTicket == 0)
        return {{"ok", false}, {"error", "no such scan (a newer one replaced it?)"}};
    if (m_running)
        return {{"ok", true}, {"done", false}, {"progress", m_next}, {"of", m_columns.size()}, {"frames", m_frames}};
    return m_result;
}
} // namespace TestHarness
