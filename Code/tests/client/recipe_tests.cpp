// A player-made potion's id in the session (Recipe) and a game's book of them (RecipeBook,
// docs/design/crafted-potions.md).
#include <catch2/catch.hpp>

#include <RecipeBook.h>

#include <map>

namespace
{
const Inventory::EffectItem cFortify{25.f, 0, 60, 30.f, GameId(0, 0x3EAF3)};
const Inventory::EffectItem cRestore{50.f, 0, 0, 20.f, GameId(0, 0x3EB15)};
} // namespace

TEST_CASE("A recipe's id is the same in every game for the same potion", "[recipes]")
{
    const Recipe cBrewed{false, {cFortify, cRestore}};
    const GameId cId = cBrewed.Id();
    CHECK(Recipe::IsId(cId));
    CHECK(cId.ModId == Recipe::kModId);

    SECTION("its effects in another order (another game's form)")
    {
        const Recipe cOther{false, {cRestore, cFortify}};
        CHECK(cOther == cBrewed);
        CHECK(cOther.Id() == cId);
    }
    SECTION("another cost (a game's own, from its perks)")
    {
        Recipe other = cBrewed;
        other.Effects[0].RawCost = 99.f;
        CHECK(other == cBrewed);
        CHECK(other.Id() == cId);
    }
    SECTION("a poison of the same effects is another item")
    {
        Recipe poison = cBrewed;
        poison.IsPoison = true;
        CHECK_FALSE(poison == cBrewed);
        CHECK(poison.Id() != cId);
    }
    SECTION("a stronger potion is another item")
    {
        Recipe stronger = cBrewed;
        stronger.Effects[0].Magnitude = 26.f;
        CHECK(stronger.Id() != cId);
    }
    SECTION("a plugin's form id is never one")
    {
        CHECK_FALSE(Recipe::IsId(GameId(0, 0x3EAF3)));
    }
}

TEST_CASE("A game's book keeps each recipe once, in the order learned", "[recipes]")
{
    RecipeBook book;
    const Recipe cPotion{false, {cFortify}};
    const Recipe cPoison{true, {cRestore}};
    CHECK(book.Learn(cPotion) == cPotion.Id());
    CHECK(book.Learn(Recipe{false, {cFortify}}) == cPotion.Id());
    book.Learn(cPoison);
    CHECK(book.Find(cPoison.Id()) == cPoison);
    CHECK_FALSE(book.Find(GameId(0, 0x3EAF3)));

    SECTION("a game told some of them gets the rest, once")
    {
        size_t sent = 1;
        CHECK(book.Unsent(sent) == std::vector{cPoison});
        CHECK(sent == 2);
        CHECK(book.Unsent(sent).empty());
        size_t joiner = 0;
        CHECK(book.Unsent(joiner) == std::vector{cPotion, cPoison});
    }
    SECTION("an inventory's recipes: its items' and its weapons' coatings")
    {
        Inventory inventory;
        Inventory::Entry potions;
        potions.BaseId = cPotion.Id();
        potions.Count = 3;
        Inventory::Entry sword;
        sword.BaseId = GameId(0, 0x12EB7);
        sword.Count = 1;
        sword.ExtraPoisonId = cPoison.Id();
        inventory.Entries = {potions, sword, potions};
        CHECK(book.Of(inventory) == std::vector{cPotion, cPoison});
    }
}

TEST_CASE("A game's form for a recipe is made once, and again when it went", "[recipes]")
{
    RecipeBook book;
    const Recipe cPotion{false, {cFortify}};
    std::map<uint32_t, Recipe> forms; // the engine's created potions
    uint32_t next = 0xFF000800;
    int made = 0;
    const auto cRecipeOf = [&](uint32_t aForm) -> std::optional<Recipe> {
        const auto it = forms.find(aForm);
        return it != forms.end() ? std::optional(it->second) : std::nullopt;
    };
    const auto cMake = [&](const Recipe& acRecipe) {
        ++made;
        forms[next] = acRecipe;
        return next++;
    };

    CHECK(book.FormFor(cPotion.Id(), cRecipeOf, cMake) == 0); // not told it
    CHECK(made == 0);

    SECTION("the brewer's own form, learned with it")
    {
        forms[0xFF000812] = cPotion;
        book.Learn(cPotion, 0xFF000812);
        CHECK(book.FormFor(cPotion.Id(), cRecipeOf, cMake) == 0xFF000812);
        CHECK(made == 0);
    }
    SECTION("made here once, then that one")
    {
        book.Learn(cPotion);
        const uint32_t cForm = book.FormFor(cPotion.Id(), cRecipeOf, cMake);
        CHECK(book.FormFor(cPotion.Id(), cRecipeOf, cMake) == cForm);
        CHECK(made == 1);

        WHEN("the engine let the form go (nothing held it) or reused its id")
        {
            forms.erase(cForm);
            const uint32_t cAgain = book.FormFor(cPotion.Id(), cRecipeOf, cMake);
            CHECK(cAgain != 0);
            CHECK(made == 2);
            forms[cAgain] = Recipe{true, {cRestore}};
            CHECK(book.FormFor(cPotion.Id(), cRecipeOf, cMake) != cAgain);
            CHECK(made == 3);
        }
    }
}
