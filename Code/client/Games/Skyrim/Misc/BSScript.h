#pragma once

#include <Games/Skyrim/BSCore/BSSpinLock.h>
#include <Games/Skyrim/BSCore/BSTHashMap.h>
#include <Misc/BSFixedString.h>
#include <tuple>

struct Actor;

struct BSScript
{
    struct IVirtualMachine;
    struct Variable;

    // A script's compiled type (CommonLibSSE-NG ObjectTypeInfo.h).
    struct ObjectTypeInfo
    {
        struct VariableInfo
        {
            BSFixedString name; // 00
            uintptr_t type;     // 08
        };

        // The type's own variables: after its user flags (8 bytes each) in data.
        const VariableInfo* GetVariables() const noexcept
        {
            return reinterpret_cast<const VariableInfo*>(static_cast<const uint8_t*>(data) + userFlagCount * 8);
        }

        uint8_t unk00[0x8];
        BSFixedString name;          // 08
        ObjectTypeInfo* pParent;     // 10 BSTSmartPointer
        BSFixedString docString;     // 18
        uint32_t linkedValid : 2;    // 20
        uint32_t userFlagCount : 6;
        uint32_t variableCount : 10;
        uint32_t variableUserFlagCount : 6;
        uint32_t pad20 : 8;
        uint8_t unk24[0x30 - 0x24];
        void* data; // 30
    };

    struct Object
    {
        uint64_t GetHandle();
        uint32_t DecRef() noexcept;
        // A script variable by name, a parent script's included (CommonLibSSE-NG Object::GetVariable).
        Variable* GetVariable(const char* acpName) noexcept;

        // CommonLibSSE-NG Object.h
        uint8_t unk00[0x8];
        ObjectTypeInfo* pType;      // 08 BSTSmartPointer
        BSFixedString currentState; // 10
        uint8_t unk18[0x30 - 0x18];
    };

    // The form a handle names (a handle's low 32 bits; CommonLibSSE-NG IObjectHandlePolicy).
    static uint32_t FormIdOfHandle(uint64_t aHandle) noexcept { return static_cast<uint32_t>(aHandle); }

    struct Variable
    {
        Variable();
        ~Variable() noexcept;

        void Reset() noexcept;
        void Clear() noexcept;
        Object* GetObject() const noexcept;
        // The actor as a script value, as Game.GetPlayer returns it.
        void SetActor(Actor* apActor, IVirtualMachine* apVm) noexcept;

        template <class T> void Set(T aValue) noexcept
        {
            // static_assert(false);
        }

        template <class T> T* ExtractComplexType() noexcept;

        enum Type : uint64_t
        {
            kEmpty,
            kObject,
            kString,
            kInteger,
            kFloat,
            kBoolean,

            kMax = 16
        };

        union Data
        {
            int32_t i;
            const char* s;
            float f;
            bool b;
            Object* pObj;
        };

        Type type;
        Data data;
    };

    // A queued call's arguments (CommonLibSSE-NG BSTArray.h: BSScrapArray<Variable>).
    struct Arguments
    {
        void* pAllocator;
        Variable* pData; // 08
        uint32_t capacity;
        uint32_t pad14;
        uint32_t size; // 18
    };

    // https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/blob/master/include/RE/I/IFunction.h

    enum class FunctionType
    {
        kNormal = 0,
        kGetter = 1,
        kSetter = 2
    };

    enum class CallResult
    {
        kCompleted = 0,
        kSetupForVM = 1,
        kInProgress = 2,
        kFailedRetry = 3,
        kFailedAbort = 4
    };

    struct StackFrame;
    struct Stack;

