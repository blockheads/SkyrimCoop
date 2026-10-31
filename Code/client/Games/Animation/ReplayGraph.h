#pragma once

#include <string_view>

namespace ReplayGraph
{
template <class Load> bool Recover(bool aTaken, std::string_view acState, Load&& aLoad)
{
    return aTaken || (!acState.empty() && aLoad(acState));
}
} // namespace ReplayGraph
