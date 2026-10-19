#pragma once

struct GFxValue;

// A Scaleform movie, a menu's SWF (CommonLibSSE-NG GFxMovie): only the slots used here are named.
struct GFxMovie
{
    virtual ~GFxMovie() = default; // 00
    virtual void Unk01() = 0;
    virtual void Unk02() = 0;
    virtual void Unk03() = 0;
    virtual void Unk04() = 0;
    virtual void Unk05() = 0;
    virtual void Unk06() = 0;
    virtual void Unk07() = 0;
    virtual void Unk08() = 0;
    virtual void Unk09() = 0;
    virtual void Unk0A() = 0;
    virtual void Unk0B() = 0;
    virtual void Unk0C() = 0;
    virtual void Unk0D() = 0;
    virtual void Unk0E() = 0;
    virtual void Unk0F() = 0;
    // Sets an ActionScript variable by its path (aSetType 0: as a plain assignment).
    virtual bool SetVariable(const char* acpPath, const GFxValue& acValue, uint32_t aSetType) = 0; // 10
    // Reads an ActionScript variable by its path into apValue.
    virtual bool GetVariable(GFxValue* apValue, const char* acpPath) const = 0; // 11
    virtual void Unk12() = 0;
    virtual void Unk13() = 0;
    virtual void Unk14() = 0;
    virtual void Unk15() = 0;
    virtual void Unk16() = 0;
    // Calls an ActionScript method by its path; apResult and apArgs may be null (no result, no arguments).
    virtual bool Invoke(const char* acpMethodName, void* apResult, const void* apArgs, uint32_t aNumArgs) = 0; // 17
};
