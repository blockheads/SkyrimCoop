// libFuzzer on both message factories (docs/design/testing.md, Sanitizers and fuzzing). The first byte picks the
// factory, the rest is a message. What decodes must encode and decode again, reading every bit it wrote, and encode
// to as many bits (not the same ones: the quantized rotations, BodyFrame's, can move a step).
#include <Messages/NotifyMessageFactory.h>
#include <Messages/RequestMessageFactory.h>

#include <cstdio>
#include <cstdlib>
#include <span>

namespace
{
template <class TMessage> size_t Encode(const TMessage& acMessage, Wire::Buffer& aBuffer)
{
    Wire::Buffer::Writer writer(&aBuffer);
    acMessage.Serialize(writer);
    return writer.GetBitPosition();
}

template <class TFactory> void RoundTrip(std::span<const uint8_t> aData)
{
    const TFactory cFactory;
    Wire::Buffer::Reader reader(aData);
    const auto cpMessage = cFactory.Extract(reader);
    if (!cpMessage)
        return;
    Wire::Buffer first, second;
    const size_t cBits = Encode(*cpMessage, first);
    Wire::Buffer::Reader again(std::span(first.GetData(), (cBits + 7) / 8));
    const auto cpAgain = cFactory.Extract(again);
    const char* cpWrong = !cpAgain ? "does not decode again"
                          : again.GetBitPosition() != cBits ? "reads other than it wrote"
                          : Encode(*cpAgain, second) != cBits ? "encodes to another size" : nullptr;
    if (cpWrong)
    {
        std::fprintf(stderr, "opcode %u %s\n", unsigned(cpMessage->GetOpcode()), cpWrong);
        std::abort();
    }
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* apData, size_t aSize)
{
    if (aSize == 0)
        return 0;
    const std::span cMessage(apData + 1, aSize - 1);
    if (apData[0] & 1)
        RoundTrip<NotifyMessageFactory>(cMessage);
    else
        RoundTrip<RequestMessageFactory>(cMessage);
    return 0;
}
