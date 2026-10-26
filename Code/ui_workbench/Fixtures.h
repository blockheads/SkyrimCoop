#pragma once

#include <filesystem>
#include <ui_models/UiModels.h>

bool LoadFixture(const std::filesystem::path& acPath, Ui::PartyModel& aParty, Ui::ChatModel& aChat,
                 Ui::JoinModel& aJoin, Ui::DeathModel& aDeath, Ui::PlayerColors& aColors);
