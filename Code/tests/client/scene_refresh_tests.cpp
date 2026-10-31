#include <catch2/catch.hpp>
#include <Services/SceneRefresh.h>

TEST_CASE("A scene ending after its event publishes the final owner state", "[scene-refresh]")
{
    SceneRefresh::States sent;
    sent[7].Scene = GameId{0, 0xC791A};
    ActorAiState native = sent.at(7);
    auto read = [&](uint32_t aId) -> std::optional<ActorAiState> {
        CHECK(aId == 7);
        return native;
    };
    CHECK(SceneRefresh::ReadChanges(sent, read).empty());
    native.Scene = {};
    native.Combat = 2;
    auto changes = SceneRefresh::ReadChanges(sent, read);
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].first == 7);
    CHECK(changes[0].second == native);
    sent[7] = changes[0].second;
    int reads = 0;
    CHECK(SceneRefresh::ReadChanges(sent, [&](uint32_t) -> std::optional<ActorAiState> {
        ++reads;
        return native;
    }).empty());
    CHECK(reads == 0);
}

TEST_CASE("Scene refresh preserves a replacement scene and ignores missing owners", "[scene-refresh]")
{
    SceneRefresh::States sent;
    sent[7].Scene = GameId{0, 0xC791A};
    sent[8] = {};
    ActorAiState replacement;
    replacement.Scene = GameId{0, 0xC791B};
    auto changes = SceneRefresh::ReadChanges(sent, [&](uint32_t aId) -> std::optional<ActorAiState> {
        CHECK(aId == 7);
        return replacement;
    });
    REQUIRE(changes.size() == 1);
    CHECK(changes[0].second.Scene == replacement.Scene);
    CHECK(SceneRefresh::ReadChanges(sent, [](uint32_t) -> std::optional<ActorAiState> { return std::nullopt; }).empty());
}
