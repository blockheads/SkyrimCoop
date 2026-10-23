#include <Misc/BSScript.h>
#include <Misc/GameVM.h>
#include <Actor.h>
#include <VersionDb.h>

uint64_t BSScript::Object::GetHandle()
{
    using TGetHandle = uint64_t(BSScript::Object*);
    POINTER_SKYRIMSE(TGetHandle, s_getHandle, 104247);
    return s_getHandle(this);
}

uint32_t BSScript::Object::DecRef() noexcept
{
    using TDecRef = uint32_t(BSScript::Object*);
    POINTER_SKYRIMSE(TDecRef, s_decRef, 104253);
    return s_decRef(this);
}

BSScript::Variable* BSScript::Object::GetVariable(const char* acpName) noexcept
{
    // The variables follow the object, the base script's first: a type's index is past its parents' counts.
    auto* pVariables = reinterpret_cast<Variable*>(this + 1);
    for (const ObjectTypeInfo* pType = this->pType; pType; pType = pType->pParent)
    {
        const ObjectTypeInfo::VariableInfo* pInfo = pType->data ? pType->GetVariables() : nullptr;
        for (uint32_t i = 0; pInfo && i < pType->variableCount; ++i)
        {
            if (!pInfo[i].name.Is(acpName))
                continue;
            uint32_t offset = 0;
            for (const ObjectTypeInfo* pParent = pType->pParent; pParent; pParent = pParent->pParent)
                offset += pParent->variableCount;
            return &pVariables[offset + i];
        }
    }
    return nullptr;
}

BSScript::Variable::Variable()
    : type(kEmpty)
{
    data.pObj = nullptr;
}

void BSScript::VirtualMachine::WaitOnLatent(uint32_t aStackId) noexcept
{
    using TWaitOnLatent = void(VirtualMachine*, uint32_t);
    POINTER_SKYRIMSE(TWaitOnLatent, s_waitOnLatent, 104851);
    s_waitOnLatent(this, aStackId);
}

BSScript::Variable::~Variable() noexcept
{
    Reset();
}

void BSScript::Variable::Reset() noexcept
{
    using TReset = void(BSScript::Variable*);

    POINTER_SKYRIMSE(TReset, s_reset, 104296);

    s_reset(this);
}

void BSScript::Variable::Clear() noexcept
{
    Reset();

    type = kEmpty;
}

void BSScript::Variable::SetActor(Actor* apActor, IVirtualMachine* apVm) noexcept
{
    // Game.GetPlayer's packer (55510) into a fresh value, then Variable's assignment (104297).
    using TPack = void(Variable*, Actor*, IVirtualMachine*);
    using TAssign = void(Variable*, const Variable*);
    POINTER_SKYRIMSE(TPack, s_pack, 55510);
    POINTER_SKYRIMSE(TAssign, s_assign, 104297);
    Variable packed;
    packed.type = kEmpty;
    packed.data.pObj = nullptr;
    s_pack(&packed, apActor, apVm);
    s_assign(this, &packed);
}

void BSScript::Variable::SetForm(TESForm* apForm, IVirtualMachine* apVm) noexcept
{
    if (Actor* pActor = Cast<Actor>(apForm))
        return SetActor(pActor, apVm);
    // ObjectBindPolicy::BindObject (104184): its BSTSmartPointer<Object>&.
    using TBindObject = void(ObjectBindPolicy*, Object**, uint64_t);
    POINTER_SKYRIMSE(TBindObject, s_bindObject, 104184);

    Clear();
    const uint32_t cType = apForm ? static_cast<uint32_t>(apForm->formType) : 0;
    ObjectTypeInfo* pClass = nullptr;
    if (!apForm || !apVm || !apVm->GetScriptObjectType2(cType, &pClass) || !pClass)
        return;
    IObjectHandlePolicy* pPolicy = apVm->GetObjectHandlePolicy();
    const uint64_t cHandle = pPolicy->GetHandleForObject(cType, apForm);
    Object* pObject = nullptr;
    if (!apVm->FindBoundObject(cHandle, pClass->name.AsAscii(), &pObject) && apVm->CreateObject2(pClass->name, &pObject) && pObject &&
        pPolicy->HandleIsType(cType, cHandle) && pPolicy->IsHandleObjectAvailable(cHandle))
        s_bindObject(apVm->GetObjectBindPolicy(), &pObject, cHandle);
    if (pObject)
    {
        // An object value's type is its class (Variable::SetObject); the reference found or made moves into it.
        type = static_cast<Type>(reinterpret_cast<uintptr_t>(pClass));
        data.pObj = pObject;
    }
    _InterlockedDecrement(reinterpret_cast<volatile long*>(pClass)); // the lookup's; the VM keeps its types
}

template <> void BSScript::Variable::Set(int32_t aValue) noexcept
{
    Reset();

    type = kInteger;
    data.i = aValue;
}

template <> void BSScript::Variable::Set(float aValue) noexcept
{
    Reset();

    type = kFloat;
    data.f = aValue;
}

template <> void BSScript::Variable::Set(bool aValue) noexcept
{
    Reset();

    type = kBoolean;
    data.b = aValue;
}

template <> void BSScript::Variable::Set(const char* acpValue) noexcept
{
    Reset();

    type = kString;
    auto pStr = (BSFixedString*)&data.s;
    pStr->Set(acpValue);
}

