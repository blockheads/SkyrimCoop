#include <HookSwitch.h>

#include <SaveLoad.h>
#include <Games/Overrides.h>

#include <Forms/TESForm.h>

#include <World.h>

#include <Wire/Serialization.h>
#include <Wire/Buffer.h>
#include <Hooks/FunctionHook.h>
#include <Hooks/Initializer.h>
#include <Wire/ViewBuffer.h>
#include <VersionDb.h>

using Wire::Serialization;
using Wire::ViewBuffer;

BGSSaveFormBuffer::BGSSaveFormBuffer() noexcept
{
    using TCtor = BGSSaveFormBuffer*(BGSSaveFormBuffer*);
    POINTER_SKYRIMSE(TCtor, s_ctor, 36035);
    buffer = nullptr;
    capacity = 0;
    position = 0;
    s_ctor(this);
}

BGSSaveFormBuffer::~BGSSaveFormBuffer() noexcept
{
    using TDtor = void(BGSSaveFormBuffer*);
    POINTER_SKYRIMSE(TDtor, s_dtor, 36044);
    s_dtor(this);
}

void BGSSaveFormBuffer::SetHeader(uint32_t aFormId, uint32_t aChangeFlags, uint8_t aFormType, uint8_t aVersion) noexcept
{
    using TSetHeader = void(BGSSaveFormBuffer*, uint64_t, uint32_t, uint32_t, uint8_t);
    POINTER_SKYRIMSE(TSetHeader, s_setHeader, 36037);
    s_setHeader(this, aFormId, aChangeFlags, aFormType, aVersion);
}

void BGSSaveFormBuffer::Reserve(uint32_t aSize) noexcept
{
    using TReserve = void(BGSSaveFormBuffer*, uint32_t);
    POINTER_SKYRIMSE(TReserve, s_reserve, 36056);
    s_reserve(this, aSize);
}

void BGSSaveFormBuffer::WriteId(uint32_t aId) noexcept
{
    uint32_t modId = 0;
    uint32_t baseId = 0;
    World::Get().GetModSystem().GetServerModId(aId, modId, baseId);

    constexpr uint32_t cMaxVarInts = 20; // two var ints of up to 10 bytes
    Reserve(position + cMaxVarInts);
    ViewBuffer view(reinterpret_cast<uint8_t*>(buffer + position), capacity - position);
    Wire::Buffer::Writer writer(&view);
    Serialization::WriteVarInt(writer, modId);
    Serialization::WriteVarInt(writer, baseId);
    position += writer.Size() & 0xFFFFFFFF;
}

BGSLoadFormBuffer::BGSLoadFormBuffer() noexcept
{
    using TCtor = BGSLoadFormBuffer*(BGSLoadFormBuffer*);
    using TDataCtor = void*(uint32_t*);
    POINTER_SKYRIMSE(TCtor, s_ctor, 35993);          // BGSLoadGameBuffer's
    POINTER_SKYRIMSE(TDataCtor, s_dataCtor, 35984); // BGSLoadFormData's
    POINTER_SKYRIMSE(uint8_t, s_vtable, 187480);      // BGSLoadFormBuffer's
    std::memset(this, 0, sizeof(*this));
    s_ctor(this);
    s_dataCtor(&formId);
    vtable = s_vtable.Get();
    unk1C = -1;
}

void BGSLoadFormBuffer::SetData(const char* apData, uint32_t aSize) noexcept
{
    buffer = apData;
    capacity = size = dataSize = aSize;
    position = 0;
}

BGSSaveLoadGame* BGSSaveLoadGame::Get() noexcept
{
    POINTER_SKYRIMSE(BGSSaveLoadGame*, s_singleton, 403330);
    return *s_singleton.Get();
}

uint8_t BGSSaveLoadGame::CurrentVersion() noexcept
{
    using TCurrentVersion = uint8_t(void*);
    POINTER_SKYRIMSE(TCurrentVersion, s_currentVersion, 35740);
    return s_currentVersion.Get()(nullptr);
}

bool BGSSaveLoadGame::HasChange(const TESForm& acForm, uint32_t aFlags) const noexcept
{
    using TGetChange = bool(const BGSSaveLoadGame*, const TESForm*, uint32_t);
    POINTER_SKYRIMSE(TGetChange, s_getChange, 35577);
    return s_getChange(this, &acForm, aFlags);
}

