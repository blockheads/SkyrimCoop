#include <CoSave.h>

#include <Services/SaveService.h>
#include <World.h>
#include <Followers/Records.h>

namespace CoSave
{
namespace
{
// SKSESerializationInterface (CommonLibSSE-NG SKSE/Impl/Stubs.h).
struct SerializationInterface
{
    using Callback = void (*)(SerializationInterface*);

    uint32_t version;
    void (*SetUniqueID)(uint32_t, uint32_t);
    void (*SetRevertCallback)(uint32_t, Callback);
    void (*SetSaveCallback)(uint32_t, Callback);
    void (*SetLoadCallback)(uint32_t, Callback);
    void (*SetFormDeleteCallback)(uint32_t, void*);
    bool (*WriteRecord)(uint32_t, uint32_t, const void*, uint32_t);
    bool (*OpenRecord)(uint32_t, uint32_t);
    bool (*WriteRecordData)(const void*, uint32_t);
    bool (*GetNextRecordInfo)(uint32_t*, uint32_t*, uint32_t*);
    uint32_t (*ReadRecordData)(void*, uint32_t);
    bool (*ResolveHandle)(uint64_t, uint64_t*);
    bool (*ResolveFormID)(uint32_t, uint32_t*);
};

constexpr uint32_t Tag(const char (&acTag)[5]) noexcept
{
    return static_cast<uint32_t>(acTag[0]) << 24 | static_cast<uint32_t>(acTag[1]) << 16 | static_cast<uint32_t>(acTag[2]) << 8 |
           static_cast<uint32_t>(acTag[3]);
}

constexpr uint32_t kUniqueId = Tag("SKCP");
constexpr uint32_t kCharacters = Tag("CHAR");
constexpr uint32_t kFollowers = Tag("FOLL");

SerializationInterface* s_pInterface = nullptr;
uint32_t s_handle = 0;

SaveService* Saves() noexcept
{
    return entt::locator<World>::has_value() ? World::Get().ctx().find<SaveService>() : nullptr;
}

void OnSave(SerializationInterface* apInterface)
{
    if (SaveService* pSaves = Saves())
    {
        const std::string cData = pSaves->WriteCoSave();
        if (!apInterface->WriteRecord(kCharacters, CharacterRecords::kVersion, cData.data(), static_cast<uint32_t>(cData.size())))
            spdlog::error("CoSave: the players' characters were not written ({} bytes)", cData.size());
        const std::string cFollowers = Followers::Encode(pSaves->GetFollowerAnchors());
        if (!apInterface->WriteRecord(kFollowers, 1, cFollowers.data(), static_cast<uint32_t>(cFollowers.size())))
            spdlog::error("CoSave: follower anchors were not written");
    }
}

void OnLoad(SerializationInterface* apInterface)
{
    SaveService* pSaves = Saves();
    uint32_t type = 0, version = 0, length = 0;
    while (pSaves && apInterface->GetNextRecordInfo(&type, &version, &length))
    {
        if (type != kCharacters && type != kFollowers)
            continue;
        std::string data(length, '\0');
        apInterface->ReadRecordData(data.data(), length);
        if (type == kCharacters)
            pSaves->ReadCoSave(data, version);
        else
            Followers::Decode(pSaves->GetFollowerAnchors(), data, version, [apInterface](uint32_t aOld, uint32_t& aNew) {
                return apInterface->ResolveFormID(aOld, &aNew);
            });
    }
}

void OnRevert(SerializationInterface*)
{
    if (SaveService* pSaves = Saves())
        pSaves->RevertCoSave();
}
} // namespace

void Start(void* apSerialization, uint32_t aPluginHandle, bool aFirstLoad) noexcept
{
    s_pInterface = static_cast<SerializationInterface*>(apSerialization);
    s_handle = aPluginHandle;
    if (!s_pInterface)
    {
        spdlog::error("CoSave: SKSE has no serialization interface: characters are not kept in saves");
        return;
    }
    // A hot reload's client takes over the callbacks; the id stays the first client's.
    if (aFirstLoad)
        s_pInterface->SetUniqueID(s_handle, kUniqueId);
    s_pInterface->SetSaveCallback(s_handle, OnSave);
    s_pInterface->SetLoadCallback(s_handle, OnLoad);
    s_pInterface->SetRevertCallback(s_handle, OnRevert);
}

void Stop() noexcept
{
    if (!s_pInterface)
        return;
    s_pInterface->SetSaveCallback(s_handle, nullptr);
    s_pInterface->SetLoadCallback(s_handle, nullptr);
    s_pInterface->SetRevertCallback(s_handle, nullptr);
    s_pInterface = nullptr;
}
} // namespace CoSave
