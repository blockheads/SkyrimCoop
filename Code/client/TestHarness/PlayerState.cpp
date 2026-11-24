// The player's own state its save keeps beside the character (docs/re/systems/player-save-state.md): read for
// scenarios, and favorites set as the item and magic menus set them.
#include "Commands.h"
#include "EngineCalls.h"
#include "Harness.h"

#include <ExtraData/ExtraContainerChanges.h>
#include <ExtraData/ExtraHotkey.h>
#include <FormIds.h>
#include <Magic/MagicFavorites.h>
#include <PlayerCharacter.h>
#include <PlayerSaveState.h>
#include <RTTI.h>

namespace TestHarness
{
namespace
{
std::string Id(const GameId& acId)
{
    return std::format("{:X}:{:X}", acId.ModId, acId.BaseId);
}

std::string FormId(const TESForm* acpForm)
{
    const auto cId = acpForm ? ToSession(acpForm->formID) : std::nullopt;
    return cId ? Id(*cId) : std::string{};
}

// {"cmd": "player_state"}: favorites (items and magic), damage, timed effects, power cooldowns, trainings, camera.
nlohmann::json ReadPlayerState(Harness&, const nlohmann::json&)
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return Error("no player");
    const PlayerOwnState cState = PlayerSaveState::Read(*pPlayer);
    nlohmann::json items = nlohmann::json::array();
    for (const auto& cItem : cState.FavoriteItems)
        items.push_back({{"item", Id(cItem.Stack.BaseId)}, {"hotkey", cItem.Hotkey}});
    nlohmann::json spells = nlohmann::json::array(), hotkeys = nlohmann::json::array();
    if (MagicFavorites* pFavorites = MagicFavorites::Get())
    {
        std::vector<std::string> sorted;
        for (uint32_t i = 0; i < pFavorites->spells.length; ++i)
            sorted.push_back(FormId(pFavorites->spells[i]));
        std::ranges::sort(sorted);
        spells = sorted;
        // Up to the last bound key (the engine grows the list with empty ones, 52011).
        uint32_t bound = 0;
        for (uint32_t i = 0; i < pFavorites->hotkeys.length; ++i)
            if (pFavorites->hotkeys[i])
                bound = i + 1;
        for (uint32_t i = 0; i < bound; ++i)
            hotkeys.push_back(FormId(pFavorites->hotkeys[i]));
    }
    nlohmann::json effects = nlohmann::json::array();
    std::vector<std::string> effectSpells;
    for (const auto& cEffect : cState.Effects)
    {
        effects.push_back({{"spell", Id(cEffect.Spell)}, {"index", cEffect.Index}, {"elapsed", cEffect.Elapsed}});
        effectSpells.push_back(Id(cEffect.Spell));
    }
    std::ranges::sort(effectSpells);
    effectSpells.erase(std::unique(effectSpells.begin(), effectSpells.end()), effectSpells.end());
    nlohmann::json cooldowns = nlohmann::json::array();
    std::vector<std::string> cooldownSpells;
    for (const auto& cCooldown : cState.Cooldowns)
    {
        cooldowns.push_back({{"spell", Id(cCooldown.Spell)}, {"seconds", cCooldown.Seconds}});
        cooldownSpells.push_back(Id(cCooldown.Spell));
    }
    std::ranges::sort(cooldownSpells);
    const auto cRound = [](float aValue) { return std::round(aValue * 10.f) / 10.f; };
    return {{"ok", true},
            {"favorite_items", items},
            {"favorite_spells", spells},
            {"spell_hotkeys", hotkeys},
            {"damage", {cRound(cState.Damage[0]), cRound(cState.Damage[1]), cRound(cState.Damage[2])}},
            {"effects", effects},
            {"effect_spells", effectSpells},
            {"cooldowns", cooldowns},
            {"cooldown_spells", cooldownSpells},
            {"trainings", cState.Trainings},
            {"first_person", cState.FirstPerson},
            {"always_run", EngineCalls::GetAlwaysRun()}};
}

// {"cmd": "favorite", "form": n, "hotkey": -1..7}: the player favorites a spell, shout or carried item and binds the
// number key, as the magic menu (MagicFavorites::SetFavorite 52004) and item menus (16098, 11664) do.
nlohmann::json Favorite(Harness&, const nlohmann::json& acRequest)
{
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    TESForm* pForm = TESForm::GetById(acRequest.value("form", 0u));
    const int8_t cHotkey = static_cast<int8_t>(acRequest.value("hotkey", -1));
    if (!pPlayer || !pForm)
        return Error("no player or form");
    if (pForm->formType == FormType::Spell || pForm->formType == FormType::Shout)
    {
        MagicFavorites* pFavorites = MagicFavorites::Get();
        if (!pFavorites)
            return Error("no MagicFavorites");
        POINTER_SKYRIMSE(void(MagicFavorites*, TESForm*), s_setFavorite, 52004);
        POINTER_SKYRIMSE(void(MagicFavorites*), s_prune, 52011);
        s_setFavorite(pFavorites, pForm);
        if (cHotkey >= 0)
        {
            s_prune(pFavorites); // grows the hotkeys to the slot count (52011)
            if (static_cast<uint32_t>(cHotkey) < pFavorites->hotkeys.length)
                pFavorites->hotkeys[cHotkey] = pForm;
        }
        return Ok();
    }
    auto* pChanges = pPlayer->GetContainerChanges();
    ExtraContainerChanges::Entry* pEntry = nullptr;
    if (pChanges && pChanges->entries)
        for (auto* pCandidate : *pChanges->entries)
            if (pCandidate && pCandidate->form == pForm)
                pEntry = pCandidate;
    if (!pEntry)
        return Error("the player carries no such item");
    ExtraContainerChanges::Entry stack{pForm, nullptr, pEntry->count};
    pChanges->SetFavorite(&stack, nullptr);
    if (cHotkey >= 0)
        if (ExtraDataList* pFavorite = pEntry->GetFavoriteList())
            pFavorite->SetHotkey(cHotkey);
    return Ok();
}

constexpr Command cCommands[] = {{"player_state", true, &ReadPlayerState}, {"favorite", true, &Favorite}};
} // namespace

std::span<const Command> PlayerStateCommands()
{
    return cCommands;
}
} // namespace TestHarness
