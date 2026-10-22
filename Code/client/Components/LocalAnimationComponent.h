#pragma once

#ifndef TP_INTERNAL_COMPONENTS_GUARD
#error Include Components.h instead
#endif

#include <Structs/ActionEvent.h>

struct LocalAnimationComponent
{
    std::vector<ActionEvent> Actions;
    ActionEvent LastProcessedAction;

    [[nodiscard]] std::optional<ActionEvent> GetLatestAction() const noexcept
    {
        if (Actions.empty())
            return std::nullopt;
        return Actions.back();
    }

    void Append(const ActionEvent& acEvent) noexcept { Actions.push_back(acEvent); }
};
