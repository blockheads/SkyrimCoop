#include <Services/SaveLibrary.h>

#include <Bridges/IJoinBridge.h>

#include <algorithm>

SaveLibrary::SaveLibrary(IJoinBridge& aBridge) noexcept
    : m_bridge(aBridge)
{
}

std::string SaveLibrary::Add(std::shared_ptr<const SaveTransfer::Image> apImage) noexcept
{
    if (!apImage)
        return {};
    auto& images = m_pState->Images;
    const std::string cId = apImage->Id;
    std::erase_if(images, [&](const auto& acpImage) { return acpImage->Id == cId; });
    images.push_back(std::move(apImage));
    while (images.size() > kKept)
    {
        const std::string cGone = images.front()->Id;
        images.pop_front();
        std::erase_if(m_pState->Streams, [&](const auto& acStream) { return acStream.first.first == cGone || acStream.first.second == cGone; });
    }
    return cId;
}

std::shared_ptr<const SaveTransfer::Stream> SaveLibrary::Encode(const std::string& acSave, const std::vector<std::string>& acHeld) noexcept
{
    const auto& cImages = m_pState->Images;
    const auto cImage = std::ranges::find(cImages, acSave, [](const auto& acpImage) { return acpImage->Id; });
    if (cImage == cImages.end())
        return std::make_shared<SaveTransfer::Stream>();
    std::shared_ptr<const SaveTransfer::Image> pBase;
    for (auto i = cImages.rbegin(); i != cImages.rend() && !pBase; ++i)
        if ((*i)->Id != acSave && std::ranges::find(acHeld, (*i)->Id) != acHeld.end())
            pBase = *i;

    const Key cKey{acSave, pBase ? pBase->Id : std::string{}};
    if (const auto cStream = m_pState->Streams.find(cKey); cStream != m_pState->Streams.end())
        return cStream->second;
    if (!m_pState->Encoding.insert(cKey).second)
        return nullptr;
    m_bridge.OffThread([pImage = *cImage, pBase, cKey, pWeak = std::weak_ptr(m_pState)]() -> std::function<void()> {
        auto pStream = std::make_shared<SaveTransfer::Stream>();
        pStream->Save = pImage->Id;
        pStream->Base = cKey.second;
        pStream->Size = static_cast<uint32_t>(pImage->Data.size());
        pStream->Data = SaveTransfer::Encode(pImage->Data, pBase ? std::string_view(pBase->Data) : std::string_view{});
        return [pStream, cKey, pWeak] {
            const auto pState = pWeak.lock();
            if (!pState)
                return;
            pState->Encoding.erase(cKey);
            // Still kept: an image dropped meanwhile would never be asked for.
            if (std::ranges::find(pState->Images, cKey.first, [](const auto& acpImage) { return acpImage->Id; }) != pState->Images.end())
                pState->Streams[cKey] = pStream;
        };
    });
    const auto cStream = m_pState->Streams.find(cKey);
    return cStream != m_pState->Streams.end() ? cStream->second : nullptr;
}

void SaveLibrary::Clear() noexcept
{
    m_pState = std::make_shared<State>();
}
