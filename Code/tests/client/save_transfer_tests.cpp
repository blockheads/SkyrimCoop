#include <Services/SaveTransfer.h>
#include <catch2/catch.hpp>
#include <lz4.h>
#include <zlib.h>
#include <cstring>
#include <format>

namespace
{
std::string Noise(size_t aSize, uint32_t aSeed = 0x923dab1)
{
    std::string data(aSize, '\0');
    uint32_t random = aSeed;
    for (auto& c : data)
    {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        c = static_cast<char>(random);
    }
    return data;
}

template <class T> void Put(std::string& aOut, T aValue)
{
    aOut.append(reinterpret_cast<const char*>(&aValue), sizeof(T));
}

// A save file as the engine writes it (docs/re/systems/save-game.md): the header ends in the screenshot's size and
// the compression type; the body follows the screenshot, packed behind its two lengths unless the type is 0.
std::string Ess(const std::string& acBody, uint16_t aType, uint32_t aVersion = 12)
{
    constexpr uint32_t cWidth = 8, cHeight = 4;
    std::string header;
    Put<uint32_t>(header, aVersion);
    Put<uint32_t>(header, 41); // save number
    header += std::string("\x08\x00" "franklin", 10);
    Put<uint64_t>(header, 0x1dd764516); // file time
    Put<uint32_t>(header, cWidth);
    Put<uint32_t>(header, cHeight);
    Put<uint16_t>(header, aType);

    std::string file = "TESV_SAVEGAME";
    Put<uint32_t>(file, static_cast<uint32_t>(header.size()));
    file += header;
    file += Noise(cWidth * cHeight * 4, 77); // the screenshot
    if (aType == 0)
        return file + acBody;
    std::string packed;
    if (aType == 2)
    {
        packed.resize(LZ4_compressBound(static_cast<int>(acBody.size())));
        packed.resize(LZ4_compress_default(acBody.data(), packed.data(), static_cast<int>(acBody.size()), static_cast<int>(packed.size())));
    }
    else
    {
        uLongf length = compressBound(static_cast<uLong>(acBody.size()));
        packed.resize(length);
        compress(reinterpret_cast<Bytef*>(packed.data()), &length, reinterpret_cast<const Bytef*>(acBody.data()), static_cast<uLong>(acBody.size()));
        packed.resize(length);
    }
    Put<uint32_t>(file, static_cast<uint32_t>(acBody.size()));
    Put<uint32_t>(file, static_cast<uint32_t>(packed.size()));
    return file + packed;
}

// A body that packs well: change forms repeat.
std::string Body(size_t aForms, uint32_t aSeed = 5)
{
    std::string body;
    for (size_t i = 0; i < aForms; ++i)
        body += std::format("form {:08x} flags 0x{:04x} ", i * 7, i % 13) + Noise(24, aSeed + static_cast<uint32_t>(i));
    return body;
}
} // namespace

TEST_CASE("A save's two files pack and unpack, and a digest is SHA-256", "[save-transfer]")
{
    SaveTransfer::SaveFiles files{Noise(350000), std::string("co-save\0binary", 14)};
    CHECK(SaveTransfer::Unpack(SaveTransfer::Pack(files)) == files);
    CHECK_FALSE(SaveTransfer::Unpack("x"));
    CHECK_FALSE(SaveTransfer::Unpack(std::string(4, '\xff')));
    CHECK(SaveTransfer::Digest("abc") == std::string("\xba\x78\x16\xbf\x8f\x01\xcf\xea\x41\x41\x40\xde\x5d\xae\x22\x23"
                                                     "\xb0\x03\x61\xa3\x96\x17\x7a\x9c\xb4\x10\xff\x61\xf2\x00\x15\xad",
                                                     32));
}

