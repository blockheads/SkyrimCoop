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
    bool ShowsChoices() const noexcept override;
    std::vector<Option> GetChoices() const noexcept override;
    uint32_t GetListsShown() const noexcept override;
    std::optional<uint32_t> GetLine(uint32_t aTopic) const noexcept override;

    bool Start(uint32_t aActorFormId, bool aShowMenu) noexcept override;
    bool ShowMenu() noexcept override;
    void CloseMenu() noexcept override;
    void SetMenuCloseEnds(bool aEnds) noexcept override;
    void End() noexcept override;
    bool ClearOrphaned() noexcept override;

    bool ShowOptions(const std::vector<Option>& acOptions) noexcept override;
    void Redraw() noexcept override;
    void Pick(uint32_t aIndex) noexcept override;
    bool PickTopic(uint32_t aTopic, uint32_t aInfo) noexcept override;
    void MarkSaid(uint32_t aInfo) noexcept override;

    void OpenTraining(uint32_t aTrainerFormId) noexcept override;
    void GainSpeechExperience(float aExperience) noexcept override;
};
