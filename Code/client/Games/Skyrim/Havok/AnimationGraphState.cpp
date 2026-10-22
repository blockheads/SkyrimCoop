#include <TiltedOnlinePCH.h>

#include <Havok/AnimationGraphState.h>

#include <BSAnimationGraphManager.h>
#include <Games/Animation/BSAnimationGraphChannel.h>
#include <Havok/BShkbAnimationGraph.h>
#include <Misc/BSFixedString.h>

#include <cstring>

namespace
{
// AnimationStreamSaveGame's 12 slots (43612-43602), each writing what the save buffer's does, in its byte order.
struct AnimationStreamWriter
{
    std::string Bytes;

    virtual ~AnimationStreamWriter() = default;
    // A transform: bytes 0-15, 32-47, then 16-31.
    virtual bool Write48(const uint8_t* apValue)
    {
        Append(apValue, 16);
        Append(apValue + 32, 16);
        Append(apValue + 16, 16);
        return true;
    }
    virtual bool Write16a(const uint8_t* apValue) { return Append(apValue, 16); }
    virtual bool Write16b(const uint8_t* apValue) { return Append(apValue, 16); }
    // Three 32-bit words: the third first.
    virtual bool Write12(const uint32_t* apValue)
    {
        Append(&apValue[2], 4);
        Append(&apValue[0], 4);
        return Append(&apValue[1], 4);
    }
    virtual bool Write4a(const void* apValue) { return Append(apValue, 4); }
    virtual bool Write1(const void* apValue) { return Append(apValue, 1); }
    virtual bool Write2a(const void* apValue) { return Append(apValue, 2); }
    virtual bool Write2b(const void* apValue) { return Append(apValue, 2); }
    virtual bool Write4b(const void* apValue) { return Append(apValue, 4); }
    virtual bool Write4c(const void* apValue) { return Append(apValue, 4); }
    // A string by its characters (the save buffer's 36049): 16-bit length, then the characters.
    virtual bool WriteString(const char* const* apValue)
    {
        const char* cpText = *apValue ? *apValue : "";
        const auto cLength = static_cast<uint16_t>(std::strlen(cpText));
        Append(&cLength, 2);
        return Append(cpText, cLength);
    }

    bool Append(const void* apData, size_t aSize)
    {
        Bytes.append(static_cast<const char*>(apData), aSize);
        return true;
    }
};

// AnimationStreamLoadGame's 14 slots: a version slot, the save's slots, and a second string read.
struct AnimationStreamReader
{
    const std::string& Bytes;
    size_t Offset = 0;
    bool Overrun = false;

    explicit AnimationStreamReader(const std::string& acBytes)
        : Bytes(acBytes)
    {
    }

    virtual ~AnimationStreamReader() = default;
    // The data's version, as the load buffer reports it (35999).
    virtual uint64_t Version()
    {
        using TVersion = uint64_t(void*);
        POINTER_SKYRIMSE(TVersion, s_version, 35999);
        return s_version.Get()(nullptr);
    }
    virtual bool Read48(uint8_t* apValue)
    {
        Take(apValue, 16);
        Take(apValue + 32, 16);
        return Take(apValue + 16, 16);
    }
    virtual bool Read16a(uint8_t* apValue) { return Take(apValue, 16); }
    virtual bool Read16b(uint8_t* apValue) { return Take(apValue, 16); }
    virtual bool Read12(uint32_t* apValue)
    {
        Take(&apValue[2], 4);
        Take(&apValue[0], 4);
        return Take(&apValue[1], 4);
    }
    virtual bool Read4a(void* apValue) { return Take(apValue, 4); }
    virtual bool Read1(void* apValue) { return Take(apValue, 1); }
    virtual bool Read2a(void* apValue) { return Take(apValue, 2); }
    virtual bool Read2b(void* apValue) { return Take(apValue, 2); }
    virtual bool Read4b(void* apValue) { return Take(apValue, 4); }
    virtual bool Read4c(void* apValue) { return Take(apValue, 4); }
    virtual bool ReadString(BSFixedString* apValue) { return TakeString(apValue); }
    virtual bool ReadFixedString(BSFixedString* apValue) { return TakeString(apValue); }

    bool Take(void* apData, size_t aSize)
    {
        if (Offset + aSize > Bytes.size())
        {
            std::memset(apData, 0, aSize);
            Overrun = true;
            return false;
        }
        std::memcpy(apData, Bytes.data() + Offset, aSize);
        Offset += aSize;
        return true;
    }

