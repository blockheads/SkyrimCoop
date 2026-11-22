#include <Services/CopyRecords.h>
#include <catch2/catch.hpp>

#include <set>

// The copy record (docs/design/one-simulator.md, piece 1) with a stand-in part: its bytes name the actor it needs.
namespace
{
constexpr RecordKind kKind = RecordKind::kAiState;

struct Game
{
    Game()
    {
        Records.Register(
            kKind,
            [this](uint32_t aId, const std::string& acBytes) -> std::vector<uint32_t> {
                if (!Here.contains(aId))
                    return {};
                Applied.emplace_back(aId, acBytes);
                const uint32_t cNamed = acBytes.empty() ? 0 : static_cast<uint8_t>(acBytes[0]);
                return cNamed && !Here.contains(cNamed) ? std::vector<uint32_t>{cNamed} : std::vector<uint32_t>{};
            },
            [](uint32_t aFormId) -> std::optional<std::string> { return std::string(1, static_cast<char>(aFormId)); });
        Records.SendWith([this](uint32_t aId, const RecordPart& acPart) { Sent.emplace_back(aId, acPart); });
    }
    void Appears(uint32_t aId)
    {
        Here.insert(aId);
        Records.OnSessionId(aId);
    }

    CopyRecords Records;
    std::set<uint32_t> Here;                                // the session ids with an actor on this game
    std::vector<std::pair<uint32_t, std::string>> Applied;  // each Apply that reached a copy
    std::vector<std::pair<uint32_t, RecordPart>> Sent;
};

RecordPart Part(uint32_t aVersion, std::string aBytes)
{
    return {kKind, aVersion, std::move(aBytes)};
}
} // namespace

TEST_CASE("The newest version of a part wins, in any order", "[records][copy-records]")
{
    Game game;
    game.Here.insert(5);
    CHECK(game.Records.Take(5, Part(2, "b")));
    CHECK_FALSE(game.Records.Take(5, Part(1, "a"))); // the spawn read before the change arrives after it
    CHECK_FALSE(game.Records.Take(5, Part(2, "x"))); // the same version twice
    CHECK(game.Records.Take(5, Part(3, "c")));
    REQUIRE(game.Applied.size() == 2);
    CHECK(game.Applied[0].second == "b");
    CHECK(game.Applied[1].second == "c");
    CHECK(game.Records.Find(5, kKind)->Bytes == "c");
}

TEST_CASE("A part taken before its copy exists is applied when the copy appears", "[records][copy-records]")
{
    Game game;
    game.Records.Take(5, Part(1, "a"));
    game.Records.Take(5, Part(2, "b"));
    CHECK(game.Applied.empty());
    game.Appears(5);
    REQUIRE(game.Applied.size() == 1);
    CHECK(game.Applied[0] == std::pair<uint32_t, std::string>{5, "b"});
}

TEST_CASE("A part naming an actor that is not here waits for it", "[records][copy-records]")
{
    Game game;
    game.Here.insert(5);
    const std::string cNames9(1, char(9));
    game.Records.Take(5, Part(1, cNames9));
    CHECK(game.Applied.size() == 1);
    CHECK(game.Records.WaitsFor(5, kKind, 9));

    game.Appears(8); // somebody else
    CHECK(game.Applied.size() == 1);
    game.Appears(9);
    CHECK(game.Applied.size() == 2);
    CHECK_FALSE(game.Records.WaitsFor(5, kKind, 9));
    game.Appears(9); // again: nothing waits any more
    CHECK(game.Applied.size() == 2);
}

TEST_CASE("A newer part ends the wait of the one it replaces; a forgotten actor waits for nobody", "[records][copy-records]")
{
    Game game;
    game.Here.insert(5);
    game.Records.Take(5, Part(1, std::string(1, char(9))));
    game.Records.Take(5, Part(2, std::string(1, char(7))));
    CHECK_FALSE(game.Records.WaitsFor(5, kKind, 9));
    CHECK(game.Records.WaitsFor(5, kKind, 7));
    game.Records.Forget(5);
    CHECK_FALSE(game.Records.WaitsFor(5, kKind, 7));
    CHECK(game.Records.Find(5, kKind) == nullptr);
    game.Applied.clear();
    game.Appears(7);
    CHECK(game.Applied.empty());
    // Its id is never used again, but a late part must not find an old version in the way.
    CHECK(game.Records.Take(5, Part(1, "a")));
}

TEST_CASE("An owner's versions go on from the last owner's", "[records][copy-records]")
{
    Game game;
    game.Records.Take(5, Part(6, "theirs")); // a copy here until now
    game.Records.Declare(5, kKind, "ours");
    REQUIRE(game.Sent.size() == 1);
    CHECK(game.Sent[0].first == 5);
    CHECK(game.Sent[0].second == Part(7, "ours"));
    // The last owner's late part does not undo it.
    CHECK_FALSE(game.Records.Take(5, Part(6, "late")));
    // Asked again (R3): the same bytes under a new version; nothing held, nothing sent.
    CHECK(game.Records.DeclareAgain(5, kKind));
    CHECK(game.Sent.back().second == Part(8, "ours"));
    CHECK_FALSE(game.Records.DeclareAgain(6, kKind));
    CHECK(game.Sent.size() == 2);
}

TEST_CASE("An announcement carries every kind read, under its first version", "[records][copy-records]")
{
    Game game;
    const RecordParts cParts = game.Records.ReadAll(5, 0x14);
    REQUIRE(cParts.Parts.size() == 1);
    CHECK(cParts.Parts[0] == Part(1, std::string(1, char(0x14))));
    CHECK(game.Sent.empty()); // the announcement carries it
    game.Records.Declare(5, kKind, "next");
    CHECK(game.Sent.back().second.Version == 2);
}