    struct IFunction : BSIntrusiveRefCounted
    {
        virtual ~IFunction() = default;
        virtual BSFixedString& GetName() = 0;
        virtual BSFixedString& GetObjectTypeName() = 0;
        virtual BSFixedString& GetStateName() = 0;
        virtual Variable::Type& GetReturnType() = 0;
        virtual uint32_t GetParamCount() = 0;
        virtual void GetParam(uint32_t aIndex, BSFixedString& apNameOut, Variable::Type& apTypeOut) = 0;
        virtual uint32_t GetStackFrameSize() = 0;
        virtual bool GetIsNative() = 0;
        virtual bool GetIsStatic() = 0;
        virtual bool GetIsEmpty() = 0;
        virtual FunctionType GetFunctionType() = 0;
        virtual uint32_t GetUserFlags() = 0;
        virtual BSFixedString& GetDocString() = 0;
        virtual void InsertLocals(StackFrame* apFrame) = 0;
        // appStack: BSTSmartPointer<Stack>&. TODO: apVm is actually GameVM
        virtual CallResult Call(Stack** appStack, void* apLogger, IVirtualMachine* apVm, bool aArg4) = 0;
        virtual BSFixedString& GetSourceFilename() = 0;
        virtual bool TranslateIPToLineNumber(uint32_t aIndexPtr, uint32_t& aLineNumberOut) = 0;
        virtual bool GetVarNameForStackIndex(uint32_t aIndex, BSFixedString& aNameOut) = 0;
        virtual bool CanBeCalledFromTasklets() = 0;
        virtual void SetCallableFromTasklets(bool aCallable) = 0;
    };

    struct IFunctionArguments
    {
        struct Statement
        {
            void SetSize(uint32_t aCount) noexcept;

            uint8_t pad0[8];
            Variable* vars;
        };

        virtual ~IFunctionArguments() {}

        virtual void Prepare(Statement* apUnk) noexcept = 0;
    };

    struct Stack
    {
        uint32_t GetPageForFrame(StackFrame* apFrame)
        {
            TP_THIS_FUNCTION(TGetPageForFrame, uint32_t, Stack, StackFrame*);
            POINTER_SKYRIMSE(TGetPageForFrame, getPageForFrame, 104483);
            return TiltedPhoques::ThisCall(getPageForFrame, this, apFrame);
        }

        Variable* GetStackFrameVariable(StackFrame* apFrame, uint32_t aIndex, uint32_t aPageHint)
        {
            TP_THIS_FUNCTION(TGetStackFrameVariable, Variable*, Stack, StackFrame*, uint32_t, uint32_t);
            POINTER_SKYRIMSE(TGetStackFrameVariable, getStackFrameVariable, 104484);
            return TiltedPhoques::ThisCall(getStackFrameVariable, this, apFrame, aIndex, aPageHint);
        }

        // CommonLibSSE-NG Stack.h
        uint8_t unk00[0x60];
        StackFrame* pTop;    // 60
        uint8_t unk68[0x18];
        uint32_t stackId;    // 80
    };

    struct StackFrame
    {
        uint32_t GetPageForFrame() { return pParent->GetPageForFrame(this); }

        Variable* GetStackFrameVariable(uint32_t aIndex, uint32_t aPageHint) { return pParent->GetStackFrameVariable(this, aIndex, aPageHint); }

        // CommonLibSSE-NG StackFrame.h
        Stack* pParent;              // 00
        StackFrame* pPrevious;       // 08
        IFunction* pFunction;        // 10 BSTSmartPointer
        void* pObjectType;           // 18
        uint32_t instructionPointer; // 20
        uint32_t pad24;
        Variable self;               // 28
    };

    // Internal::CodeTasklet (CommonLibSSE-NG CodeTasklet.h): runs a stack's compiled code.
    struct CodeTasklet
    {
        uint8_t unk00[0x10];
        Stack* pStack; // 10
    };

    struct IObjectHandlePolicy
    {
        static IObjectHandlePolicy* Get() noexcept;

        virtual ~IObjectHandlePolicy();
        virtual bool HandleIsType(uint32_t aType, uint64_t aHandle);
        virtual bool IsHandleObjectAvailable(uint64_t aHandle);
        virtual uint64_t EmptyHandle();
        virtual uint64_t GetHandleForObject(uint32_t aType, const void* apObject);
        virtual void sub_05();
        virtual void sub_06();
        virtual void sub_07();
        virtual void* GetObjectForHandle(uint32_t aType, uint64_t aHandle);

