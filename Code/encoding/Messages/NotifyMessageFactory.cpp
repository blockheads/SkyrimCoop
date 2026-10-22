#include <Messages/NotifyMessageFactory.h>

static std::function<UniquePtr<NotifyMessage>(TiltedPhoques::Buffer::Reader& aReader)> s_serverMessageExtractor[kNotifyOpcodeMax];

namespace details
{
static struct NotifyMessageFactoryInit
{
    NotifyMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_serverMessageExtractor[T::Opcode] = [](TiltedPhoques::Buffer::Reader& aReader)
            {
                auto ptr = TiltedPhoques::MakeUnique<T>();
                ptr->DeserializeRaw(aReader);
                return TiltedPhoques::CastUnique<NotifyMessage>(std::move(ptr));
            };

            return false;
        };

        NotifyMessageFactory::Visit(extractor);
    }
} s_NotifyMessageFactoryInit;
} // namespace details

UniquePtr<NotifyMessage> NotifyMessageFactory::Extract(TiltedPhoques::Buffer::Reader& aReader) const noexcept
{
    uint64_t data;
    aReader.ReadBits(data, sizeof(NotifyOpcode) * 8);

    if (data >= kNotifyOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<NotifyOpcode>(data);
    return s_serverMessageExtractor[opcode](aReader);
}
