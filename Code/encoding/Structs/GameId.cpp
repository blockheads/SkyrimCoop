#include <Structs/GameId.h>

GameId::GameId(uint32_t aModId, uint32_t aBaseId) noexcept
    : BaseId(aBaseId)
    , ModId(aModId)
{
}

GameId::operator bool() const noexcept
{
    return *this != GameId{};
}