        template <class T> T* GetObjectForHandle(uint64_t aHandle) { return (T*)(BSScript::IObjectHandlePolicy::Get()->GetObjectForHandle((uint32_t)T::Type, aHandle)); }
    };

    struct IVirtualMachine
    {
        virtual ~IVirtualMachine() = 0;

        virtual void sub_01();
        virtual void sub_02();
        virtual void sub_03();
        virtual void sub_04();
        virtual void sub_05();
        virtual void sub_06();
        virtual void sub_07();
        virtual void sub_08();
        virtual void GetScriptObjectType1(BSFixedString* apClassName, void** apOutTypeInfoPtr);
        virtual void sub_0A();
        virtual void sub_0B();
        virtual void sub_0C();
        virtual void sub_0D();
        virtual void sub_0E();
        virtual void sub_0F();
        virtual void sub_10();
        virtual void sub_11();
        virtual void sub_12();
        virtual void sub_13();
        virtual void sub_14();
        virtual void sub_15();
        virtual void sub_16();
        virtual void sub_17();
        virtual void BindNativeMethod(IFunction* apFunction);
        virtual void sub_19();
        virtual void sub_1A();
        virtual void sub_1B();
        // appResult: BSTSmartPointer<Object>&, so a found object comes with a reference to release.
        virtual bool FindBoundObject(uint64_t aHandle, const char* acpClassName, Object** appResult) const;
        virtual void sub_1D();
        virtual void sub_1E();
        virtual void sub_1F();
        virtual void sub_20();
        virtual void sub_21();
        virtual void sub_22();
        virtual void sub_23();
        virtual void SendEvent(uint64_t aId, const BSFixedString& acEventName, IFunctionArguments* apArgs) const noexcept;
        virtual void sub_25();
        virtual void sub_26();
        virtual void sub_27();
        virtual void sub_28();
        virtual void sub_29();
        virtual void sub_2A();
        virtual void sub_2B();
        virtual void sub_2C();
        virtual IObjectHandlePolicy* GetObjectHandlePolicy();
    };

    // Internal::VirtualMachine, the IVirtualMachine behind GameVM (CommonLibSSE-NG VirtualMachine.h): its running stacks.
    struct VirtualMachine
    {
        uint8_t unk0[0x9318];
        BSSpinLock runningStacksLock;                            // 9318
        creation::BSTHashMap<uint32_t, Stack*> allRunningStacks; // 9320, BSTSmartPointer values
    };

    struct NativeFunctionBase : IFunction
    {
        BSFixedString& GetName() override;
        BSFixedString& GetObjectTypeName() override;
        BSFixedString& GetStateName() override;
        Variable::Type& GetReturnType() override;
        std::uint32_t GetParamCount() override;
        void GetParam(uint32_t aIndex, BSFixedString& apNameOut, Variable::Type& a_typeOut) override;
        uint32_t GetStackFrameSize() override;
        bool GetIsNative() override;
        bool GetIsStatic() override;
        bool GetIsEmpty() override;
        FunctionType GetFunctionType() override;
        std::uint32_t GetUserFlags() override;
        BSFixedString& GetDocString() override;
        void InsertLocals(StackFrame* apFrame) override;
        CallResult Call(Stack** appStack, void* apLogger, IVirtualMachine* apVm, bool aArg4) override;
        BSFixedString& GetSourceFilename() override;
        bool TranslateIPToLineNumber(uint32_t aTaskletExecutionOffset, uint32_t& aLineNumberOut) override;
        bool GetVarNameForStackIndex(uint32_t aIndex, BSFixedString& aNameOut) override;
        bool CanBeCalledFromTasklets() override;
        void SetCallableFromTasklets(bool aCallable) override;

        virtual bool HasCallback() { return false; }
        virtual bool MarshallAndDispatch(Variable* apBaseVar, IVirtualMachine* apVm, uint32_t aStackID, Variable* apResult, StackFrame* apStackFrame) { return false; }

