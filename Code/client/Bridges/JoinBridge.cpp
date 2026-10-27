#include <Events/GameLoadFailedEvent.h>
#include <World.h>
#include <Bridges/JoinBridge.h>

#include <Breadcrumbs.h>
#include <FormIds.h>
#include <Forms/BGSPerk.h>
#include <Forms/SpellItem.h>
#include <Forms/TESNPC.h>
#include <Forms/TESRace.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>
#include <Games/FormState.h>
#include <Structs/GridCellCoords.h>
#include <Interface/UI.h>
#include <Misc/TintMask.h>
#include <PlayerCharacter.h>
#include <RTTI.h>
#include <SaveLoadManager.h>
#include <Utils.h>
#include <MainLoopTasks.h>

#include <fstream>
#include <random>

namespace
{
constexpr uint32_t kFactions = 1u << 6;
constexpr uint32_t kStamina = 26;
constexpr uint32_t kFirstSkill = 6;

std::optional<std::string> ReadFile(const std::filesystem::path& acPath) noexcept
{
    std::ifstream file(acPath, std::ios::binary);
    if (!file)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool WriteFile(const std::filesystem::path& acPath, const std::string& acData) noexcept
{
    std::ofstream file(acPath, std::ios::binary | std::ios::trunc);
    file.write(acData.data(), static_cast<std::streamsize>(acData.size()));
    return static_cast<bool>(file);
}

TESNPC* PlayerBase(PlayerCharacter* apPlayer) noexcept
{
    return apPlayer ? Cast<TESNPC>(apPlayer->baseForm) : nullptr;
}

// The record's ids, for this game.
template <class T> T* FormOf(const GameId& acId) noexcept
{
    return Cast<T>(TESForm::GetById(ToGame(acId)));
}

// The record's place in this game: outdoors its worldspace's cell at its position, indoors its cell.
TESObjectCELL* PlaceCell(const PlayerRecord& acRecord) noexcept
{
    if (acRecord.WorldSpace)
    {
        TESWorldSpace* pWorldSpace = FormOf<TESWorldSpace>(acRecord.WorldSpace);
        const auto cCoordinates = GridCellCoords::CalculateGridCellCoords(acRecord.Position[0], acRecord.Position[1]);
        return pWorldSpace ? pWorldSpace->LoadCell(cCoordinates.X, cCoordinates.Y) : nullptr;
    }
    return acRecord.Cell ? FormOf<TESObjectCELL>(acRecord.Cell) : nullptr;
}
} // namespace

bool JoinBridge::IsAtMainMenu() const noexcept
{
    const UI* pUI = UI::Get();
    return pUI && pUI->GetMenuOpen("Main Menu");
}

// Kept beside the saves: My Games/Skyrim Special Edition/SkyrimCoop/player_id.
std::string JoinBridge::PlayerId() noexcept
{
    if (!m_playerId.empty())
        return m_playerId;
    const auto cSaves = BGSSaveLoadManager::SaveFilePath("player").parent_path();
    const auto cPath = cSaves.parent_path() / "SkyrimCoop" / "player_id";
    if (auto id = ReadFile(cPath); id && id->size() == 19 && id->starts_with("id:"))
        return m_playerId = *id;
    std::random_device random;
    const uint64_t cValue = (static_cast<uint64_t>(random()) << 32) | random();
    std::string id = std::format("id:{:016x}", cValue);
    std::error_code error;
    std::filesystem::create_directories(cPath.parent_path(), error);
    if (!WriteFile(cPath, id))
        spdlog::warn("JoinBridge: could not keep the player id in {}", cPath.string());
    return m_playerId = id;
}

namespace
{
// On the main thread between frames: the save and its two files.
std::optional<IJoinBridge::SaveFiles> MakeSave(const std::string& acName) noexcept
{
    BGSSaveLoadManager* pManager = BGSSaveLoadManager::Get();
    const auto cGame = BGSSaveLoadManager::SaveFilePath(acName.c_str());
    if (!pManager || cGame.empty() || !pManager->Save(acName.c_str()))
        return std::nullopt;
    auto coSave = cGame;
    coSave.replace_extension(".skse");
    auto game = ReadFile(cGame);
    auto files = game ? std::optional<IJoinBridge::SaveFiles>({std::move(*game), ReadFile(coSave).value_or("")}) : std::nullopt;
    // Not one of the host's own saves: gone from its list once read.
    std::error_code error;
    std::filesystem::remove(cGame, error);
    std::filesystem::remove(coSave, error);
    if (files)
        spdlog::info("JoinBridge: saved {} ({} and {} bytes)", acName, files->Game.size(), files->CoSave.size());
    return files;
}
} // namespace

std::optional<IJoinBridge::SaveFiles> JoinBridge::SaveNow(const std::string& acName) noexcept
{
    std::scoped_lock lock(m_pSave->Lock);
    if (m_pSave->Done)
    {
        m_pSave->Done = false;
        return std::exchange(m_pSave->Files, std::nullopt);
    }
    if (!m_pSave->Pending)
    {
        m_pSave->Pending = true;
        MainLoopTasks::Queue([pSave = m_pSave, cName = acName] {
            auto files = MakeSave(cName);
            std::scoped_lock saveLock(pSave->Lock);
            pSave->Files = std::move(files);
            pSave->Pending = false;
            pSave->Done = true;
        });
    }
    return std::nullopt;
}

bool JoinBridge::WriteAndLoad(const std::string& acName, const SaveFiles& acFiles) noexcept
{
    const auto cGame = BGSSaveLoadManager::SaveFilePath(acName.c_str());
    if (cGame.empty())
        return false;
    auto coSave = cGame;
    coSave.replace_extension(".skse");
    std::error_code error;
    std::filesystem::create_directories(cGame.parent_path(), error);
    if (!WriteFile(cGame, acFiles.Game) || !WriteFile(coSave, acFiles.CoSave))
    {
        spdlog::error("JoinBridge: could not write {}", cGame.string());
        return false;
    }
    MainLoopTasks::Queue([cName = acName, cPath = cGame.string()] {
        BGSSaveLoadManager* pManager = BGSSaveLoadManager::Get();
        spdlog::info("JoinBridge: loading {}", cPath);
        if (!pManager || !pManager->Load(cName.c_str()))
        {
            spdlog::error("JoinBridge: the engine did not load {}", cPath);
            World::Get().GetRunner().Trigger(GameLoadFailedEvent{cName});
        }
    });
    return true;
}

PlayerRecord JoinBridge::ReadPlayer() const noexcept
{
    PlayerRecord record;
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESNPC* pBase = PlayerBase(pPlayer);
    if (!pBase)
        return record;

    record.Name = pBase->GetName();
    record.Level = pPlayer->GetLevel();
    record.BaseFlags = kBaseFlags;
    if (!FormState::Save(*pBase, kBaseFlags, record.Base))
        spdlog::warn("JoinBridge: the player's base was not readable (a save or load runs)");
    record.Weight = pBase->weight;

    for (const TintMask* cpTint : pPlayer->baseTints)
    {
        if (!cpTint)
            continue;
        const char* cpTexture = cpTint->texture ? cpTint->texture->name.AsAscii() : nullptr;
        record.FaceTints.Entries.push_back({cpTexture ? cpTexture : "", cpTint->alpha, cpTint->color, cpTint->type});
    }

    if (const Skills* cpSkills = pPlayer->pSkills ? *pPlayer->pSkills : nullptr)
    {
        record.LevelExperience = cpSkills->xp;
        record.LevelThreshold = cpSkills->levelThreshold;
        for (size_t i = 0; i < PlayerRecord::kSkillCount; ++i)
        {
            const auto& cSkill = cpSkills->skills[i];
            record.Skills[i] = {cSkill.level, cSkill.xp, cSkill.levelThreshold, cpSkills->legendaryLevels[i]};
        }
    }
    record.PerkPoints = pPlayer->perkPoints;
    // Once each (the engine's list can hold a perk twice, from earlier records added on top).
    for (const PlayerCharacter::PerkRank* cpPerk : pPlayer->perkRanks)
        if (cpPerk && cpPerk->perk)
            if (const auto cId = ToSession(cpPerk->perk->formID))
            {
                if (auto it = std::ranges::find(record.Perks, *cId, &PlayerRecord::Perk::Id); it != record.Perks.end())
                    it->Rank = std::max(it->Rank, cpPerk->rank);
                else
                    record.Perks.push_back({*cId, cpPerk->rank});
            }
    for (uint32_t i = 0; i < pPlayer->addedSpells.size; ++i)
        if (const SpellItem* cpSpell = pPlayer->addedSpells.Get(i))
            if (const auto cId = ToSession(cpSpell->formID))
                record.Spells.push_back(*cId);

    ActorValueOwner& values = EngineVirtual(pPlayer->actorValueOwner);
    for (uint32_t i = kFirstSkill; i <= kStamina; ++i)
        record.Values.push_back({static_cast<uint8_t>(i), values.GetBaseValue(i)});

    record.Items = pPlayer->GetActorInventory();
    return record;
}

void JoinBridge::ApplyPlayer(const PlayerRecord& acRecord) noexcept
{
    Breadcrumbs::Record("JoinBridge::ApplyPlayer", acRecord.Level);
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESNPC* pBase = PlayerBase(pPlayer);
    if (!pBase)
        return;

    // The base first: name, race, sex, face, level, base spells and shouts; recorded as changed for saves and copies.
    const uint32_t cFlags = acRecord.BaseFlags & ~kFactions;
    if (!acRecord.Base.empty() && FormState::Load(*pBase, cFlags, acRecord.Base))
        pBase->MarkChanged(cFlags);
    else
        spdlog::warn("JoinBridge: the record's base did not load ({} bytes)", acRecord.Base.size());
    pBase->weight = acRecord.Weight; // the body follows on QueueUpdate below
    if (TESRace* pRace = pBase->raceForm.race; pRace && pRace != pPlayer->race)
        pPlayer->SwitchRace(pRace, true);

    // The masks of the record's race and sex (the save's player had its own), then the record's colours on them.
    pPlayer->RebuildTintMasks();
    for (const Tints::Entry& cTint : acRecord.FaceTints.Entries)
        for (TintMask* pTint : pPlayer->baseTints)
        {
            const char* cpTexture = pTint && pTint->texture ? pTint->texture->name.AsAscii() : nullptr;
            if (cpTexture && cTint.Name == cpTexture) // by texture: masks loaded from a save have no type
            {
                pTint->color = cTint.Color;
                pTint->alpha = cTint.Alpha;
            }
        }

    if (Skills* pSkills = pPlayer->pSkills ? *pPlayer->pSkills : nullptr)
    {
        pSkills->xp = acRecord.LevelExperience;
        pSkills->levelThreshold = acRecord.LevelThreshold;
        for (size_t i = 0; i < PlayerRecord::kSkillCount; ++i)
        {
            const auto& cSkill = acRecord.Skills[i];
            pSkills->skills[i] = {cSkill.Level, cSkill.Experience, cSkill.Threshold};
            pSkills->legendaryLevels[i] = cSkill.Legendary;
        }
    }
    ActorValueOwner& values = EngineVirtual(pPlayer->actorValueOwner);
    for (const PlayerRecord::Value& cValue : acRecord.Values)
        if (cValue.Index >= kFirstSkill && cValue.Index <= kStamina)
            values.SetBaseValue(cValue.Index, cValue.Base);
    pPlayer->perkPoints = acRecord.PerkPoints;

    // Perks and added spells: the save's player's go, the record's come (a perk's removal is queued, so shared ones stay).
    std::vector<BGSPerk*> drop;
    for (const PlayerCharacter::PerkRank* cpPerk : pPlayer->perkRanks)
        if (cpPerk && cpPerk->perk)
        {
            const auto cId = ToSession(cpPerk->perk->formID);
            if (std::ranges::none_of(acRecord.Perks, [&](const PlayerRecord::Perk& acPerk) { return cId && acPerk.Id == *cId; }))
                drop.push_back(cpPerk->perk);
        }
    for (BGSPerk* pPerk : drop)
        pPlayer->RemovePerk(pPerk);
    // One the player kept at that rank is not added again (AddPerk appends another entry, as the record reads it).
    const auto cHas = [pPlayer](const BGSPerk* apPerk, uint8_t aRank) {
        for (const PlayerCharacter::PerkRank* cpPerk : pPlayer->perkRanks)
            if (cpPerk && cpPerk->perk == apPerk && cpPerk->rank >= aRank)
                return true;
        return false;
    };
    for (const PlayerRecord::Perk& cPerk : acRecord.Perks)
        if (BGSPerk* pPerk = FormOf<BGSPerk>(cPerk.Id); pPerk && !cHas(pPerk, cPerk.Rank))
            pPlayer->AddPerk(pPerk, cPerk.Rank);

    std::vector<SpellItem*> unlearn;
    for (uint32_t i = 0; i < pPlayer->addedSpells.size; ++i)
        if (SpellItem* pSpell = pPlayer->addedSpells.Get(i))
            if (const auto cId = ToSession(pSpell->formID); !cId || std::ranges::find(acRecord.Spells, *cId) == acRecord.Spells.end())
                unlearn.push_back(pSpell);
    for (SpellItem* pSpell : unlearn)
        pPlayer->RemoveSpell(pSpell);
    for (const GameId& cId : acRecord.Spells)
        pPlayer->AddSpell(FormOf<SpellItem>(cId));

    pPlayer->SetActorInventory(acRecord.Items);
    // The head again from the base's face and the tints (DoReset3D).
    pPlayer->QueueUpdate();
    // Where the host's save left this character; the save's player stands where the host's did.
    if (TESObjectCELL* pCell = PlaceCell(acRecord))
    {
        NiPoint3 position;
        position.x = acRecord.Position[0];
        position.y = acRecord.Position[1];
        position.z = acRecord.Position[2];
        pPlayer->MoveTo(pCell, position);
    }
    spdlog::info("JoinBridge: the player is {} now (level {}, {} perks, {} spells added, {} items)", acRecord.Name, acRecord.Level,
                 acRecord.Perks.size(), acRecord.Spells.size(), acRecord.Items.Entries.size());
}

std::optional<IJoinBridge::SaveFiles> JoinBridge::PreviousSave() noexcept
{
    BGSSaveLoadManager* pManager = BGSSaveLoadManager::Get();
    const auto cName = pManager ? pManager->PreviousGameplaySave() : std::nullopt;
    if (!cName)
        return std::nullopt;
    return ReadSave(*cName);
}

std::optional<IJoinBridge::SaveFiles> JoinBridge::ReadSave(const std::string& acName) noexcept
{
    const auto cGame = BGSSaveLoadManager::SaveFilePath(acName.c_str());
    auto game = ReadFile(cGame);
    if (!game)
        return std::nullopt;
    auto coSave = cGame;
    coSave.replace_extension(".skse");
    return SaveFiles{std::move(*game), ReadFile(coSave).value_or("")};
}
