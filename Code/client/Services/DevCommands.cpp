#include <Services/DevCommands.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <span>

namespace DevCommands
{
namespace
{
struct Named
{
    std::string_view Name;
    uint32_t FormId;
};

// Skyrim.esm: leveled NPC lists (LCharWolf, LCharBanditMeleeAny, LCharDraugrMeleeAllMale, LCharSabrecat, LCharGiant,
// LCharDragonAny) and EncTroll; the console's placeatme picks from a list at the player's level.
constexpr std::array cSpawns{Named{"wolf", 0xB83C2},  Named{"bandit", 0x3DECD},   Named{"draugr", 0x55955},
                             Named{"troll", 0x23ABA}, Named{"sabrecat", 0xFE2D5}, Named{"giant", 0x30529},
                             Named{"dragon", 0x5EACF}};
// Skyrim.esm weathers: SkyrimClear, SkyrimOvercastRain, SkyrimOvercastSnow, SkyrimStormRain, SkyrimStormSnow,
// SkyrimFog.
constexpr std::array cWeathers{Named{"clear", 0x81A},   Named{"rain", 0xC821F},     Named{"snow", 0x4D7FB},
                               Named{"storm", 0xC8220}, Named{"blizzard", 0xC8221}, Named{"fog", 0xC821E}};
constexpr std::array cGearSets{Named{"daedric", 0}, Named{"glass", 1}};
// Armor and weapons equipped, then ammunition (50).
constexpr std::array<uint32_t, 6> cDaedric{0x1396B, 0x1396A, 0x1396C, 0x1396D, 0x1396E, 0x139B9};
constexpr std::array<uint32_t, 6> cGlass{0x13939, 0x13938, 0x1393A, 0x1393B, 0x139A5, 0x139BE};
// LootBanditChestBossMagicItem100: a boss chest's enchanted item, one per roll (the boss chest's base list rolled
// mostly arrows; Skyrim 2026-11-16).
constexpr uint32_t cLootBox = 0xE88;
// The player's twenty shouts (Skyrim.esm SHOU) and their words (WOOP), from Unrelenting Force to Disarm.
struct Shout
{
    uint32_t Id;
    std::array<uint32_t, 3> Words;
};
constexpr std::array<Shout, 20> cShouts{{
    {0x13E07, {0x13E22, 0x13E23, 0x13E24}}, {0x2F7BA, {0x2F7BB, 0x2F7BC, 0x2F7BD}},
    {0x32920, {0x32917, 0x32918, 0x32919}}, {0x32921, {0x3291D, 0x3291E, 0x3291F}},
    {0x3CD34, {0x3CD31, 0x3CD32, 0x3CD33}}, {0x3F9EA, {0x20E17, 0x20E18, 0x20E19}},
    {0x44250, {0x44251, 0x44252, 0x44253}}, {0x46B8C, {0x46B89, 0x46B8A, 0x46B8B}},
    {0x48AC9, {0x48ACA, 0x48ACB, 0x48ACC}}, {0x5196A, {0x51960, 0x51961, 0x51962}},
    {0x5D16B, {0x5D16C, 0x5D16D, 0x5D16E}}, {0x5FC77, {0x60291, 0x60292, 0x60293}},
    {0x2395A, {0x3291A, 0x3291B, 0x3291C}}, {0x7097B, {0x60294, 0x60295, 0x60296}},
    {0x7097C, {0x60297, 0x60298, 0x60299}}, {0x7097D, {0x6029A, 0x6029B, 0x6029C}},
    {0x7097E, {0x6029D, 0x6029E, 0x6029F}}, {0x7097F, {0x602A0, 0x602A1, 0x602A2}},
    {0x70980, {0x602A3, 0x602A4, 0x602A5}}, {0x70981, {0x5FB95, 0x5FB96, 0x5FB97}},
}};
// The vanilla values the tweaks scale: fInAirFallingCharGravityMult:Havok (1.35, the executable) and fJumpHeightMin
// (76, Skyrim.esm). Both are read live (80098, Actor::Jump): docs/re/systems/dev-commands.md.
constexpr float cFallGravity = 1.35f;
constexpr float cJumpHeight = 76.f;

struct Spec
{
    std::string_view Name;
    Kind What;
    std::string_view Usage;
};
constexpr std::array cSpecs{
    Spec{"heal", Kind::Heal, "/heal"},
    Spec{"god", Kind::God, "/god [on|off]"},
    Spec{"speed", Kind::Speed, "/speed <0.1-10, 1 is normal>"},
    Spec{"gravity", Kind::Gravity, "/gravity <0.1-3, 1 is normal>"},
    Spec{"shouts", Kind::Shouts, "/shouts"},
    Spec{"gear", Kind::Gear, "/gear [daedric|glass]"},
    Spec{"lootbox", Kind::LootBox, "/lootbox [1-10]"},
    Spec{"killmove", Kind::KillMove, "/killmove <0-100|default>"},
    Spec{"reset", Kind::Reset, "/reset"},
    Spec{"spawn", Kind::Spawn, "/spawn <wolf|bandit|draugr|troll|sabrecat|giant|dragon> [1-20]"},
    Spec{"killall", Kind::KillAll, "/killall"},
    Spec{"weather", Kind::Weather, "/weather <clear|rain|snow|storm|blizzard|fog>"},
    Spec{"leash", Kind::Leash, "/leash [on|off]"},
};

std::vector<std::string_view> Words(std::string_view aLine) noexcept
{
    std::vector<std::string_view> words;
    size_t i = 0;
    while (i < aLine.size())
    {
        while (i < aLine.size() && aLine[i] == ' ')
            ++i;
        const size_t cStart = i;
        while (i < aLine.size() && aLine[i] != ' ')
            ++i;
        if (i > cStart)
            words.push_back(aLine.substr(cStart, i - cStart));
    }
    return words;
}

const Spec* Find(std::string_view aLine) noexcept
{
    const auto cWords = Words(aLine);
    if (cWords.empty() || !cWords[0].starts_with('/'))
        return nullptr;
    const auto cName = cWords[0].substr(1);
    const auto it = std::ranges::find(cSpecs, cName, &Spec::Name);
    return it == cSpecs.end() ? nullptr : &*it;
}

template <class T> std::optional<T> Number(std::string_view aWord, T aMin, T aMax) noexcept
{
    T value{};
    const auto [cEnd, cError] = std::from_chars(aWord.data(), aWord.data() + aWord.size(), value);
    if (cError != std::errc{} || cEnd != aWord.data() + aWord.size() || !(value >= aMin && value <= aMax))
        return std::nullopt;
    return value;
}

template <size_t N> const Named* Lookup(const std::array<Named, N>& acTable, std::string_view aName) noexcept
{
    const auto it = std::ranges::find(acTable, aName, &Named::Name);
    return it == acTable.end() ? nullptr : &*it;
}

std::string Number(float aValue)
{
    return std::format("{:.3f}", aValue);
}
} // namespace

bool IsDevCommand(std::string_view aLine) noexcept
{
    return aLine == "/dev" || Find(aLine);
}

Parsed Parse(std::string_view aLine) noexcept
{
    const Spec* pSpec = Find(aLine);
    if (!pSpec)
        return {std::nullopt, Help()};
    const auto cWords = Words(aLine);
    const auto cArg = [&](size_t aIndex) { return aIndex < cWords.size() ? cWords[aIndex] : std::string_view{}; };
    const Parsed cUsage{std::nullopt, std::format("Usage: {}", pSpec->Usage)};
    Command command;
    command.What = pSpec->What;
    command.Text = std::string(aLine);
    const size_t cMaxArgs = pSpec->What == Kind::Spawn ? 2 : 1;
    if (cWords.size() > cMaxArgs + 1)
        return cUsage;
    switch (pSpec->What)
    {
    case Kind::Heal:
    case Kind::Shouts:
    case Kind::Reset:
    case Kind::KillAll:
        if (cWords.size() > 1)
            return cUsage;
        break;
    case Kind::God:
    case Kind::Leash:
        if (cArg(1) == "on")
            command.On = true;
        else if (cArg(1) == "off")
            command.On = false;
        else if (!cArg(1).empty())
            return cUsage;
        break;
    case Kind::Speed:
    case Kind::Gravity: {
        const auto cValue = Number<float>(cArg(1), 0.1f, pSpec->What == Kind::Speed ? 10.f : 3.f);
        if (!cValue)
            return cUsage;
        command.Value = *cValue;
        break;
    }
    case Kind::Gear: {
        const Named* pSet = Lookup(cGearSets, cArg(1).empty() ? "daedric" : cArg(1));
        if (!pSet)
            return cUsage;
        command.FormId = pSet->FormId;
        break;
    }
    case Kind::LootBox: {
        const auto cCount = cArg(1).empty() ? std::optional<uint32_t>(1) : Number<uint32_t>(cArg(1), 1, 10);
        if (!cCount)
            return cUsage;
        command.Count = *cCount;
        break;
    }
    case Kind::KillMove:
        if (cArg(1) != "default")
        {
            const auto cOdds = Number<uint32_t>(cArg(1), 0, 100);
            if (!cOdds)
                return cUsage;
            command.Odds = static_cast<uint8_t>(*cOdds);
        }
        break;
    case Kind::Spawn: {
        const Named* pNpc = Lookup(cSpawns, cArg(1));
        const auto cCount = cArg(2).empty() ? std::optional<uint32_t>(1) : Number<uint32_t>(cArg(2), 1, 20);
        if (!pNpc || !cCount)
            return cUsage;
        command.FormId = pNpc->FormId;
        command.Count = *cCount;
        break;
    }
    case Kind::Weather: {
        const Named* pWeather = Lookup(cWeathers, cArg(1));
        if (!pWeather)
            return cUsage;
        command.FormId = pWeather->FormId;
        break;
    }
    }
    return {std::move(command), {}};
}

std::string Help() noexcept
{
    std::string help = "Dev commands (everyone):";
    for (const auto& cSpec : cSpecs)
        help += std::format(" {}", cSpec.Usage);
    return help;
}

bool IsWorld(Kind aKind) noexcept
{
    return aKind == Kind::Spawn || aKind == Kind::KillAll || aKind == Kind::Weather || aKind == Kind::Leash;
}

std::optional<std::string> ConsoleName(uint32_t aFormId, uint32_t aPlayerFormId) noexcept
{
    if (!aFormId)
        return std::nullopt;
    if (aFormId == aPlayerFormId)
        return "player";
    std::string name = std::format("{:08X}", aFormId);
    return name[0] >= 'A' && name[0] <= 'F' ? std::optional(std::move(name)) : std::nullopt;
}

std::vector<std::string> PlayerLines(const Command& acCommand) noexcept
{
    std::vector<std::string> lines;
    const auto cSpeed = [&](float aMult) {
        lines.push_back(std::format("player.setav speedmult {}", Number(aMult * 100.f)));
        // The engine applies a new speed with the next carry weight change.
        lines.push_back("player.modav carryweight 1");
        lines.push_back("player.modav carryweight -1");
    };
    const auto cGravity = [&](float aMult) {
        // Falling takes the INI's gravity (80098), rising the world's: the jump reaches the height that gravity gives.
        lines.push_back(std::format("setini \"fInAirFallingCharGravityMult:Havok\" {}", Number(cFallGravity * aMult)));
        lines.push_back(std::format("setgs fJumpHeightMin {}", Number(cJumpHeight / aMult)));
    };
    switch (acCommand.What)
    {
    case Kind::Heal:
        for (const char* cpValue : {"health", "magicka", "stamina"})
            lines.push_back(std::format("player.restoreav {} 100000", cpValue));
        break;
    case Kind::Speed:
        cSpeed(acCommand.Value);
        break;
    case Kind::Gravity:
        cGravity(acCommand.Value);
        break;
    case Kind::Shouts:
        for (const auto& cShout : cShouts)
        {
            lines.push_back(std::format("player.addshout {:X}", cShout.Id));
            for (const uint32_t cWord : cShout.Words)
            {
                lines.push_back(std::format("player.teachword {:X}", cWord));
                lines.push_back(std::format("player.unlockword {:X}", cWord));
            }
        }
        lines.push_back("player.setav shoutrecoverymult 0");
        break;
    case Kind::Gear: {
        const std::span<const uint32_t> cSet =
            acCommand.FormId == 1 ? std::span<const uint32_t>(cGlass) : std::span<const uint32_t>(cDaedric);
        for (const uint32_t cItem : cSet)
        {
            const bool cAmmo = cItem == 0x139BE;
            lines.push_back(std::format("player.additem {:X} {}", cItem, cAmmo ? 50 : 1));
            lines.push_back(std::format("player.equipitem {:X}", cItem));
        }
        break;
    }
    case Kind::LootBox:
        lines.push_back(std::format("player.additem {:X} {}", cLootBox, acCommand.Count));
        break;
    case Kind::Reset:
        // Whatever set them: a command, the console, or a game since reloaded.
        cSpeed(1.f);
        cGravity(1.f);
        break;
    case Kind::God:
    case Kind::KillMove:
    case Kind::Spawn:
    case Kind::KillAll:
    case Kind::Weather:
    case Kind::Leash:
        break;
    }
    return lines;
}

std::vector<std::string> WorldLines(const Command& acCommand, std::string_view aAt) noexcept
{
    switch (acCommand.What)
    {
    case Kind::Spawn:
        return {std::format("{}.placeatme {:X} {}", aAt, acCommand.FormId, acCommand.Count)};
    case Kind::Weather:
        return {std::format("fw {:X}", acCommand.FormId)};
    default:
        return {};
    }
}

std::optional<bool> GodAfter(const Command& acCommand, bool aNow) noexcept
{
    if (acCommand.What == Kind::Reset)
        return false;
    if (acCommand.What == Kind::God)
        return acCommand.On.value_or(!aNow);
    return std::nullopt;
}

bool ShouldAttackKill(bool aFatal, bool aKillable, std::optional<uint8_t> aOdds, uint32_t aRoll) noexcept
{
    return aOdds ? aKillable && aRoll < *aOdds : aFatal;
}

bool IsLastHostileActor(bool aLast, std::optional<uint8_t> aOdds) noexcept
{
    // With odds set a kill move may finish any enemy, not only the last (vanilla's rule).
    return aOdds ? true : aLast;
}
} // namespace DevCommands

namespace DevCommands
{
uint32_t KillMoveRolls::Roll(uint64_t aAttacker, uint64_t aTarget, uint64_t aNowMs,
                             const std::function<uint32_t()>& acDraw) noexcept
{
    constexpr uint64_t cHoldMs = 1000;
    constexpr size_t cMaxEntries = 64;
    std::erase_if(m_entries, [&](const Entry& acEntry) { return aNowMs - acEntry.AtMs >= cHoldMs; });
    for (const auto& cEntry : m_entries)
        if (cEntry.Attacker == aAttacker && cEntry.Target == aTarget)
            return cEntry.Roll;
    if (m_entries.size() >= cMaxEntries)
        m_entries.erase(m_entries.begin());
    m_entries.push_back({aAttacker, aTarget, aNowMs, acDraw()});
    return m_entries.back().Roll;
}
} // namespace DevCommands
