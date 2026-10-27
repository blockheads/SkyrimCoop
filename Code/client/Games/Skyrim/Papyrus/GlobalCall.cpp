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
bool IsObjectType(uintptr_t aType) noexcept
{
    return aType >= BSScript::Variable::kMax && !(aType & 1);
}

// The function's parameters' types; none for a function the class does not declare.
std::vector<uintptr_t> ParameterTypes(BSScript::IVirtualMachine* apVm, std::string_view aClass, std::string_view aFunction) noexcept
{
    BSScript::ObjectTypeInfo* pType = nullptr;
    std::vector<uintptr_t> types;
    if (!apVm->GetScriptObjectType1(BSFixedString(std::string(aClass).c_str()), &pType) || !pType)
        return types;
    if (BSScript::IFunction* pFunction = pType->GetFunction(std::string(aFunction).c_str()))
        for (uint32_t i = 0; i < pFunction->GetParamCount(); ++i)
        {
            BSFixedString name;
            BSScript::Variable::Type type{};
            pFunction->GetParam(i, name, type);
            types.push_back(type);
        }
    _InterlockedDecrement(reinterpret_cast<volatile long*>(pType)); // the lookup's; the VM keeps its types
    return types;
}

// The call's arguments, filled by the VM within the dispatch (IFunctionArguments, CommonLibSSE-NG). An object goes as
// its parameter's type: the VM takes no other (an Actor for an ObjectReference fails, 141461D00).
struct Arguments final : BSScript::IFunctionArguments
{
    Arguments(std::span<const LocalValue> aValues, std::vector<uintptr_t> aTypes) noexcept
        : m_values(aValues)
        , m_types(std::move(aTypes))
    {
    }

    bool Prepare(Statement* apArguments) const noexcept override
    {
        if (!apArguments->SetSize(static_cast<uint32_t>(m_values.size())))
            return false;
        for (size_t i = 0; i < m_values.size(); ++i)
        {
            BSScript::Variable& variable = apArguments->vars[i];
            Write(variable, m_values[i]);
            const uintptr_t cParameter = i < m_types.size() ? m_types[i] : 0;
            if (IsObjectType(variable.type) && IsObjectType(cParameter) &&
                reinterpret_cast<const BSScript::ObjectTypeInfo*>(variable.type)->IsA(reinterpret_cast<const BSScript::ObjectTypeInfo*>(cParameter)))
                variable.type = static_cast<BSScript::Variable::Type>(cParameter);
        }
        return true;
    }

private:
    std::span<const LocalValue> m_values;
    std::vector<uintptr_t> m_types;
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

// aSelf: a method's object handle; 0 for a global function.
bool Dispatch(uint64_t aSelf, std::string_view aClass, std::string_view aFunction, std::span<const LocalValue> aArguments, OnReturn aOnReturn) noexcept
{
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    if (!pVm)
        return false;
    Arguments arguments(aArguments, ParameterTypes(pVm, aClass, aFunction));
    // BSTSmartPointer: refCount at 08; ours released once the VM holds its own.
    ReturnFunctor* pCallback = aOnReturn ? new ReturnFunctor(std::move(aOnReturn)) : nullptr;
    if (pCallback)
        pCallback->refCount = 1;
    const BSFixedString cClass(std::string(aClass).c_str());
    const BSFixedString cFunction(std::string(aFunction).c_str());
    void** ppCallback = reinterpret_cast<void**>(&pCallback);
    const bool cQueued = aSelf ? pVm->DispatchMethodCall(aSelf, cClass, cFunction, &arguments, ppCallback)
                               : pVm->DispatchStaticCall(cClass, cFunction, &arguments, ppCallback);
    if (pCallback && _InterlockedDecrement(reinterpret_cast<volatile long*>(&pCallback->refCount)) == 0)
        delete pCallback;
    return cQueued;
}
} // namespace

bool Queue(std::string_view aClass, std::string_view aFunction, std::span<const LocalValue> aArguments, OnReturn aOnReturn) noexcept
{
    return Dispatch(0, aClass, aFunction, aArguments, std::move(aOnReturn));
}

bool QueueCall(const LocalCall& acCall, OnReturn aOnReturn) noexcept
{
    const auto* pSelf = std::get_if<LocalForm>(&acCall.Self);
    if (!pSelf)
        return Queue(acCall.Class, acCall.Function, acCall.Arguments, std::move(aOnReturn));
    auto* pVm = GameVM::Get() ? GameVM::Get()->virtualMachine : nullptr;
    auto* pForm = TESForm::GetById(pSelf->FormId);
    if (!pVm || !pForm)
        return false;
    const uint64_t cHandle = pVm->GetObjectHandlePolicy()->GetHandleForObject(static_cast<uint32_t>(pForm->formType), pForm);
    // A method needs the form's script object: one with no script of its own (a plain NPC) is bound to its type's first.
    BSScript::Variable self;
    self.SetForm(pForm, pVm);
    return Dispatch(cHandle, acCall.Class, acCall.Function, acCall.Arguments, std::move(aOnReturn));
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

std::optional<LocalCall> ReadCall(BSScript::IFunction* apFunction, BSScript::StackFrame& aFrame) noexcept
{
    LocalCall call{.Class = apFunction->GetObjectTypeName().AsAscii(), .Function = apFunction->GetName().AsAscii()};
    if (!apFunction->GetIsStatic())
    {
        auto self = Read(aFrame.self);
        if (!self || !std::holds_alternative<LocalForm>(*self))
            return std::nullopt;
        call.Self = std::move(*self);
    }
    const uint32_t cPage = aFrame.GetPageForFrame();
    for (uint32_t i = 0; i < apFunction->GetParamCount(); ++i)
    {
        auto value = Read(*aFrame.GetStackFrameVariable(i, cPage));
        if (!value)
            return std::nullopt;
        call.Arguments.push_back(std::move(*value));
    }
    return call;
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