TEST_CASE("A plain save has its body raw, type 0 and a blank screenshot", "[save-transfer][plain]")
{
    const auto cBody = Body(4000);
    const auto cRaw = Ess(cBody, 0);
    const uint16_t cType = GENERATE(uint16_t{0}, uint16_t{1}, uint16_t{2});
    const auto cPlain = SaveTransfer::Plain(Ess(cBody, cType));

    REQUIRE(cPlain.size() == cRaw.size());
    constexpr size_t cShot = 8 * 4 * 4;
    const size_t cHeaderEnd = cRaw.size() - cBody.size() - cShot;
    // The header as it was but for its type; the engine reads a type 0 body as it lies (109334).
    CHECK(cPlain.substr(0, cHeaderEnd) == cRaw.substr(0, cHeaderEnd));
    CHECK(cPlain.substr(cHeaderEnd, cShot) == std::string(cShot, '\0'));
    CHECK(cPlain.substr(cHeaderEnd + cShot) == cBody);
    CHECK(SaveTransfer::Plain(cPlain) == cPlain);
}

TEST_CASE("What is not a compressed save of this version stays as it is", "[save-transfer][plain]")
{
    const auto cBody = Body(500);
    const auto cSave = Ess(cBody, 2);
    CHECK(SaveTransfer::Plain("a test's save") == "a test's save");
    CHECK(SaveTransfer::Plain("") == "");
    CHECK(SaveTransfer::Plain(Ess(cBody, 2, 11)) == Ess(cBody, 2, 11));
    CHECK(SaveTransfer::Plain(Ess(cBody, 3)) == Ess(cBody, 3));
    // Cut short anywhere, or its packed bytes damaged: never read past the file.
    for (const size_t cSize : {size_t{13}, size_t{17}, size_t{30}, size_t{60}, cSave.size() - 200, cSave.size() - 1})
        CHECK(SaveTransfer::Plain(cSave.substr(0, cSize)) == cSave.substr(0, cSize));
    auto damaged = cSave;
    std::memset(damaged.data() + damaged.size() - 400, 0xFF, 300);
    CHECK(SaveTransfer::Plain(damaged) == damaged);
    // Its raw length off by one (the lengths follow the 36-byte header and the screenshot).
    auto lying = cSave;
    lying[13 + 4 + 36 + 8 * 4 * 4] ^= 0x01;
    CHECK(SaveTransfer::Plain(lying) == lying);
}

TEST_CASE("An image is the plain game file and the co-save under their digest", "[save-transfer][image]")
{
    const SaveTransfer::SaveFiles cFiles{Ess(Body(3000), 2), "co-save"};
    const auto cpImage = SaveTransfer::MakeImage(cFiles);
    REQUIRE(cpImage);
    CHECK(cpImage->Id == SaveTransfer::Digest(cpImage->Data));
    const auto cUnpacked = SaveTransfer::Unpack(cpImage->Data);
    REQUIRE(cUnpacked);
    CHECK(cUnpacked->Game == SaveTransfer::Plain(cFiles.Game));
    CHECK(cUnpacked->CoSave == "co-save");
    // The same world saved under another compression is the same image.
    CHECK(SaveTransfer::MakeImage({Ess(Body(3000), 1), "co-save"})->Id == cpImage->Id);
    CHECK(SaveTransfer::MakeImage({Ess(Body(3000, 6), 2), "co-save"})->Id != cpImage->Id);
}

TEST_CASE("A save encodes alone or against one the guest holds", "[save-transfer][delta]")
{
    // Too random to pack alone, and longer than a compressor's usual window: only the base makes it small.
    const auto cBase = Noise(6 * 1024 * 1024);
    auto target = cBase;
    target.insert(1000, "a new change form");
    target.replace(3 * 1024 * 1024, 5000, Noise(5000, 99));
    target += Noise(20000, 3);

    const auto cAlone = SaveTransfer::Encode(target);
    const auto cDelta = SaveTransfer::Encode(target, cBase);
    REQUIRE_FALSE(cAlone.empty());
    REQUIRE_FALSE(cDelta.empty());
    CHECK(cAlone.size() > target.size() / 2);
    CHECK(cDelta.size() < 100 * 1024);

    const auto cSize = static_cast<uint32_t>(target.size());
    CHECK(SaveTransfer::Decode(cAlone, {}, cSize) == target);
    CHECK(SaveTransfer::Decode(cDelta, cBase, cSize) == target);

    SECTION("a wrong size, damaged bytes or another base never give the save")
    {
        CHECK_FALSE(SaveTransfer::Decode(cDelta, cBase, cSize - 1));
        CHECK_FALSE(SaveTransfer::Decode(cDelta, cBase, 0));
        CHECK_FALSE(SaveTransfer::Decode("not zstd", cBase, cSize));
        auto damaged = cDelta;
        damaged[damaged.size() / 2] ^= 0x55;
        CHECK(SaveTransfer::Decode(damaged, cBase, cSize) != target);
        CHECK(SaveTransfer::Decode(cDelta, Noise(cBase.size(), 4), cSize) != target);
        CHECK(SaveTransfer::Decode(cDelta, {}, cSize) != target);
    }
    SECTION("nothing encodes to nothing")
    {
        CHECK(SaveTransfer::Encode("").empty());
    }
}

