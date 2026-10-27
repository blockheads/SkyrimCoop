#pragma once

#include <Bridges/IDialogueBridge.h>

/// IDialogueBridge on the engine (docs/re/systems/dialogue.md).
class DialogueBridge final : public IDialogueBridge
{
public:
    DialogueBridge() noexcept;
    ~DialogueBridge() override;

    void SetFilters(Filters aFilters) noexcept override;

    uint32_t GetSpeaker() const noexcept override;
    bool IsMenuOpen() const noexcept override;
    std::vector<Option> GetOptions() const noexcept override;
    bool IsChoices() const noexcept override;
    uint32_t GetGreeting() const noexcept override;

    bool Start(uint32_t aActorFormId, bool aShowMenu, uint32_t aGreeting = 0) noexcept override;
    bool ShowMenu() noexcept override;
    void CloseMenu() noexcept override;
    void SetMenuCloseEnds(bool aEnds) noexcept override;
    void End() noexcept override;
    bool ClearOrphaned() noexcept override;

    bool ShowList(const std::vector<Option>& acOptions, bool aChoices) noexcept override;
    bool PickOption(uint32_t aTopic, uint32_t aInfo) noexcept override;
};
