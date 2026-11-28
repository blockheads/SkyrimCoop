#include <physics/HostBodies.h>

#include <core/HostRouter.h>
#include <characters/HostCharacters.h>
#include <physics/HostBodiesGame.h>
#include <core/HostNetwork.h>
#include <core/HostPlayers.h>

#include <Messages/Relayed.h>
#include <physics/BodyPlayback.h>
#include <physics/BodyPrediction.h>
#include <physics/BodyRelevance.h>
#include <physics/BodyTolerances.h>
#include <physics/BodyLog.h>

#include <algorithm>

namespace
{
// States per message: about 1 KB, under a datagram's MTU (an unreliable fragment lost loses the packet).
constexpr size_t kStatesPerMessage = 24;

bool Apart(const BodyFrame& acA, const BodyFrame& acB) noexcept
{
    const auto cApart = BodyPrediction::Between(acA, acB);
    return cApart.Distance > BodyTolerances::kSendDistance || cApart.Angle > BodyTolerances::kSendAngle;
}
} // namespace

HostBodies::HostBodies(HostRouter& aRouter, HostNetwork& aNetwork, const HostPlayers& acPlayers, const HostCharacters& acCharacters,
                       const HostBodiesGame& acGame) noexcept
    : m_network(aNetwork)
    , m_players(acPlayers)
    , m_characters(acCharacters)
    , m_game(acGame)
{
    aRouter.On(*this, &HostBodies::OnStates);
}

uint32_t HostBodies::NumberOf(ConnectionId_t aPlayer) noexcept
{
    const auto [it, cNew] = m_numbers.try_emplace(aPlayer, m_lastNumber + 1);
    if (cNew)
        ++m_lastNumber;
    return it->second;
}

bool HostBodies::IsGuest(uint32_t aPlayer) const noexcept
{
    const auto it = m_numbers.find(m_players.Host());
    return aPlayer != BodyAuthority::kNobody && (it == m_numbers.end() || aPlayer != it->second);
}

bool HostBodies::MayClaim(const BodyKey& acKey, ConnectionId_t aSender) const noexcept
{
    // A guest's own (the host plays it, keyframed if held) stays its; else only what the host's physics simulates.
    if (!acKey.Actor)
    {
        const auto cRow = m_rows.find(acKey);
        if (IsGuest(m_authority.Of(acKey).Player) || (cRow != m_rows.end() && cRow->second.FromGuest))
            return true;
        return !(m_game.IsKeyframed && m_game.IsKeyframed(acKey));
    }
    // Its owner's ragdoll; a corpse, anyone's to drag (40555 grabs any dead actor).
    return m_characters.OwnerOf(acKey.Actor) == aSender || (m_game.IsDead && m_game.IsDead(acKey.Actor));
}

bool HostBodies::IsCopy(const BodyKey& acKey) const noexcept
{
    return acKey.Actor && m_characters.OwnerOf(acKey.Actor) != m_players.Host();
}