    bool TakeString(BSFixedString* apValue)
    {
        uint16_t length = 0;
        Take(&length, 2);
        if (Offset + length > Bytes.size())
        {
            Overrun = true;
            apValue->Set("");
            return false;
        }
        const std::string cText(Bytes.data() + Offset, length);
        Offset += length;
        apValue->Set(cText.c_str());
        return true;
    }
};

// BShkbAnimationGraph::SaveState / LoadState (63601, 63602; our names).
using TSaveGraphState = bool(BShkbAnimationGraph*, AnimationStreamWriter* apStream, bool aUnk);
using TLoadGraphState = bool(BShkbAnimationGraph*, AnimationStreamReader* apStream);

// The format: per graph its project name (16-bit length, characters) and its state (32-bit length, bytes).
void AppendBlock(std::string& aOut, const char* apData, size_t aSize, bool aWide)
{
    if (aWide)
    {
        const auto cSize = static_cast<uint32_t>(aSize);
        aOut.append(reinterpret_cast<const char*>(&cSize), 4);
    }
    else
    {
        const auto cSize = static_cast<uint16_t>(aSize);
        aOut.append(reinterpret_cast<const char*>(&cSize), 2);
    }
    aOut.append(apData, aSize);
}

bool TakeBlock(const std::string& acIn, size_t& aOffset, std::string& aOut, bool aWide)
{
    uint32_t size = 0;
    const size_t cHeader = aWide ? 4 : 2;
    if (aOffset + cHeader > acIn.size())
        return false;
    std::memcpy(&size, acIn.data() + aOffset, cHeader);
    aOffset += cHeader;
    if (aOffset + size > acIn.size())
        return false;
    aOut.assign(acIn.data() + aOffset, size);
    aOffset += size;
    return true;
}
} // namespace

namespace AnimationGraphState
{
bool Save(BSAnimationGraphManager* apManager, std::string& aState) noexcept
{
    POINTER_SKYRIMSE(TSaveGraphState, s_saveGraphState, 63601);

    aState.clear();
    if (!apManager)
        return false;

    bool saved = true;
    BSScopedLock<BSRecursiveLock> _{apManager->lock};
    for (uint32_t i = 0; i < apManager->animationGraphs.size; ++i)
    {
        BShkbAnimationGraph* pGraph = apManager->animationGraphs.Get(i);
        if (!pGraph)
            continue;
        AnimationStreamWriter writer;
        // The flag is the save's option bit 4 (TESObjectREFR::SaveGame); a normal save passes it clear.
        saved = s_saveGraphState(pGraph, &writer, false) && saved;
        const char* cpProject = pGraph->projectName ? pGraph->projectName : "";
        AppendBlock(aState, cpProject, std::strlen(cpProject), false);
        AppendBlock(aState, writer.Bytes.data(), writer.Bytes.size(), true);
    }
    return saved && !aState.empty();
}

bool Load(BSAnimationGraphManager* apManager, const std::string& acState) noexcept
{
    POINTER_SKYRIMSE(TLoadGraphState, s_loadGraphState, 63602);

    if (!apManager || acState.empty())
        return false;

    // Saved graphs by project.
    std::vector<std::pair<std::string, std::string>> saved;
    for (size_t offset = 0; offset < acState.size();)
    {
        std::string project, state;
        if (!TakeBlock(acState, offset, project, false) || !TakeBlock(acState, offset, state, true))
            return false;
        saved.emplace_back(std::move(project), std::move(state));
    }

    uint32_t loaded = 0;
    bool ok = true;
    BSScopedLock<BSRecursiveLock> _{apManager->lock};
    for (uint32_t i = 0; i < apManager->animationGraphs.size; ++i)
    {
        BShkbAnimationGraph* pGraph = apManager->animationGraphs.Get(i);
        if (!pGraph || !pGraph->projectName)
            continue;
        const auto cIt = std::find_if(saved.begin(), saved.end(), [pGraph](const auto& acEntry) { return acEntry.first == pGraph->projectName; });
        if (cIt == saved.end())
            continue;
        AnimationStreamReader reader(cIt->second);
        const bool cLoaded = s_loadGraphState(pGraph, &reader);
        ok = ok && cLoaded && !reader.Overrun && reader.Offset == cIt->second.size();
        spdlog::debug("Graph state {}: loaded {}, read {} of {} bytes", pGraph->projectName, cLoaded, reader.Offset, cIt->second.size());
        ++loaded;
    }
    // As the manager's own load does once its graphs are in.
    for (uint32_t i = 0; i < apManager->boundChannels.length; ++i)
        if (apManager->boundChannels.data[i])
            apManager->boundChannels.data[i]->sub_2();
    return ok && loaded > 0;
}
} // namespace AnimationGraphState