        struct Parameters
        {
            struct Entry
            {
                const char* name;
                union
                {
                    Variable::Type type;
                    void* pType;
                };
            };

            Entry* data;
            uint16_t size;
            uint16_t capacity;
        };

        BSFixedString name;
        BSFixedString objectName;
        BSFixedString stateName;
        Variable::Type returnType;
        Parameters parameters;
        bool isStatic;
        bool isCallableFromTask;
        bool isLatent;
        uint8_t pad43;
        uint32_t flags;
        BSFixedString documentation;
    };
    static_assert(sizeof(NativeFunctionBase) == 0x50);

    struct NativeFunction : NativeFunctionBase
    {
        virtual ~NativeFunction()
        {
            TP_THIS_FUNCTION(TNativeFunctionDtor, void, NativeFunction);
            // TODO: not sure about this address
            POINTER_SKYRIMSE(TNativeFunctionDtor, dtor, 104655);
            TiltedPhoques::ThisCall(dtor, this);
        }

        NativeFunction(const char* apFunctionName, const char* apClassName, bool aIsStatic, uint32_t aParameterCount)
        {
            TP_THIS_FUNCTION(TNativeFunctionCtor, void, NativeFunction, const char*, const char*, bool, uint32_t);
            POINTER_SKYRIMSE(TNativeFunctionCtor, ctor, 104653);
            TiltedPhoques::ThisCall(ctor, this, apFunctionName, apClassName, aIsStatic, aParameterCount);
        }

        bool HasCallback() override { return pFunction != nullptr; }

        void* pFunction;
    };

    // TODO: template stuff for a cleaner implementation
    // https://github.com/Ryan-rsm-McKenzie/CommonLibSSE/blob/master/include/RE/N/NativeFunction.h

    struct DidLaunchSkyrimTogetherFunc : NativeFunction
    {
        using FunctionType = bool();

        DidLaunchSkyrimTogetherFunc(const char* apFunctionName, const char* apClassName, FunctionType aFunction, Variable::Type aType);

        bool MarshallAndDispatch(Variable* apBaseVar, IVirtualMachine* apVm, uint32_t aStackID, Variable* apResult, StackFrame* apStackFrame) override;
    };

    // Bad PoC code
};

template <> void BSScript::Variable::Set(int32_t aValue) noexcept;
template <> void BSScript::Variable::Set(float aValue) noexcept;
template <> void BSScript::Variable::Set(bool aValue) noexcept;
template <> void BSScript::Variable::Set(const char* acpValue) noexcept;

static_assert(sizeof(BSScript::Variable) == 0x10);
static_assert(offsetof(BSScript::Arguments, pData) == 0x8 && offsetof(BSScript::Arguments, size) == 0x18);
static_assert(offsetof(BSScript::Object, currentState) == 0x10 && offsetof(BSScript::Object, pType) == 0x8 && sizeof(BSScript::Object) == 0x30);
static_assert(offsetof(BSScript::ObjectTypeInfo, pParent) == 0x10 && offsetof(BSScript::ObjectTypeInfo, data) == 0x30 &&
              sizeof(BSScript::ObjectTypeInfo) == 0x38 && sizeof(BSScript::ObjectTypeInfo::VariableInfo) == 0x10);
static_assert(offsetof(BSScript::Stack, pTop) == 0x60 && offsetof(BSScript::Stack, stackId) == 0x80);
static_assert(offsetof(BSScript::StackFrame, pPrevious) == 0x8 && offsetof(BSScript::StackFrame, pFunction) == 0x10 &&
              offsetof(BSScript::StackFrame, self) == 0x28);
static_assert(offsetof(BSScript::CodeTasklet, pStack) == 0x10);
static_assert(offsetof(BSScript::VirtualMachine, runningStacksLock) == 0x9318 && offsetof(BSScript::VirtualMachine, allRunningStacks) == 0x9320 &&
              sizeof(creation::BSTHashMap<uint32_t, BSScript::Stack*>) == 0x30);