void HostBodies::OnStates(const BodyStatesRequest& acMessage, ConnectionId_t aSender) noexcept
{
    for (const BodyState& cState : acMessage.States)
    {
        const BodyKey& cKey = cState.Frame.Key;
        if (!MayClaim(cKey, aSender))
            continue;
        // A body its own physics moved that it never heard of: the host's state of it goes on the stream.
        if (cState.Author == BodyAuthor::kNone)
        {
            if (!m_rows.contains(cKey) && m_game.ReadBodies)
                if (const auto cPoses = m_game.ReadBodies({cKey}); !cPoses.empty())
                {
                    m_rows[cKey] = {.Frame = {.Key = cKey, .Position = cPoses[0].Position, .Rotation = cPoses[0].Rotation}, .Tick = m_tick};
                    m_authority.Rested(cKey);
                    spdlog::info("HostBodies: {:x} moved {} by itself, gets the host's", aSender, BodyLog::Describe(cKey));
                }
            continue;
        }
        const uint32_t cWas = m_authority.Of(cKey).Player;
        const uint32_t cSender = NumberOf(aSender);
        const bool cAccepted = m_authority.Accept(cSender, cState);
        spdlog::debug("HostBodies: {:x}'s state of {}: auth {} hold {} held {} rest {} at ({:.1f}, {:.1f}) {}", aSender, BodyLog::Describe(cKey),
                      cState.AuthSeq, cState.HoldSeq, cState.Held, cState.AtRest, cState.Frame.Position.x, cState.Frame.Position.y,
                      cAccepted ? "taken" : "refused");
        if (!cAccepted)
            continue;
        if (cWas != cSender && !cState.AtRest)
            spdlog::info("HostBodies: {:x} {} {}", aSender, cState.Held ? "holds" : "moves", BodyLog::Describe(cKey));
        Row& row = m_rows[cKey];
        row.Frame = cState.Frame;
        row.Placed = false;
        row.HostMoved = false;
        row.Released = 0;
        row.Tick = acMessage.Tick;
        row.Heard = m_tick; // this game's clock: a guest's may run ahead
        row.Quiet = 0;
        // Its rest: the host's game puts it, the stream confirms it to its mover.
        row.FromGuest = cState.AtRest;
        row.Given = false;
        if (cState.AtRest)
            spdlog::info("HostBodies: {:x}'s {} rests at ({:.0f}, {:.0f}, {:.0f})", aSender, BodyLog::Describe(cKey),
                         cState.Frame.Position.x, cState.Frame.Position.y, cState.Frame.Position.z);
    }
}

void HostBodies::Unclaim(const BodyKey& acKey, const char* acpWhy) noexcept
{
    const uint32_t cWas = m_authority.Of(acKey).Player;
    m_authority.Unclaim(acKey);
    const auto it = m_rows.find(acKey);
    if ((IsGuest(cWas) || (it != m_rows.end() && it->second.FromGuest)) && m_game.EndPlayback)
        m_game.EndPlayback(acKey, it != m_rows.end() ? it->second.Tick : m_tick);
    if (it != m_rows.end())
    {
        it->second.FromGuest = false;
        it->second.Tick = m_tick; // the host's word on it now: every game hears it is nobody's
        // A guest's corpse another player moved: kept until its owner heard it is nobody's (it plays that player's states
        // on it till then).
        if (IsCopy(acKey) && cWas != NumberOf(m_characters.OwnerOf(acKey.Actor)))
            it->second.Released = m_tick;
    }
    spdlog::info("HostBodies: {} is nobody's again: {}", BodyLog::Describe(acKey), acpWhy);
}

