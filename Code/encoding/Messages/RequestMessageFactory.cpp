#include <Messages/RequestMessageFactory.h>

static std::function<std::unique_ptr<RequestMessage>(Wire::Buffer::Reader& aReader)> s_clientMessageExtractor[kRequestOpcodeMax];

namespace details
{
static struct RequestMessageFactoryInit
{
    RequestMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_clientMessageExtractor[T::Opcode] = [](Wire::Buffer::Reader& aReader)
            {
                auto ptr = std::make_unique<T>();
                ptr->DeserializeRaw(aReader);
                return CastUnique<RequestMessage>(std::move(ptr));
            };

            return false;
        };

        RequestMessageFactory::Visit(extractor);
    }
} s_RequestMessageFactoryInit;
} // namespace details

std::unique_ptr<RequestMessage> RequestMessageFactory::Extract(Wire::Buffer::Reader& aReader) const noexcept
{
    uint64_t data = 0;
    if (!aReader.ReadBits(data, sizeof(RequestOpcode) * 8) || data >= kRequestOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<RequestOpcode>(data);
    auto pMessage = s_clientMessageExtractor[opcode](aReader);
    // A short read or a bad count: drop the message rather than act on zeros.
    if (aReader.Failed()) [[unlikely]]
        return {nullptr};
    return pMessage;
}
