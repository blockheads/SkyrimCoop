#pragma once

#include <Interface/FxDelegateArgs.h>
#include <Interface/GFxMovie.h>
#include <map>
#include <string>
#include <variant>

// Vanilla MapMarker leaves TextClip transparent until hovered, including on a disabled party pin.
struct FakeMapMovie final : GFxMovie
{
    std::map<std::string, std::variant<bool, double, std::string>> Variables;

    void AttachMarker(const std::string& acPath)
    {
        Variables[acPath + ".TextClip._visible"] = true;
        Variables[acPath + ".TextClip._alpha"] = 0.0;
        Variables[acPath + ".textField._visible"] = true;
    }
    bool NameVisible(const std::string& acPath) const
    {
        return std::get<bool>(Variables.at(acPath + ".TextClip._visible")) &&
            std::get<double>(Variables.at(acPath + ".TextClip._alpha")) > 0 &&
            std::get<bool>(Variables.at(acPath + ".textField._visible"));
    }
    bool SetVariable(const char* acpPath, const GFxValue& acValue, uint32_t) override
    {
        switch (acValue.type)
        {
        case GFxValue::kBoolean: Variables[acpPath] = acValue.value.boolean; break;
        case GFxValue::kNumber: Variables[acpPath] = acValue.value.number; break;
        case GFxValue::kString: Variables[acpPath] = std::string(acValue.value.string); break;
        default: return false;
        }
        return true;
    }
    bool GetVariable(GFxValue*, const char*) const override { return false; }
    bool SetVariableArray(uint32_t, const char*, uint32_t, const void*, uint32_t, uint32_t) const override { return false; }
    bool Invoke(const char*, void*, const void*, uint32_t) override { return false; }
    void Unk01() override {}
    void Unk02() override {}
    void Unk03() override {}
    void Unk04() override {}
    void Unk05() override {}
    void Unk06() override {}
    void Unk07() override {}
    void Unk08() override {}
    void Unk09() override {}
    void Unk0A() override {}
    void Unk0B() override {}
    void Unk0C() override {}
    void Unk0D() override {}
    void Unk0E() override {}
    void Unk0F() override {}
    void Unk13() override {}
    void Unk14() override {}
    void Unk15() override {}
    void Unk16() override {}
};
