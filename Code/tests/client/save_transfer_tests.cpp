#include <Services/SaveTransfer.h>
#include <catch2/catch.hpp>
#include <snappy.h>

namespace
{
std::string Noise(size_t aSize)
{
    std::string data(aSize, '\0');
    uint32_t random = 0x923dab1;
    for (auto& c : data)
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        c = static_cast<char>(random);
    }
    return data;
}
} // namespace

TEST_CASE("Save blocks reconstruct the exact game and SKSE files", "[save-transfer]")
{
    SaveTransfer::SaveFiles files{Noise(350000), std::string("co-save\0binary", 14)};
    const auto cPacked = SaveTransfer::Pack(files);
    const auto cChunks = SaveTransfer::Split(cPacked);
    REQUIRE(cChunks.size() > 8);
    SaveTransfer::Assembly assembly;
    for (const auto& cChunk : cChunks)
    {
        CHECK(cChunk.RawSize <= SaveTransfer::kMaxBlock);
        CHECK(cChunk.Data.size() <= NotifySaveChunk::kMaxData);
        REQUIRE(assembly.Add(cChunk));
    }
    REQUIRE(assembly.Complete());
    CHECK(assembly.Progress() == 1.f);
    CHECK(SaveTransfer::Unpack(assembly.Data()) == files);
    CHECK_FALSE(SaveTransfer::Unpack("x"));
    CHECK_FALSE(SaveTransfer::Unpack(std::string(4, '\xff')));
    CHECK(SaveTransfer::Digest("abc") == std::string("\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23"
                                                     "\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad",
                                                     32));
}

TEST_CASE("Save block references reuse a verified prior save even after insertion", "[save-transfer]")
{
    SaveTransfer::SaveFiles old{Noise(400000), "old co-save"};
    auto files = old;
    SECTION("unchanged save")
    {
    }
    SECTION("inserted data and a changed SKSE co-save")
    {
        files.Game.insert(80000, "new world state");
        files.CoSave = "new co-save";
    }
    SaveTransfer::Cache cache;
    cache.Load(old);
    auto chunks = SaveTransfer::Split(SaveTransfer::Pack(files));
    size_t reused = 0;
    SaveTransfer::Assembly assembly;
    for (auto& chunk : chunks)
    {
        if (cache.Find(chunk.Hash))
        {
            reused += chunk.RawSize;
            chunk.Data.clear();
        }
        REQUIRE(assembly.Add(chunk, &cache));
    }
    REQUIRE(assembly.Complete());
    CHECK(SaveTransfer::Unpack(assembly.Data()) == files);
    CHECK(reused > 300000);
    cache.Clear();
    CHECK(cache.Hashes().empty());
}

TEST_CASE("Bad save data never reaches the loader", "[save-transfer]")
{
    const auto chunks = SaveTransfer::Split(SaveTransfer::Pack({Noise(80000), "SKSE"}));
    REQUIRE(chunks.size() > 2);
    auto chunk = chunks.front();
    SaveTransfer::Assembly assembly;
    SECTION("missing advertised cache block")
    {
        chunk.Data.clear();
    }
    SECTION("corrupted payload")
    {
        chunk.Data[chunk.Data.size() / 2] ^= 0x40;
    }
    SECTION("wrong fingerprint")
    {
        chunk.Hash[0] ^= 0x40;
    }
    SECTION("wrong declared block size")
    {
        ++chunk.RawSize;
    }
    SECTION("excessive block size")
    {
        chunk.RawSize = SaveTransfer::kMaxBlock + 1;
    }
    SECTION("excessive total")
    {
        chunk.Total = SaveTransfer::kMaxSave + 1;
    }
    SECTION("empty block")
    {
        chunk.RawSize = 0;
    }
    SECTION("wrong fingerprint size")
    {
        chunk.Hash.clear();
    }
    SECTION("truncated compressed block")
    {
        chunk.Data.pop_back();
    }
    SECTION("offset beyond end")
    {
        chunk.Offset = chunk.Total + 1;
    }
    SECTION("decompression expansion")
    {
        const std::string bomb(1024 * 1024, 'x');
        snappy::Compress(bomb.data(), bomb.size(), &chunk.Data);
    }
    REQUIRE_FALSE(assembly.Add(chunk));
    CHECK_FALSE(assembly.Complete());
    CHECK(assembly.Data().empty());
}

TEST_CASE("Save blocks reject missing, repeated, and mixed transfers", "[save-transfer]")
{
    const auto chunks = SaveTransfer::Split(SaveTransfer::Pack({Noise(100000), "SKSE"}));
    REQUIRE(chunks.size() > 3);
    SaveTransfer::Assembly assembly;
    REQUIRE(assembly.Add(chunks[0]));
    SECTION("gap")
    {
        CHECK_FALSE(assembly.Add(chunks[2]));
    }
    SECTION("repeat")
    {
        REQUIRE(assembly.Add(chunks[1]));
        CHECK_FALSE(assembly.Add(chunks[1]));
    }
    SECTION("different transfer")
    {
        auto other = chunks[1];
        ++other.Total;
        CHECK_FALSE(assembly.Add(other));
    }
    CHECK_FALSE(assembly.Complete());
}