void HostBodies::TakeHands(uint64_t aTick, const std::vector<BodyKey>& acHeld) noexcept
{
    const uint32_t cHost = NumberOf(m_players.Host());
    const auto cTake = [&](const BodyKey& acKey, bool aGrab) {
        const uint32_t cWas = m_authority.Of(acKey).Player;
        if (!(aGrab ? m_authority.Grab(acKey, cHost, aTick) : m_authority.Touch(acKey, cHost, aTick)))
            return;
        const auto it = m_rows.find(acKey);
        const bool cPutting = it != m_rows.end() && it->second.FromGuest;
        // A guest's corpse: its owner's frames and rest stop playing on the copy here.
        if ((IsGuest(cWas) || cPutting || IsCopy(acKey)) && m_game.EndPlayback)
            m_game.EndPlayback(acKey, it != m_rows.end() ? it->second.Tick : m_tick);
        if (cPutting)
            it->second.FromGuest = false;
        if (cWas != cHost)
            spdlog::info("HostBodies: the host's player {} {}", aGrab ? "holds" : "moves", BodyLog::Describe(acKey));
    };
    if (m_game.TakeTouches)
        for (const BodyKey& cKey : m_game.TakeTouches())
            cTake(cKey, false);
    for (const BodyKey& cKey : m_held)
        if (std::ranges::find(acHeld, cKey) == acHeld.end())
            m_authority.Release(cKey, cHost);
    for (const BodyKey& cKey : acHeld)
        if (std::ranges::find(m_held, cKey) == m_held.end())
            cTake(cKey, true);
    m_held = acHeld;
    // What its bodies touch with nobody's claim is its too (recursively).
    std::vector<BodyKey> mine;
    for (const BodyKey& cKey : m_authority.Bodies(cHost))
        if (!cKey.Actor)
            mine.push_back(cKey);
    if (mine.empty() || !m_game.ReadTouching)
        return;
    const auto cContacts = m_game.ReadTouching(mine);
    std::vector<BodyKey> others;
    for (const auto& [cMine, cOther] : cContacts)
        others.push_back(cOther);
    std::vector<std::pair<BodyKey, std::optional<BodyPose>>> paired;
    for (const BodyPose& cPose : m_game.ReadBodies ? m_game.ReadBodies(others) : std::vector<BodyPose>{})
        paired.emplace_back(cPose.Key, cPose);
    m_paired.Update(paired, [this](const BodyKey& acKey) -> std::optional<BodyPose> {
        const auto it = m_rows.find(acKey);
        if (it == m_rows.end() || !m_authority.Of(acKey).AtRest)
            return std::nullopt;
        return BodyPose{.Key = acKey, .Position = it->second.Frame.Position, .Rotation = it->second.Frame.Rotation};
    });
    (void)m_authority.Spread(cHost, cContacts, aTick, [this](const BodyKey& acKey) { return m_paired.Moved(acKey); });
}

void HostBodies::Read(uint64_t aTick) noexcept
{
    if (!m_game.TakeBodies)
        return;
    HostBodyReads reads = m_game.TakeBodies(aTick);
    TakeHands(aTick, reads.Held);
    const uint32_t cHost = NumberOf(m_players.Host());
    // A copy's bodies are its owner's, but a corpse the host's player took (until it rests).
    const auto cNotHosts = [&](const BodyKey& acKey) { return IsCopy(acKey) && !m_authority.Is(acKey, cHost); };
    m_motions.Mark(reads.Awake, aTick);
    std::set<BodyKey> awakeActors;
    for (const BodyFrame& cFrame : reads.Awake)
    {
        const BodyKey& cKey = cFrame.Key;
        if (IsGuest(m_authority.Of(cKey).Player) || cNotHosts(cKey))
            continue; // played here from its authority's states
        Row& row = m_rows[cKey];
        row.HostMoved = IsCopy(cKey);
        // A guest's rest being put here (glided, a step awake) is not the host's move; its rest read decides.
        if (row.FromGuest)
            continue;
        row.Placed = false;
        row.Frame = cFrame;
        row.Tick = aTick;
        m_authority.Moved(cKey);
        if (cKey.Actor)
            awakeActors.insert(cKey);
    }
    for (const BodyPose& cRest : reads.Rested)
    {
        const BodyKey& cKey = cRest.Key;
        if (IsGuest(m_authority.Of(cKey).Player) || cNotHosts(cKey))
            continue;
        m_motions.Forget(cKey);
        Row& row = m_rows[cKey];
        // Read still before the host's game was given the guest's rest: not its word yet.
        if (row.FromGuest && !row.Given)
            continue;
        const BodyFrame cFrame{.Key = cKey, .Position = cRest.Position, .Rotation = cRest.Rotation};
        // A guest's rest, put here: its exact pose stands.
        if (row.FromGuest && !Apart(cFrame, row.Frame))
        {
            row.FromGuest = false;
            continue;
        }
        row.FromGuest = false;
        // Woken where it was put and asleep there again: still the game's placement (no physics settled it).
        row.Placed = row.Placed && !Apart(cFrame, row.Frame);
        row.Frame = cFrame;
        row.Tick = aTick;
        m_authority.Rested(cKey);
    }
    // A moving row nobody has spoken for since kLostMs (its body unloaded, deleted, or asleep unread): where the host's
    // game has it now, at rest; gone, forgotten. Streamed on stale, every game dropped it as lost on arrival.
    std::vector<BodyKey> stale;
    for (const auto& [cKey, cRow] : m_rows)
        if (const auto cClaim = m_authority.Of(cKey);
            !cKey.Actor && !cClaim.AtRest && !IsGuest(cClaim.Player) && !cRow.FromGuest && aTick > cRow.Tick + BodyPlayback::kLostMs)
            stale.push_back(cKey);
    const auto cNow = stale.empty() || !m_game.ReadBodies ? std::vector<BodyPose>{} : m_game.ReadBodies(stale);
    for (const BodyKey& cKey : stale)
    {
        const auto it = std::ranges::find(cNow, cKey, &BodyPose::Key);
        if (it == cNow.end())
        {
            m_rows.erase(cKey);
            m_authority.Forget(cKey);
            m_motions.Forget(cKey);
            for (auto& [cPlayer, stream] : m_streams)
                stream.Forget(cKey);
            continue;
        }
        Row& row = m_rows[cKey];
        row.Frame = {.Key = cKey, .Position = it->Position, .Rotation = it->Rotation};
        row.Tick = aTick;
        m_authority.Rested(cKey);
    }
    // An actor's ragdoll rests as its record part (BodyService::DeclareRagdolls): its rows end with its motion. A guest's
    // corpse the host's player moved keeps its rest for its owner while it lies dead.
    std::erase_if(m_rows, [&](const auto& acRow) {
        const BodyKey& cKey = acRow.first;
        if (!cKey.Actor || awakeActors.contains(cKey) || acRow.second.FromGuest || IsGuest(m_authority.Of(cKey).Player) ||
            (acRow.second.HostMoved && m_game.IsDead && m_game.IsDead(cKey.Actor)) ||
            (acRow.second.Released && aTick < acRow.second.Released + BodyTolerances::kSilentMs))
            return false;
        m_authority.Forget(cKey);
        m_motions.Forget(cKey);
        for (auto& [cPlayer, stream] : m_streams)
            stream.Forget(cKey);
        return true;
    });
}

