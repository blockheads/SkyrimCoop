#include <Messages/NotifyMessageFactory.h>

static std::function<std::unique_ptr<NotifyMessage>(Wire::Buffer::Reader& aReader)> s_serverMessageExtractor[kNotifyOpcodeMax];

namespace details
{
static struct NotifyMessageFactoryInit
{
    NotifyMessageFactoryInit()
    {
        auto extractor = [](auto& x)
        {
            using T = typename std::remove_reference_t<decltype(x)>::Type;

            s_serverMessageExtractor[T::Opcode] = [](Wire::Buffer::Reader& aReader)
            {
                auto ptr = std::make_unique<T>();
                ptr->DeserializeRaw(aReader);
                return CastUnique<NotifyMessage>(std::move(ptr));
            };

            return false;
        };

        NotifyMessageFactory::Visit(extractor);
    }
} s_NotifyMessageFactoryInit;
} // namespace details

std::unique_ptr<NotifyMessage> NotifyMessageFactory::Extract(Wire::Buffer::Reader& aReader) const noexcept
{
    uint64_t data = 0;
    if (!aReader.ReadBits(data, sizeof(NotifyOpcode) * 8) || data >= kNotifyOpcodeMax) [[unlikely]]
        return {nullptr};

    const auto opcode = static_cast<NotifyOpcode>(data);
    auto pMessage = s_serverMessageExtractor[opcode](aReader);
    // A short read or a bad count: drop the message rather than act on zeros.
    if (aReader.Failed()) [[unlikely]]
        return {nullptr};
    return pMessage;
}
