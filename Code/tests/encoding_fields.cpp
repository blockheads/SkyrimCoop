// Every message round trips through its factory, and its bytes stay those recorded (encoding_golden.h): the bytes of
// a deterministic fill of its fields, as the hand-written serializers wrote them before Fields replaced them. A new
// message is recorded with COOP_GOLDEN_OUT=<file> (existing lines are kept as they are) and its line added there.
#include <catch2/catch.hpp>

#include <Messages/NotifyMessageFactory.h>
#include <Messages/RequestMessageFactory.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "encoding_fields_fill.h"

namespace
{
struct Golden
{
    const char* Name;
    const char* Hex;
};
constexpr Golden s_golden[] = {
    {"", ""},
#include "encoding_golden.h"
};

std::string Hex(const Wire::Buffer& acBuffer, size_t aSize)
{
    static constexpr char s_digits[] = "0123456789abcdef";
    std::string hex;
    for (size_t i = 0; i < aSize; ++i)
    {
        hex += s_digits[acBuffer[i] >> 4];
        hex += s_digits[acBuffer[i] & 0xF];
    }
    return hex;
}

// Recorded this run: each message's name and bytes, written to COOP_GOLDEN_OUT for the ones not yet recorded.
std::map<std::string, std::string>& Recorded()
{
    static std::map<std::string, std::string> s_recorded;
    return s_recorded;
}

template <class T, class TFactory> void RoundTrip(const char* acpName, const TFactory& acFactory)
{
    INFO(acpName);
    T message;
    FillArchive fill;
    fill(message);
    if (!fill.Filled())
    {
        FAIL_CHECK("nothing fills it: give it Fields, or a FillValue in encoding_fields_fill.h");
        return;
    }

    Wire::Buffer buffer;
    Wire::Buffer::Writer writer(&buffer);
    message.Serialize(writer);
    const std::string cHex = Hex(buffer, writer.Size());

    Wire::Buffer::Reader reader(&buffer);
    auto pMessage = acFactory.Extract(reader);
    CHECK_FALSE(reader.Failed());
    if (!pMessage)
    {
        FAIL_CHECK("its factory read nothing back");
        return;
    }
    CHECK(pMessage->GetOpcode() == message.GetOpcode());
    CHECK(*CastUnique<T>(std::move(pMessage)) == message);

    Recorded()[acpName] = cHex;
    const auto* cpGolden = std::find_if(std::begin(s_golden), std::end(s_golden), [&](const Golden& acGolden) { return std::string(acGolden.Name) == acpName; });
    if (cpGolden == std::end(s_golden))
    {
        if (!std::getenv("COOP_GOLDEN_OUT"))
            FAIL_CHECK("no recorded bytes");
        return;
    }
    CHECK(cpGolden->Hex == cHex);
}
} // namespace

TEST_CASE("Every message round trips with the bytes recorded for it", "[encoding.fields]")
{
    const RequestMessageFactory cRequests;
    const NotifyMessageFactory cNotifications;
    size_t count = 0;
    RequestMessageFactory::Visit([&](auto aMeta) {
        using T = typename decltype(aMeta)::Type;
        RoundTrip<T>(RequestName(T::Opcode), cRequests);
        ++count;
        return false;
    });
    NotifyMessageFactory::Visit([&](auto aMeta) {
        using T = typename decltype(aMeta)::Type;
        RoundTrip<T>(NotifyName(T::Opcode), cNotifications);
        ++count;
        return false;
    });
    CHECK(count == size_t(kRequestOpcodeMax) + size_t(kNotifyOpcodeMax));

    // Record the messages not yet recorded, as encoding_golden.h's lines; recorded ones are kept as they are.
    if (const char* cpOut = std::getenv("COOP_GOLDEN_OUT"))
    {
        std::map<std::string, std::string> lines;
        for (const auto& cGolden : s_golden)
            if (*cGolden.Name)
                lines[cGolden.Name] = cGolden.Hex;
        for (const auto& [cName, cHex] : Recorded())
            lines.try_emplace(cName, cHex);
        if (FILE* pFile = std::fopen(cpOut, "w"))
        {
            for (const auto& [cName, cHex] : lines)
                std::fprintf(pFile, "{\"%s\", \"%s\"},\n", cName.c_str(), cHex.c_str());
            std::fclose(pFile);
        }
    }
}
