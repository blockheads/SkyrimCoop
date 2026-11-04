#pragma once

#include <Games/Animation/GraphRandom.h>
#include <Structs/ActionEvent.h>

struct LocalAnimationComponent
{
    std::vector<ActionEvent> Actions;
    ActionEvent LastProcessedAction;
    GraphRandom::Owner Random;

    [[nodiscard]] std::optional<ActionEvent> GetLatestAction() const noexcept
    {
        if (Actions.empty())
            return std::nullopt;
        return Actions.back();
    }

    void Append(const ActionEvent& acEvent) noexcept { Actions.push_back(acEvent); }
};
