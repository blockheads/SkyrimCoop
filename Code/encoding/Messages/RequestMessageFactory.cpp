#include <Messages/RequestMessageFactory.h>

static std::function<UniquePtr<RequestMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_clientMessageExtractor[kRequestOpcodeMax];

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
                auto ptr = TiltedPhoques::MakeUnique<T>();
                ptr->DeserializeRaw(aReader);
                return TiltedPhoques::CastUnique<RequestMessage>(std::move(ptr));
            };

            return false;
        };

        RequestMessageFactory::Visit(extractor);
    }
} s_RequestMessageFactoryInit;
} // namespace details

UniquePtr<RequestMessage> RequestMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept
{
    uint64_t data;
    aReader.ReadBits(data, sizeof(RequestOpcode) * 8);

    if (data >= kRequestOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<RequestOpcode>(data);
    return s_clientMessageExtractor[opcode](aReader);
}