BSScript::Object* BSScript::Variable::GetObject() const noexcept
{
    if (type > Type::kMax)
    {
        bool isArray = type & 1;
        if (isArray)
            return nullptr;
    }
    else if (type != Type::kObject)
        return nullptr;

    return data.pObj;
}

template <class T> T* BSScript::Variable::ExtractComplexType() noexcept
{
    auto* pPolicy = GameVM::Get()->virtualMachine->GetObjectHandlePolicy();
    BSScript::Object* pBaseObject = GetObject();

    if (!pBaseObject || !pPolicy)
        return nullptr;

    uint64_t handle = pBaseObject->GetHandle();

    if (!pPolicy->HandleIsType((uint32_t)T::Type, handle) || !pPolicy->IsHandleObjectAvailable(handle))
        return nullptr;

    return pPolicy->GetObjectForHandle<T>(handle);
}

bool BSScript::IFunctionArguments::Statement::SetSize(uint32_t aCount) noexcept
{
    using TSetSize = bool(BSScript::IFunctionArguments::Statement*, uint32_t aCount);
    POINTER_SKYRIMSE(TSetSize, s_setSize, 53915);
    return s_setSize(this, aCount);
}

BSScript::IObjectHandlePolicy* BSScript::IObjectHandlePolicy::Get() noexcept
{
    POINTER_SKYRIMSE(BSScript::IObjectHandlePolicy*, s_policy, 414391);
    return *s_policy.Get();
}

BSFixedString& BSScript::NativeFunctionBase::GetName()
{
    return name;
}

BSFixedString& BSScript::NativeFunctionBase::GetObjectTypeName()
{
    return objectName;
}

BSFixedString& BSScript::NativeFunctionBase::GetStateName()
{
    return stateName;
}

BSScript::TypeInfo BSScript::NativeFunctionBase::GetReturnType()
{
    return {returnType};
}

uint32_t BSScript::NativeFunctionBase::GetParamCount()
{
    return parameters.capacity;
}

void BSScript::NativeFunctionBase::GetParam(uint32_t aIndex, BSFixedString& apNameOut, Variable::Type& aTypeOut)
{
    if (aIndex < parameters.size)
    {
        auto& elem = parameters.data[aIndex];
        apNameOut = elem.name;
        aTypeOut = elem.type;
    }
    else
    {
        apNameOut = "";
        aTypeOut = Variable::Type::kEmpty;
    }
}

uint32_t BSScript::NativeFunctionBase::GetStackFrameSize()
{
    return parameters.capacity;
}

bool BSScript::NativeFunctionBase::GetIsNative()
{
    return true;
}

bool BSScript::NativeFunctionBase::GetIsStatic()
{
    return isStatic;
}

bool BSScript::NativeFunctionBase::GetIsEmpty()
{
    return false;
}

auto BSScript::NativeFunctionBase::GetFunctionType() -> FunctionType
{
    return FunctionType::kNormal;
}

uint32_t BSScript::NativeFunctionBase::GetUserFlags()
{
    return flags;
}

BSFixedString& BSScript::NativeFunctionBase::GetDocString()
{
    return documentation;
}

void BSScript::NativeFunctionBase::InsertLocals(StackFrame*)
{
    return;
}

BSScript::CallResult BSScript::NativeFunctionBase::Call(Stack** appStack, void* apLogger, IVirtualMachine* apVm, bool aArg4)
{
    using TCall = CallResult(NativeFunctionBase*, Stack**, void*, IVirtualMachine*, bool);
    POINTER_SKYRIMSE(TCall, s_call, 104651);
    return s_call(this, appStack, apLogger, apVm, aArg4);
}

BSFixedString& BSScript::NativeFunctionBase::GetSourceFilename()
{
    static BSFixedString native("<native>");
    return native;
}

bool BSScript::NativeFunctionBase::TranslateIPToLineNumber(uint32_t, uint32_t& aLineNumberOut)
{
    aLineNumberOut = 0;
    return false;
}

bool BSScript::NativeFunctionBase::GetVarNameForStackIndex(std::uint32_t aIndex, BSFixedString& aNameOut)
{
    if (aIndex < parameters.capacity)
    {
        aNameOut = parameters.data[aIndex].name;
        return true;
    }
    else
    {
        aNameOut = "";
        return false;
    }
}

bool BSScript::NativeFunctionBase::CanBeCalledFromTasklets()
{
    return isCallableFromTask;
}

void BSScript::NativeFunctionBase::SetCallableFromTasklets(bool aCallable)
{
    isCallableFromTask = aCallable;
}

BSScript::DidLaunchSkyrimTogetherFunc::DidLaunchSkyrimTogetherFunc(const char* apFunctionName, const char* apClassName, FunctionType aFunction, Variable::Type aType)
    : NativeFunction(apFunctionName, apClassName, true, 0)
{
    pFunction = reinterpret_cast<void*>(aFunction);
    returnType = aType;
}

bool BSScript::DidLaunchSkyrimTogetherFunc::MarshallAndDispatch(Variable* apBaseVar, IVirtualMachine* apVm, uint32_t aStackID, Variable* apResult, StackFrame* apStackFrame)
{
    apResult->Set<bool>(true);
    return true;
}
