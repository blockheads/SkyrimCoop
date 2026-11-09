#include "FakeGraph.h"

#include <algorithm>
#include <format>

std::string FirstGraphDifference(const std::vector<FakeGraphMemory::Entry>& acOwner,
                                 const std::vector<FakeGraphMemory::Entry>& acCopy)
{
    size_t sameTimeCopy = 0; // the copy's entries so far at its current time: matched one to one with its owner's
    for (size_t i = 0; i < acCopy.size(); ++i)
    {
        const auto& cCopy = acCopy[i];
        sameTimeCopy = i && acCopy[i - 1].Ms == cCopy.Ms ? sameTimeCopy + 1 : 0;
        // The first owner entry past the copy's time (a copy's time goes back when it loads an older point).
        const size_t owner = size_t(
            std::upper_bound(acOwner.begin(), acOwner.end(), cCopy.Ms,
                             [](double aMs, const FakeGraphMemory::Entry& acEntry) { return aMs < acEntry.Ms; }) -
            acOwner.begin());
        size_t first = owner; // owner entries [first, owner) are at the copy's time: the n-th matches the copy's n-th
        while (first > 0 && acOwner[first - 1].Ms == cCopy.Ms)
            --first;
        if (owner == 0)
            continue; // before its owner's history
        const size_t cAt = sameTimeCopy < owner - first ? first + sameTimeCopy : owner - 1;
        const auto& cOwner = acOwner[cAt];
        if (cOwner.State == cCopy.State)
            continue;
        std::string inputs;
        for (size_t j = i >= 4 ? i - 4 : 0; j <= i; ++j)
            inputs += std::format("{}{} at {:.1f}", inputs.empty() ? "" : ", ", acCopy[j].Input, acCopy[j].Ms);
        std::string ownerInputs;
        for (size_t j = cAt >= 5 ? cAt - 5 : 0; j < std::min(cAt + 1, acOwner.size()); ++j)
            ownerInputs +=
                std::format("{}{} at {:.1f}", ownerInputs.empty() ? "" : ", ", acOwner[j].Input, acOwner[j].Ms);
        return std::format("at owner time {:.1f} ms the copy is in {} after {}, its owner in {} after {}", cCopy.Ms,
                           cCopy.State, inputs, cOwner.State, ownerInputs);
    }
    return {};
}