uint32_t BGSSaveLoadGame::ChangeFlags(const TESForm& acForm, uint32_t aMask) const noexcept
{
    uint32_t flags = 0;
    for (uint32_t left = aMask; left; left &= left - 1)
        if (const uint32_t cBit = left & ~(left - 1); HasChange(acForm, cBit))
            flags |= cBit;
    return flags;
}

bool BGSSaveLoadGame::BeginLoading() noexcept
{
    using TSetLoading = bool(BGSSaveLoadGame*, bool);
    POINTER_SKYRIMSE(TSetLoading, s_setLoading, 19378);
    return s_setLoading(this, false);
}

void BGSSaveLoadGame::EndLoading(bool aWasClear) noexcept
{
    using TSetLoading = bool(BGSSaveLoadGame*, bool);
    POINTER_SKYRIMSE(TSetLoading, s_setLoading, 19378);
    s_setLoading(this, aWasClear);
}

using TBGSLoadFormBuffer_ReadFormId = bool(BGSLoadFormBuffer*, uint32_t&);
using TBGSSaveFormBuffer_WriteFormId = void(BGSSaveFormBuffer*, TESForm*);
using TBGSSaveFormBuffer_WriteId = void(BGSSaveFormBuffer*, uint64_t);

static TBGSSaveFormBuffer_WriteFormId* RealBGSSaveFormBuffer_WriteFormId = nullptr;
static TBGSLoadFormBuffer_ReadFormId* RealBGSLoadFormBuffer_ReadFormId = nullptr;
static TBGSSaveFormBuffer_WriteId* RealBGSSaveFormBuffer_WriteId = nullptr;

void BGSSaveFormBuffer_WriteFormId(BGSSaveFormBuffer* apThis, TESForm* apForm)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        RealBGSSaveFormBuffer_WriteFormId(apThis, apForm);
        return;
    }

    apThis->WriteId(apForm ? apForm->formID : 0);
}

void BGSSaveFormBuffer_WriteId(BGSSaveFormBuffer* apThis, uint64_t aId)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        RealBGSSaveFormBuffer_WriteId(apThis, aId);
        return;
    }

    apThis->WriteId(aId & 0xFFFFFFFF);
}

bool BGSLoadFormBuffer_LoadFormId(BGSLoadFormBuffer* apThis, uint32_t& aFormId)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        return RealBGSLoadFormBuffer_ReadFormId(apThis, aFormId);
    }

    uint8_t* pReadLocation = (uint8_t*)(apThis->buffer + apThis->position);

    ViewBuffer buffer(pReadLocation, apThis->size - apThis->position);
    ViewBuffer::Reader reader(&buffer);

    const uint32_t modId = Serialization::ReadVarInt(reader) & 0xFFFFFFFF;
    const uint32_t baseId = Serialization::ReadVarInt(reader) & 0xFFFFFFFF;

    aFormId = 0;

    if (modId != 0 || baseId != 0)
        aFormId = World::Get().GetModSystem().GetGameId(modId, baseId);

    apThis->position += reader.Size() & 0xFFFFFFFF;

    // As the engine's: true for a reference whose form is gone.
    return (modId != 0 || baseId != 0) && aFormId == 0;
}

static Hooks::Initializer s_saveLoadHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TBGSLoadFormBuffer_ReadFormId, s_readFormId, 36000);
        POINTER_SKYRIMSE(TBGSSaveFormBuffer_WriteFormId, s_writeFormId, 36048);
        POINTER_SKYRIMSE(TBGSSaveFormBuffer_WriteId, s_writeId, 36047);

        RealBGSLoadFormBuffer_ReadFormId = s_readFormId.Get();
        RealBGSSaveFormBuffer_WriteFormId = s_writeFormId.Get();
        RealBGSSaveFormBuffer_WriteId = s_writeId.Get();

        HOOK(&RealBGSLoadFormBuffer_ReadFormId, BGSLoadFormBuffer_LoadFormId);
        HOOK(&RealBGSSaveFormBuffer_WriteFormId, BGSSaveFormBuffer_WriteFormId);
        HOOK(&RealBGSSaveFormBuffer_WriteId, BGSSaveFormBuffer_WriteId);
    });
