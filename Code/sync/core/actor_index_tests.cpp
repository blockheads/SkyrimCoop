// ActorIndex: this game's actors by session id and form id, kept by the registry's signals.
#include <catch2/catch.hpp>

#include <core/ActorIndex.h>
#include <core/FormIdComponent.h>
#include <core/LocalComponent.h>
#include <core/RemoteComponent.h>

TEST_CASE("Actor index: an actor is found by its ids until they go", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cActor = registry.create();
    registry.emplace<FormIdComponent>(cActor, 0x1A2Bu);
    registry.emplace<LocalComponent>(cActor, 7u);
    CHECK(index.ByForm(0x1A2B) == cActor);
    CHECK(index.BySession(7) == cActor);
    CHECK(index.BySession<FormIdComponent, LocalComponent>(7) == cActor);
    CHECK(index.BySession<RemoteComponent>(7) == entt::entity{entt::null});
    CHECK(index.BySession(8) == entt::entity{entt::null});

    registry.remove<FormIdComponent>(cActor);
    CHECK(index.ByForm(0x1A2B) == entt::entity{entt::null});
    CHECK(index.BySession(7) == cActor);
    CHECK(index.BySession<FormIdComponent>(7) == entt::entity{entt::null});

    registry.destroy(cActor);
    CHECK(index.BySession(7) == entt::entity{entt::null});
}

TEST_CASE("Actor index: a held copy has a form id and no session id", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cHeld = registry.create();
    registry.emplace<FormIdComponent>(cHeld, 0x1A2Bu);
    CHECK(index.ByForm(0x1A2B) == cHeld);
    CHECK(index.ByForm<LocalComponent>(0x1A2B) == entt::entity{entt::null});
    CHECK(index.BySession(0) == entt::entity{entt::null});
}

TEST_CASE("Actor index: an actor handed over keeps its session id", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cActor = registry.create();
    registry.emplace<FormIdComponent>(cActor, 0x1A2Bu);
    registry.emplace<LocalComponent>(cActor, 7u);

    // GiveUpOwnership: the Local goes, then the Remote comes.
    registry.remove<LocalComponent>(cActor);
    CHECK(index.BySession(7) == entt::entity{entt::null});
    registry.emplace_or_replace<RemoteComponent>(cActor, 7u, 0x1A2Bu);
    CHECK(index.BySession<RemoteComponent>(7) == cActor);

    // TakeOwnership: the Local comes while the Remote is still there, then the Remote goes.
    registry.emplace_or_replace<LocalComponent>(cActor, 7u);
    CHECK(index.BySession<LocalComponent>(7) == cActor);
    registry.remove<RemoteComponent>(cActor);
    CHECK(index.BySession<LocalComponent>(7) == cActor);
    CHECK(index.ByForm(0x1A2B) == cActor);
}

TEST_CASE("Actor index: a replaced id is found by the new one only", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cActor = registry.create();
    registry.emplace<FormIdComponent>(cActor, 0x1A2Bu);
    registry.emplace<RemoteComponent>(cActor, 7u, 0x1A2Bu);
    registry.emplace_or_replace<FormIdComponent>(cActor, 0x3C4Du);
    registry.emplace_or_replace<RemoteComponent>(cActor, 9u, 0x3C4Du);
    CHECK(index.ByForm(0x1A2B) == entt::entity{entt::null});
    CHECK(index.ByForm(0x3C4D) == cActor);
    CHECK(index.BySession(7) == entt::entity{entt::null});
    CHECK(index.BySession(9) == cActor);

    // A Local's session id is the actor's while it has both.
    registry.emplace<LocalComponent>(cActor, 11u);
    CHECK(index.BySession(9) == entt::entity{entt::null});
    CHECK(index.BySession(11) == cActor);
    registry.remove<LocalComponent>(cActor);
    CHECK(index.BySession(9) == cActor);
}

TEST_CASE("Actor index: an id reused after a respawn finds the new actor", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cOld = registry.create();
    registry.emplace<FormIdComponent>(cOld, 0x1A2Bu);
    registry.emplace<RemoteComponent>(cOld, 7u, 0x1A2Bu);
    registry.destroy(cOld);

    const auto cNew = registry.create();
    registry.emplace<RemoteComponent>(cNew, 7u, 0x1A2Bu);
    registry.emplace<FormIdComponent>(cNew, 0x1A2Bu);
    CHECK(index.ByForm(0x1A2B) == cNew);
    CHECK(index.BySession(7) == cNew);
}

TEST_CASE("Actor index: an id on two actors finds the newer, then the other", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cFirst = registry.create();
    registry.emplace<FormIdComponent>(cFirst, 0x1A2Bu);
    const auto cSecond = registry.create();
    registry.emplace<FormIdComponent>(cSecond, 0x1A2Bu);
    registry.emplace<LocalComponent>(cFirst, 7u);
    CHECK(index.ByForm(0x1A2B) == cSecond);
    CHECK(index.ByForm<LocalComponent>(0x1A2B) == cFirst);

    registry.destroy(cSecond);
    CHECK(index.ByForm(0x1A2B) == cFirst);
}

TEST_CASE("Actor index: a cleared registry has no actors", "[ActorIndex]")
{
    entt::registry registry;
    ActorIndex index(registry);

    const auto cActor = registry.create();
    registry.emplace<FormIdComponent>(cActor, 0x1A2Bu);
    registry.emplace<LocalComponent>(cActor, 7u);
    registry.clear();
    CHECK(index.ByForm(0x1A2B) == entt::entity{entt::null});
    CHECK(index.BySession(7) == entt::entity{entt::null});

    const auto cNext = registry.create();
    registry.emplace<FormIdComponent>(cNext, 0x1A2Bu);
    CHECK(index.ByForm(0x1A2B) == cNext);
}