TEST_CASE("A stream's pieces assemble in order", "[save-transfer][stream]")
{
    const SaveTransfer::Stream cStream{std::string(32, 's'), std::string(32, 'b'), 900000, Noise(100000)};
    std::vector<NotifySaveChunk> pieces;
    for (size_t offset = 0; offset < cStream.Data.size(); offset += NotifySaveChunk::kMaxData)
        pieces.push_back(cStream.Piece(offset, true));
    REQUIRE(pieces.size() == 4);
    CHECK(pieces.back().Data.size() == 100000 - 3 * NotifySaveChunk::kMaxData);

    SaveTransfer::Assembly assembly;
    CHECK_FALSE(assembly.Complete());
    for (const auto& cPiece : pieces)
    {
        CHECK(cPiece.Ahead);
        CHECK_FALSE(assembly.Complete());
        REQUIRE(assembly.Add(cPiece));
    }
    REQUIRE(assembly.Complete());
    CHECK(assembly.Progress() == 1.f);
    CHECK(assembly.Get().Save == cStream.Save);
    CHECK(assembly.Get().Base == cStream.Base);
    CHECK(assembly.Get().Size == cStream.Size);
    CHECK(assembly.Get().Data == cStream.Data);

    SECTION("a newer save's first piece starts over")
    {
        const SaveTransfer::Stream cNewer{std::string(32, 'n'), {}, 5000, Noise(40000, 8)};
        SaveTransfer::Assembly replaced;
        REQUIRE(replaced.Add(pieces[0]));
        REQUIRE(replaced.Add(cNewer.Piece(0, true)));
        CHECK_FALSE(replaced.Complete());
        REQUIRE(replaced.Add(cNewer.Piece(NotifySaveChunk::kMaxData, true)));
        REQUIRE(replaced.Complete());
        CHECK(replaced.Get().Save == cNewer.Save);
        CHECK(replaced.Get().Data == cNewer.Data);
    }
    SECTION("a piece out of order, of another save, or out of bounds is refused")
    {
        SaveTransfer::Assembly other;
        CHECK_FALSE(other.Add(pieces[1]));
        REQUIRE(other.Add(pieces[0]));
        CHECK_FALSE(other.Add(pieces[2]));
        auto mixed = pieces[1];
        mixed.Save = std::string(32, 'x');
        CHECK_FALSE(other.Add(mixed));
        mixed = pieces[1];
        mixed.Total += 1;
        CHECK_FALSE(other.Add(mixed));
        mixed = pieces[1];
        mixed.Base.clear();
        CHECK_FALSE(other.Add(mixed));
        REQUIRE(other.Add(pieces[1]));

        auto bad = pieces[0];
        bad.Save = "short";
        CHECK_FALSE(SaveTransfer::Assembly{}.Add(bad));
        bad = pieces[0];
        bad.Size = SaveTransfer::kMaxSave + 1;
        CHECK_FALSE(SaveTransfer::Assembly{}.Add(bad));
        bad = pieces[0];
        bad.Data.clear();
        CHECK_FALSE(SaveTransfer::Assembly{}.Add(bad));
        bad = pieces[0];
        bad.Data.resize(NotifySaveChunk::kMaxData + 1);
        CHECK_FALSE(SaveTransfer::Assembly{}.Add(bad));
        bad = pieces[0];
        bad.Base = "short";
        CHECK_FALSE(SaveTransfer::Assembly{}.Add(bad));
    }
}
