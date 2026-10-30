#include "Party.h"

namespace GameTest
{
// Pure actor comparison; FollowOwner leaves a copy within 64 units of its owner.
static void Compare(const std::string& acWhat, const FakeEngine& acOwner, uint32_t aOwnerId,
                    const FakeEngine& acCopies, uint32_t aCopyId, std::vector<std::string>& aDifferences)
{
    const auto cOwnerIt = acOwner.Actors.find(aOwnerId);
    const auto cCopyIt = acCopies.Actors.find(aCopyId);
    if (cOwnerIt == acOwner.Actors.end() || cCopyIt == acCopies.Actors.end())
    {
        aDifferences.push_back(std::format("{}: {} has no actor {:X}", acWhat,
                                           cOwnerIt == acOwner.Actors.end() ? acOwner.Name : acCopies.Name,
                                           cOwnerIt == acOwner.Actors.end() ? aOwnerId : aCopyId));
        return;
    }
    const auto& cOwner = cOwnerIt->second;
    const auto& cCopy = cCopyIt->second;
    const auto cAt = [](const FakeEngine::Actor& acActor) {
        return glm::vec3(acActor.Position[0], acActor.Position[1], acActor.Position[2]);
    };
    if (!cCopy.Copy)
        aDifferences.push_back(std::format("{} ({:X}) is not flagged a copy", acWhat, aCopyId));
    // Outdoors the worldspace: by a cell's border the two can stand in neighbouring cells.
    if (cOwner.WorldSpace ? cCopy.WorldSpace != cOwner.WorldSpace : cCopy.Cell != cOwner.Cell)
        aDifferences.push_back(std::format("{} ({:X}) is in cell {:X} (worldspace {:X}), the owner's in {:X} ({:X})",
                                           acWhat, aCopyId, cCopy.Cell, cCopy.WorldSpace, cOwner.Cell,
                                           cOwner.WorldSpace));
    else if (const float cApart = glm::distance(cAt(cCopy), cAt(cOwner)); cApart > 64.f)
        aDifferences.push_back(std::format("{} ({:X}) stands {:.0f} from the owner's", acWhat, aCopyId, cApart));
    if (cCopy.Dead != cOwner.Dead)
        aDifferences.push_back(std::format("{} ({:X}) is {}, the owner's {}", acWhat, aCopyId, cCopy.Dead ? "dead" : "alive",
                                           cOwner.Dead ? "dead" : "alive"));
    if (cCopy.Disabled != cOwner.Disabled)
        aDifferences.push_back(std::format("{} ({:X}) is {}, the owner's {}", acWhat, aCopyId,
                                           cCopy.Disabled ? "disabled" : "enabled", cOwner.Disabled ? "disabled" : "enabled"));
    const auto cWorn = [](const FakeEngine& acEngine, uint32_t aId) {
        std::string worn;
        for (const auto& [cMod, cBase, cLeft] : acEngine.Worn(aId))
            worn += std::format("{}{:X}:{:X}{}", worn.empty() ? "" : " ", cMod, cBase, cLeft ? " (left)" : "");
        return worn.empty() ? std::string("nothing") : worn;
    };
    if (acCopies.Worn(aCopyId) != acOwner.Worn(aOwnerId))
        aDifferences.push_back(std::format("{} ({:X}) wears {}, the owner's {}", acWhat, aCopyId,
                                           cWorn(acCopies, aCopyId), cWorn(acOwner, aOwnerId)));
    if (ShowsHandsUp(cCopy.Weapon) != HandsUp(cOwner.Weapon))
        aDifferences.push_back(std::format("{} ({:X}) shows its hands {}, the owner's are {}", acWhat, aCopyId,
                                           ShowsHandsUp(cCopy.Weapon) ? "up" : "down",
                                           HandsUp(cOwner.Weapon) ? "up" : "down"));
    const auto cCarried = [](const FakeEngine& acEngine, uint32_t aId) {
        std::string carried;
        for (const auto& [cItem, cCount] : acEngine.Carried(aId))
            carried += std::format("{}{} {:X}:{:X}", carried.empty() ? "" : ", ", cCount, cItem.first, cItem.second);
        return carried.empty() ? std::string("nothing") : carried;
    };
    if (acCopies.Carried(aCopyId) != acOwner.Carried(aOwnerId))
        aDifferences.push_back(std::format("{} ({:X}) carries {}, the owner's {}", acWhat, aCopyId,
                                           cCarried(acCopies, aCopyId), cCarried(acOwner, aOwnerId)));
}

std::vector<std::string> Party::Differences()
{
    std::vector<std::string> differences;
    const FakeEngine& cHost = Engine(Host());
    for (size_t i = 1; i < m_games.size(); ++i)
    {
        const FakeEngine& cGuest = Engine(*m_games[i]);
        for (const auto& [cId, cReference] : cHost.References)
            if (cHost.Attached.contains(cReference.Cell) && cGuest.Attached.contains(cReference.Cell) &&
                cGuest.References.at(cId) != cReference)
                differences.push_back(std::format("{}: reference {:X} differs from the host's", cGuest.Name, cId));
    }
    return differences;
}

std::vector<std::string> Party::ActorDifferences()
{
    std::vector<std::string> differences;
    std::map<uint32_t, std::pair<World*, uint32_t>> owned; // session id: its owner's game and form id
    for (auto& pGame : m_games)
        for (const auto cEntity : pGame->view<LocalComponent, FormIdComponent>())
            owned[pGame->get<LocalComponent>(cEntity).Id] = {pGame.get(), pGame->get<FormIdComponent>(cEntity).Id};

    for (auto& pGame : m_games)
    {
        World& game = *pGame;
        const FakeEngine& cEngine = Engine(game);
        std::set<uint32_t> accounted; // the copies here that stand for an owner's actor
        for (const auto& [cSessionId, cOwner] : owned)
        {
            if (cOwner.first == &game)
                continue;
            const FakeEngine& cOwnerEngine = Engine(*cOwner.first);
            const auto cWhat = std::format("{}'s copy of {}'s {:X} (session {:X})", cEngine.Name,
                                           cOwnerEngine.Name, cOwner.second, cSessionId);
            std::vector<uint32_t> copies;
            for (const auto cEntity : game.view<RemoteComponent>())
                if (game.get<RemoteComponent>(cEntity).Id == cSessionId)
                    if (const auto* cpFormId = game.try_get<FormIdComponent>(cEntity))
                        copies.push_back(cpFormId->Id);
            if (copies.size() != 1)
            {
                differences.push_back(std::format("{}: {} of them", cWhat, copies.size()));
                continue;
            }
            accounted.insert(copies[0]);
            Compare(cWhat, cOwnerEngine, cOwner.second, cEngine, copies[0], differences);
        }
        for (const auto cEntity : game.view<HeldCopyComponent, FormIdComponent>())
        {
            const uint32_t cFormId = game.get<FormIdComponent>(cEntity).Id;
            accounted.insert(cFormId);
            if (&game != &Host())
                Compare(std::format("{}'s held copy {:X}", cEngine.Name, cFormId), Engine(Host()), cFormId, cEngine,
                        cFormId, differences);
        }
        for (const uint32_t cCopy : cEngine.CopiesOf())
            if (!accounted.contains(cCopy))
                differences.push_back(std::format("{}: {:X} is a copy of nothing anyone owns", cEngine.Name, cCopy));
    }
    return differences;
}
} // namespace GameTest
