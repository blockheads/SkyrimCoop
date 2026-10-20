#include <TiltedOnlinePCH.h>
#include <HookSwitch.h>

#include <SaveLoad.h>
#include <Games/Overrides.h>

#include <Forms/TESForm.h>

#include <World.h>

#include <TiltedCore/Serialization.hpp>
#include <TiltedCore/Buffer.hpp>

using TiltedPhoques::Serialization;
using TiltedPhoques::ViewBuffer;

BGSSaveFormBuffer::BGSSaveFormBuffer() noexcept
{
    TP_THIS_FUNCTION(TCtor, BGSSaveFormBuffer*, BGSSaveFormBuffer);
    POINTER_SKYRIMSE(TCtor, s_ctor, 36035);
    buffer = nullptr;
    capacity = 0;
    position = 0;
    TiltedPhoques::ThisCall(s_ctor, this);
}

BGSSaveFormBuffer::~BGSSaveFormBuffer() noexcept
{
    TP_THIS_FUNCTION(TDtor, void, BGSSaveFormBuffer);
    POINTER_SKYRIMSE(TDtor, s_dtor, 36044);
    TiltedPhoques::ThisCall(s_dtor, this);
}

void BGSSaveFormBuffer::SetHeader(uint32_t aFormId, uint32_t aChangeFlags, uint8_t aFormType, uint8_t aVersion) noexcept
{
    TP_THIS_FUNCTION(TSetHeader, void, BGSSaveFormBuffer, uint64_t, uint32_t, uint32_t, uint8_t);
    POINTER_SKYRIMSE(TSetHeader, s_setHeader, 36037);
    TiltedPhoques::ThisCall(s_setHeader, this, aFormId, aChangeFlags, aFormType, aVersion);
}

void BGSSaveFormBuffer::Reserve(uint32_t aSize) noexcept
{
    TP_THIS_FUNCTION(TReserve, void, BGSSaveFormBuffer, uint32_t);
    POINTER_SKYRIMSE(TReserve, s_reserve, 36056);
    TiltedPhoques::ThisCall(s_reserve, this, aSize);
}

void BGSSaveFormBuffer::WriteId(uint32_t aId) noexcept
{
    uint32_t modId = 0;
    uint32_t baseId = 0;
    World::Get().GetModSystem().GetServerModId(aId, modId, baseId);

    constexpr uint32_t cMaxVarInts = 20; // two var ints of up to 10 bytes
    Reserve(position + cMaxVarInts);
    ViewBuffer view(reinterpret_cast<uint8_t*>(buffer + position), capacity - position);
    Buffer::Writer writer(&view);
    Serialization::WriteVarInt(writer, modId);
    Serialization::WriteVarInt(writer, baseId);
    position += writer.Size() & 0xFFFFFFFF;
}

BGSLoadFormBuffer::BGSLoadFormBuffer() noexcept
{
    TP_THIS_FUNCTION(TCtor, BGSLoadFormBuffer*, BGSLoadFormBuffer);
    TP_THIS_FUNCTION(TDataCtor, void*, uint32_t);
    POINTER_SKYRIMSE(TCtor, s_ctor, 35993);          // BGSLoadGameBuffer's
    POINTER_SKYRIMSE(TDataCtor, s_dataCtor, 35984); // BGSLoadFormData's
    POINTER_SKYRIMSE(uint8_t, s_vtable, 187480);      // BGSLoadFormBuffer's
    std::memset(this, 0, sizeof(*this));
    TiltedPhoques::ThisCall(s_ctor, this);
    TiltedPhoques::ThisCall(s_dataCtor, &formId);
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

bool BGSSaveLoadGame::BeginLoading() noexcept
{
    TP_THIS_FUNCTION(TSetLoading, bool, BGSSaveLoadGame, bool);
    POINTER_SKYRIMSE(TSetLoading, s_setLoading, 19378);
    return TiltedPhoques::ThisCall(s_setLoading, this, false);
}

void BGSSaveLoadGame::EndLoading(bool aWasClear) noexcept
{
    TP_THIS_FUNCTION(TSetLoading, bool, BGSSaveLoadGame, bool);
    POINTER_SKYRIMSE(TSetLoading, s_setLoading, 19378);
    TiltedPhoques::ThisCall(s_setLoading, this, aWasClear);
}

TP_THIS_FUNCTION(TBGSLoadFormBuffer_ReadFormId, bool, BGSLoadFormBuffer, uint32_t&);
TP_THIS_FUNCTION(TBGSSaveFormBuffer_WriteFormId, void, BGSSaveFormBuffer, TESForm*);
TP_THIS_FUNCTION(TBGSSaveFormBuffer_WriteId, void, BGSSaveFormBuffer, uint64_t);

static TBGSSaveFormBuffer_WriteFormId* RealBGSSaveFormBuffer_WriteFormId = nullptr;
static TBGSLoadFormBuffer_ReadFormId* RealBGSLoadFormBuffer_ReadFormId = nullptr;
static TBGSSaveFormBuffer_WriteId* RealBGSSaveFormBuffer_WriteId = nullptr;

void TP_MAKE_THISCALL(BGSSaveFormBuffer_WriteFormId, BGSSaveFormBuffer, TESForm* apForm)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        TiltedPhoques::ThisCall(RealBGSSaveFormBuffer_WriteFormId, apThis, apForm);
        return;
    }

    apThis->WriteId(apForm ? apForm->formID : 0);
}

void TP_MAKE_THISCALL(BGSSaveFormBuffer_WriteId, BGSSaveFormBuffer, uint64_t aId)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        TiltedPhoques::ThisCall(RealBGSSaveFormBuffer_WriteId, apThis, aId);
        return;
    }

    apThis->WriteId(aId & 0xFFFFFFFF);
}

bool TP_MAKE_THISCALL(BGSLoadFormBuffer_LoadFormId, BGSLoadFormBuffer, uint32_t& aFormId)
{
    if (!ScopedSaveLoadOverride::IsOverriden())
    {
        return TiltedPhoques::ThisCall(RealBGSLoadFormBuffer_ReadFormId, apThis, aFormId);
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

static TiltedPhoques::Initializer s_saveLoadHooks(
    []()
    {
        const HookGate cGate(__FILE__);
        POINTER_SKYRIMSE(TBGSLoadFormBuffer_ReadFormId, s_readFormId, 36000);
        POINTER_SKYRIMSE(TBGSSaveFormBuffer_WriteFormId, s_writeFormId, 36048);
        POINTER_SKYRIMSE(TBGSSaveFormBuffer_WriteId, s_writeId, 36047);

        RealBGSLoadFormBuffer_ReadFormId = s_readFormId.Get();
        RealBGSSaveFormBuffer_WriteFormId = s_writeFormId.Get();
        RealBGSSaveFormBuffer_WriteId = s_writeId.Get();

        TP_HOOK(&RealBGSLoadFormBuffer_ReadFormId, BGSLoadFormBuffer_LoadFormId);
        TP_HOOK(&RealBGSSaveFormBuffer_WriteFormId, BGSSaveFormBuffer_WriteFormId);
        TP_HOOK(&RealBGSSaveFormBuffer_WriteId, BGSSaveFormBuffer_WriteId);
    });
