#include "FakeNativeVm.h"
#include <Games/Memory.h>
#include <Misc/BSScript.h>

#include <cassert>
#include <unordered_map>
#include <utility>

namespace
{
FakeNativeVm* s_pBinding = nullptr;
std::unordered_map<void*, FakeNativeVm*> s_allocations;
} // namespace

struct FakeNativeVm::Functions
{
    BSScript::IFunction* Launch = nullptr;
};

FakeNativeVm::FakeNativeVm() : m_functions(std::make_unique<Functions>())
{
    s_pBinding = this;
    m_functions->Launch = BSScript::MakeLaunchFunction();
    s_pBinding = nullptr;
}

FakeNativeVm::~FakeNativeVm()
{
    ReleaseTypes();
    std::erase_if(s_allocations, [this](const auto& acEntry) { return acEntry.second == this; });
}

void FakeNativeVm::ReleaseTypes()
{
    delete std::exchange(m_functions->Launch, nullptr);
}

bool FakeNativeVm::LaunchCheck() const
{
    auto* pFunction = m_functions->Launch;
    if (!pFunction || !pFunction->GetIsNative() || !pFunction->GetIsStatic() ||
        !pFunction->GetName().Is("DidLaunchSkyrimTogether"))
        return false;
    BSScript::Variable result;
    auto* pNative = static_cast<BSScript::NativeFunction*>(pFunction);
    return pNative->HasCallback() && pNative->MarshallAndDispatch(nullptr, nullptr, 0, &result, nullptr) &&
           result.data.b;
}

void* Memory::Allocate(size_t aSize) noexcept
{
    assert(s_pBinding);
    void* pMemory = ::operator new(aSize);
    s_allocations[pMemory] = s_pBinding;
    ++s_pBinding->Allocations;
    return pMemory;
}

void Memory::Free(void* apData) noexcept
{
    if (!apData)
        return;
    auto it = s_allocations.find(apData);
    assert(it != s_allocations.end());
    ++it->second->GameHeapFrees;
    s_allocations.erase(it);
    ::operator delete(apData);
}

// The engine ctor/dtor and string pool are seams; the function, factory and virtual delete are real.
BSScript::NativeFunction::NativeFunction(const char* apName, const char* apClass, bool aStatic, uint32_t aParams)
{
    refCount = 1;
    name.Set(apName);
    objectName.Set(apClass);
    stateName.Set("");
    returnType = Variable::kEmpty;
    parameters = {};
    parameters.size = parameters.capacity = aParams;
    isStatic = aStatic;
    isCallableFromTask = isLatent = false;
    flags = 0;
}

BSScript::NativeFunction::~NativeFunction()
{
    ++s_allocations.at(this)->Destructions;
}

BSScript::CallResult BSScript::NativeFunctionBase::Call(Stack**, void*, IVirtualMachine*, bool)
{
    return CallResult::kFailedAbort;
}

BSFixedString::BSFixedString() : data(nullptr)
{
}
BSFixedString::BSFixedString(const char* apValue) : data(apValue)
{
}
BSFixedString::~BSFixedString() = default;
void BSFixedString::Set(const char* apValue)
{
    data = apValue;
}
BSScript::Variable::Variable() : type(kEmpty), data{}
{
}
BSScript::Variable::~Variable() = default;
template <> void BSScript::Variable::Set(bool aValue) noexcept
{
    type = kBoolean;
    data.b = aValue;
}
