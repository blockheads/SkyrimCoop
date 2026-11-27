#include <Memory.h>
#include <Misc/BSScript.h>

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
