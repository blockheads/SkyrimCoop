// HookAddTarget's rule (magic/EffectRules.h): the caster's game decides a spell hit, the target's owner applies it.
#include <catch2/catch.hpp>

#include <magic/EffectRules.h>

using namespace EffectRules;
using enum HitRules::Party;

namespace
{
// A shared hostile spell, as most are.
constexpr Landing Hostile(Party aTarget, Party aCaster) noexcept
{
    return {.Target = aTarget, .Caster = aCaster, .Shared = true};
}
} // namespace

TEST_CASE("Our actor takes our own spell, and one no actor cast", "[EffectRules]")
{
    CHECK(Of(Hostile(Local, LocalPlayer)) == Verdict{.Apply = true});
    CHECK(Of(Hostile(LocalPlayer, Local)) == Verdict{.Apply = true});
    CHECK(Of(Hostile(Local, None)) == Verdict{.Apply = true});
}

TEST_CASE("Our spell on another game's actor asks its owner, and is a hit when hostile", "[EffectRules]")
{
    CHECK(Of(Hostile(Copy, LocalPlayer)) == Verdict{.AskOwner = true, .Hit = true});
    CHECK(Of(Hostile(OtherPlayer, Local)) == Verdict{.AskOwner = true, .Hit = true});

    Landing heal = Hostile(Copy, LocalPlayer);
    heal.Healing = true;
    CHECK(Of(heal) == Verdict{.AskOwner = true});
    Landing buff = Hostile(Copy, LocalPlayer);
    buff.Buff = true;
    CHECK(Of(buff) == Verdict{.AskOwner = true});
}

TEST_CASE("A heal on another player carries the caster's healing perks", "[EffectRules]")
{
    Landing heal = Hostile(OtherPlayer, LocalPlayer);
    heal.Healing = true;
    CHECK(Of(heal) == Verdict{.AskOwner = true, .HealPerks = true});
    heal.Target = Copy; // an NPC's copy: no perks
    CHECK(Of(heal) == Verdict{.AskOwner = true});
}

TEST_CASE("An effect that is never shared still counts as our hit", "[EffectRules]")
{
    Landing ward = Hostile(Copy, LocalPlayer);
    ward.Shared = false;
    CHECK(Of(ward) == Verdict{.Hit = true});
}

TEST_CASE("Another game's actor hit by another game's spell is theirs to handle", "[EffectRules]")
{
    CHECK(Of(Hostile(Copy, Copy)) == Verdict{});
    CHECK(Of(Hostile(OtherPlayer, OtherPlayer)) == Verdict{});
    CHECK(Of(Hostile(Copy, None)) == Verdict{});
    // A guest never judges the world for another player.
    Landing guest = Hostile(OtherPlayer, None);
    guest.Guest = true;
    CHECK(Of(guest) == Verdict{});
}

TEST_CASE("The host's world on a guest's player asks that guest's game", "[EffectRules]")
{
    Landing trap = Hostile(OtherPlayer, None);
    trap.Hosting = true;
    CHECK(Of(trap) == Verdict{.AskOwner = true});

    // An object's projectile's enchantment: the hit actor casts it on itself, the world's all the same, and no hit.
    Landing impact = Hostile(OtherPlayer, OtherPlayer);
    impact.Hosting = true;
    impact.WorldImpact = true;
    CHECK(Of(impact) == Verdict{.AskOwner = true});

    // A script run for a guest making a copy cast: the world's, and that copy's hit. The host's own player's script: not.
    Landing script = Hostile(OtherPlayer, Copy);
    script.Hosting = true;
    script.ScriptFor = 0x1234;
    CHECK(Of(script) == Verdict{.AskOwner = true, .Hit = true});
    script.ScriptFor = 0x14;
    CHECK(Of(script) == Verdict{});
    script.ScriptFor = 0;
    CHECK(Of(script) == Verdict{});

    // Only a player's copy: an NPC's copy is never the host's to judge.
    Landing npc = Hostile(Copy, None);
    npc.Hosting = true;
    CHECK(Of(npc) == Verdict{});
}

TEST_CASE("A guest's own copy of a world cast on its player applies nothing", "[EffectRules]")
{
    Landing trap = Hostile(LocalPlayer, None);
    trap.Guest = true;
    CHECK(Of(trap) == Verdict{});
    // Its own script's does.
    trap.ScriptFor = 0x14;
    CHECK(Of(trap) == Verdict{.Apply = true});
    // The host's game applies its own.
    trap.ScriptFor = 0;
    trap.Guest = false;
    CHECK(Of(trap) == Verdict{.Apply = true});
    // A guest's NPC is not its player.
    Landing npc = Hostile(Local, None);
    npc.Guest = true;
    CHECK(Of(npc) == Verdict{.Apply = true});
}

TEST_CASE("A copy's replayed spell on our actor is only a hit: its game already asked us", "[EffectRules]")
{
    CHECK(Of(Hostile(Local, Copy)) == Verdict{.Hit = true});
    CHECK(Of(Hostile(LocalPlayer, OtherPlayer)) == Verdict{.Hit = true});
}

TEST_CASE("A guest's player's own summon runs here and tells the host", "[EffectRules]")
{
    Landing summon{.Target = LocalPlayer, .Caster = LocalPlayer, .TargetIsThisPlayer = true, .CasterIsTarget = true, .Guest = true, .Summon = true};
    CHECK(Of(summon) == Verdict{.Apply = true, .Summon = true});
    summon.Guest = false; // the host's player's: its engine makes the creature
    CHECK(Of(summon) == Verdict{.Apply = true});
}

TEST_CASE("A player's own effect and another game's request pass straight to the engine", "[EffectRules]")
{
    Landing nightVision = Hostile(Copy, Copy);
    nightVision.Forbidden = true;
    CHECK(Of(nightVision) == Verdict{.Apply = true});
    Landing request = Hostile(Copy, Copy);
    request.Overridden = true;
    CHECK(Of(request) == Verdict{.Apply = true});
}
