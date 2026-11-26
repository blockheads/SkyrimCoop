#include <join/SaveStore.h>

#include <join/IJoinBridge.h>

namespace SaveStore
{
std::optional<std::string> Read(IJoinBridge& aBridge, const std::string& acId) noexcept
{
    auto image = aBridge.ReadHeldSave(acId);
    if (image && SaveTransfer::Digest(*image) != acId)
    {
        spdlog::warn("SaveStore: a held save no longer matches its id: dropped");
        aBridge.DropHeldSave(acId);
        image.reset();
    }
    return image;
}

void Check(IJoinBridge& aBridge) noexcept
{
    for (const auto& cSave : aBridge.HeldSaves())
        (void)Read(aBridge, cSave);
}

std::optional<std::string> Decode(IJoinBridge& aBridge, const SaveTransfer::Stream& acStream) noexcept
{
    std::optional<std::string> base;
    if (!acStream.Base.empty() && !(base = Read(aBridge, acStream.Base)))
        return std::nullopt;
    auto image = SaveTransfer::Decode(acStream.Data, base ? std::string_view(*base) : std::string_view{}, acStream.Size);
    if (image && SaveTransfer::Digest(*image) != acStream.Save)
        image.reset();
    return image;
}

bool Keep(IJoinBridge& aBridge, const std::string& acId, const std::string& acImage) noexcept
{
    if (!aBridge.WriteHeldSave(acId, acImage))
        return false;
    const auto cHeld = aBridge.HeldSaves();
    for (size_t i = kKept; i < cHeld.size(); ++i)
        aBridge.DropHeldSave(cHeld[i]);
    return true;
}
} // namespace SaveStore
