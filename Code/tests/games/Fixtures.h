#pragma once
#include "FakeEngine.h"

namespace GameTest
{
void PlayerCell(FakeEngine& aEngine, uint32_t aCell);
void SmokeWorld(FakeEngine& aEngine);
std::vector<uint32_t> PlayerCopies(const FakeEngine& acEngine);
uint32_t PlayerCopy(const FakeEngine& acEngine, size_t aSkip = 0);
} // namespace GameTest
