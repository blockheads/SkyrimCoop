#include <Messages/RequestMessageFactory.h>

static std::function<std::unique_ptr<RequestMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_clientMessageExtractor[kRequestOpcodeMax];

namespace details
{
static struct RequestMessageFactoryInit
{
    RequestMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_clientMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
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

std::unique_ptr<RequestMessage> RequestMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept
{
    uint64_t data;
    aReader.ReadBits(data, sizeof(RequestOpcode) * 8);

    if (data >= kRequestOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<RequestOpcode>(data);
    return s_clientMessageExtractor[opcode](aReader);
}
