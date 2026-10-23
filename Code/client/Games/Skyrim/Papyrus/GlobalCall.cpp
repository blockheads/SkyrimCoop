#include <Papyrus/GlobalCall.h>

#include <Actor.h>
#include <Forms/TESForm.h>
#include <Misc/BSScript.h>
#include <Misc/GameVM.h>
#include <Overloaded.h>
#include <RTTI.h>

namespace GlobalCall
{
namespace
{
// The call's arguments, filled by the VM within the dispatch (IFunctionArguments, CommonLibSSE-NG).
struct Arguments final : BSScript::IFunctionArguments
{
    explicit Arguments(std::span<const LocalValue> aValues) noexcept
        : m_values(aValues)
    {
    }

    bool Prepare(Statement* apArguments) const noexcept override
    {
        apArguments->SetSize(static_cast<uint32_t>(m_values.size()));
        for (size_t i = 0; i < m_values.size(); ++i)
            Write(apArguments->vars[i], m_values[i]);
        return true;
    }

private:
    std::span<const LocalValue> m_values;
};

// The call's result, told on a script thread (IStackCallbackFunctor), held by the VM's smart pointer until then.
struct ReturnFunctor final : BSScript::IStackCallbackFunctor
{
    explicit ReturnFunctor(OnReturn aOnReturn) noexcept
        : m_onReturn(std::move(aOnReturn))
    {
    }

    void operator()(BSScript::Variable aResult) override { m_onReturn(Read(aResult).value_or(std::monostate{})); }

private:
    OnReturn m_onReturn;
};
} // namespace

bool Queue(std::string_view aClass, std::string_view aFunction, std::span<const LocalValue> aArguments, OnReturn aOnReturn) noexcept
{
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    if (!pVm)
        return false;
    Arguments arguments(aArguments);
    // BSTSmartPointer: refCount at 08; ours released once the VM holds its own.
    ReturnFunctor* pCallback = aOnReturn ? new ReturnFunctor(std::move(aOnReturn)) : nullptr;
    if (pCallback)
        pCallback->refCount = 1;
    const BSFixedString cClass(std::string(aClass).c_str());
    const BSFixedString cFunction(std::string(aFunction).c_str());
    const bool cQueued = pVm->DispatchStaticCall(cClass, cFunction, &arguments, reinterpret_cast<void**>(&pCallback));
    if (pCallback && _InterlockedDecrement(reinterpret_cast<volatile long*>(&pCallback->refCount)) == 0)
        delete pCallback;
    return cQueued;
}

std::optional<LocalValue> Read(const BSScript::Variable& acVariable) noexcept
{
    switch (acVariable.type)
    {
    case BSScript::Variable::kInteger: return acVariable.data.i;
    case BSScript::Variable::kFloat: return acVariable.data.f;
    case BSScript::Variable::kBoolean: return acVariable.data.b;
    case BSScript::Variable::kString: return std::string(acVariable.data.s ? acVariable.data.s : "");
    case BSScript::Variable::kEmpty: return std::monostate{};
    default:
        // An object's type is kObject or its class (low bit clear; set, and 11-15: arrays, not carried).
        if (acVariable.type != BSScript::Variable::kObject && (acVariable.type <= BSScript::Variable::kMax || (acVariable.type & 1)))
            return std::nullopt;
        auto* pObject = acVariable.GetObject();
        if (!pObject)
            return std::monostate{}; // None
        const uint64_t cHandle = pObject->GetHandle();
        auto* pForm = BSScript::IsFormHandle(cHandle) ? TESForm::GetById(BSScript::FormIdOfHandle(cHandle)) : nullptr;
        if (!pForm || (pForm->formID >= 0xFF000000 && !Cast<Actor>(pForm)))
            return std::nullopt;
        return LocalForm{pForm->formID};
    }
}

void Write(BSScript::Variable& aVariable, const LocalValue& acValue) noexcept
{
    std::visit(Overloaded{
                   [&aVariable](std::monostate) { aVariable.Clear(); },
                   [&aVariable](const std::string& acString) { aVariable.Set(acString.c_str()); },
                   [&aVariable](const LocalForm& acForm) { aVariable.SetForm(TESForm::GetById(acForm.FormId), GameVM::Get()->virtualMachine); },
                   [&aVariable](auto aNumber) { aVariable.Set(aNumber); },
               },
               acValue);
}
} // namespace GlobalCall