void HostBodies::Stream(uint64_t aTick) noexcept
{
    const ConnectionId_t cHost = m_players.Host();
    for (const ConnectionId_t cPlayer : m_players.Connections())
    {
        const bool cIsHost = cPlayer == cHost;
        const uint32_t cNumber = NumberOf(cPlayer);
        const auto cPosition = cIsHost || !m_game.ReadPlayerPosition ? std::nullopt : m_game.ReadPlayerPosition(m_players.PlayerIdOf(cPlayer));
        std::vector<BodyState> states;
        for (const auto& [cKey, cRow] : m_rows)
        {
            const auto cClaim = m_authority.Of(cKey);
            // Not what that game simulates: the host's game simulates nobody's and its player's (but a guest's rest it
            // is still putting); a guest, its own.
            if (cIsHost ? !(IsGuest(cClaim.Player) || cRow.FromGuest) : cClaim.Player == cNumber)
                continue;
            // Moving: only near that game's character (BodyRelevance), its budget for what it sees.
            if (!cClaim.AtRest && !cIsHost && cPosition && !BodyRelevance::IsNear(cRow.Frame.Position, *cPosition))
                continue;
            states.push_back(m_authority.Stamp(cRow.Frame, cNumber));
            states.back().Placed = states.back().AtRest && cRow.Placed;
        }
        // By the time each state was read: a guest's at its clock's tick.
        std::map<uint64_t, std::vector<BodyState>> byTick;
        for (BodyState& state : m_streams[cPlayer].Due(states, aTick))
        {
            const auto it = m_rows.find(state.Frame.Key);
            if (cIsHost && it != m_rows.end() && state.AtRest)
                it->second.Given = true;
            byTick[it != m_rows.end() ? it->second.Tick : aTick].push_back(std::move(state));
        }
        for (const auto& [cTick, cStates] : byTick)
            for (size_t first = 0; first < cStates.size(); first += kStatesPerMessage)
            {
                NotifyBodyStates message;
                message.Tick = cTick;
                message.States.assign(cStates.begin() + first, cStates.begin() + std::min(cStates.size(), first + kStatesPerMessage));
                m_network.Send(cPlayer, message, Net::kUnreliable);
            }
    }
}

