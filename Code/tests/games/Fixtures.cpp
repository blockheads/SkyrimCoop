#include "Fixtures.h"

namespace GameTest
{
void PlayerCell(FakeEngine& aEngine, uint32_t aCell)
{
    aEngine.Actors.at(FakeEngine::kPlayer).Cell = aCell;
    aEngine.LoadCell(aCell);
}

void SmokeWorld(FakeEngine& aEngine)
{
    PlayerCell(aEngine, 0x00031D2E);
}

std::vector<uint32_t> PlayerCopies(const FakeEngine& acEngine)
{
    std::vector<uint32_t> copies;
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player)
            copies.push_back(cId);
    return copies;
}

uint32_t PlayerCopy(const FakeEngine& acEngine, size_t aSkip)
{
    for (const uint32_t cId : acEngine.CopiesOf())
        if (acEngine.Actors.at(cId).Player && aSkip-- == 0)
            return cId;
    return 0;
}
} // namespace GameTest
