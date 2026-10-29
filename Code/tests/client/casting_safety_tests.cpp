#include <catch2/catch.hpp>
#include <Games/Magic/CastingSafety.h>

TEST_CASE("Casting synchronization accepts absent actors and the native current-spell fallback", "[casting-safety]")
{
    struct Actor
    {
        int* Extension{};
        int* GetExtension() { return Extension; }
    };
    int extension{};
    Actor actor{&extension};
    CHECK(CastingSafety::ExtensionOf(static_cast<Actor*>(nullptr)) == nullptr);
    CHECK(CastingSafety::ExtensionOf(&actor) == &extension);
    actor.Extension = nullptr;
    CHECK(CastingSafety::ExtensionOf(&actor) == nullptr);
    struct Caster { int* pCurrentSpell{}; } caster;
    int spell{}, other{};
    CHECK(CastingSafety::SpellFor(caster, static_cast<int*>(nullptr)) == nullptr);
    caster.pCurrentSpell = &spell;
    CHECK(CastingSafety::SpellFor(caster, static_cast<int*>(nullptr)) == &spell);
    CHECK(CastingSafety::SpellFor(caster, &other) == &other);
}

TEST_CASE("A failed or deferred projectile launch emits no event and keeps the native result", "[casting-safety]")
{
    struct Handle { struct { unsigned iBits{}; } handle; } handle{{42}};
    int projectile{}, resolves{}, notifications{};
    const bool cResolved = GENERATE(false, true);
    auto resolve = [&](unsigned aHandle) -> int* {
        CHECK(aHandle == 42);
        ++resolves;
        return cResolved ? &projectile : nullptr;
    };
    auto notify = [&](int* apProjectile) { CHECK(apProjectile == &projectile); ++notifications; };
    CHECK(CastingSafety::WithProjectile(static_cast<Handle*>(nullptr), resolve, notify) == nullptr);
    CHECK(resolves == 0);
    CHECK(notifications == 0);
    CHECK(CastingSafety::WithProjectile(&handle, resolve, notify) == &handle);
    CHECK(resolves == 1);
    CHECK(notifications == static_cast<int>(cResolved));
}