void HostBodies::Update(uint64_t aTick) noexcept
{
    m_tick = aTick;
    if (m_players.Host())
        (void)NumberOf(m_players.Host());
    // An authority gone silent (its game stalled, or left without a word): nobody's again, where it is.
    for (auto& [cKey, row] : m_rows)
        if (const auto cClaim = m_authority.Of(cKey); IsGuest(cClaim.Player) && !cClaim.AtRest && ++row.Quiet > kQuietUpdates &&
                                                      aTick > row.Heard + BodyTolerances::kSilentMs)
            Unclaim(cKey, "its authority went silent");
    for (auto& [cKey, row] : m_rows)
        if (row.FromGuest && row.Given && aTick > row.Heard + BodyTolerances::kSilentMs)
            row.FromGuest = false;
    Read(aTick);
    Stream(aTick);
}

void HostBodies::OnPlayerLeft(ConnectionId_t aPlayer) noexcept
{
    for (const BodyKey& cKey : m_authority.Bodies(NumberOf(aPlayer)))
        Unclaim(cKey, "its authority left");
    m_streams.erase(aPlayer);
    m_numbers.erase(aPlayer);
}

void HostBodies::OnHostPlaced(const GameId& acObject, const glm::vec3& acWhere) noexcept
{
    // A body never seen moving gets its row now: a guest shows it from the stream, not the move row.
    if (const BodyKey cFirst{.Object = acObject}; !m_rows.contains(cFirst) && m_game.ReadBodies)
        if (const auto cPoses = m_game.ReadBodies({cFirst}); !cPoses.empty())
        {
            m_rows[cFirst] = {.Frame = {.Key = cFirst, .Position = acWhere, .Rotation = cPoses[0].Rotation}, .Tick = m_tick, .Placed = true};
            // By a newer number: a touch from before a game heard of it does not stand.
            m_authority.Unclaim(cFirst);
            m_authority.Rested(cFirst);
            return;
        }
    for (auto& [cKey, row] : m_rows)
    {
        if (cKey.Actor || cKey.Object != acObject)
            continue;
        if (glm::distance(acWhere, row.Frame.Position) <= kPlacedApart)
        {
            // Near a state a guest's game steers it by: that state's. At rest and nobody's: any new pose is the game's.
            const auto cNow = m_authority.Of(cKey).AtRest && !row.FromGuest && m_game.ReadBodies ? m_game.ReadBodies({cKey}) : std::vector<BodyPose>{};
            if (cNow.empty())
                continue;
            const auto cApart = BodyPrediction::Between({.Position = cNow[0].Position, .Rotation = cNow[0].Rotation}, row.Frame);
            if (cApart.Distance < BodyTolerances::kSameDistance && cApart.Angle < BodyTolerances::kStillAngle)
                continue;
            Unclaim(cKey, "the host's game placed it");
            row.Frame = {.Key = cKey, .Position = cNow[0].Position, .Rotation = cNow[0].Rotation};
            row.Placed = true;
            m_authority.Rested(cKey);
            continue;
        }
        if (const auto cClaim = m_authority.Of(cKey); cClaim.Held && IsGuest(cClaim.Player))
            continue; // a held body is its holder's alone
        Unclaim(cKey, "the host's game placed it");
        // Where the game put it (a guest's states may still steer the host's body this frame).
        const auto cPoses = m_game.ReadBodies ? m_game.ReadBodies({cKey}) : std::vector<BodyPose>{};
        row.Frame = {.Key = cKey, .Position = acWhere, .Rotation = cPoses.empty() ? row.Frame.Rotation : cPoses[0].Rotation};
        row.Tick = m_tick;
        row.Placed = true;
        m_authority.Rested(cKey);
    }
}
