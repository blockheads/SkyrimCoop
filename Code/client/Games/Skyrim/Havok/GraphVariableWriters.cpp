#include <TiltedOnlinePCH.h>

#include <Havok/GraphVariableWriters.h>

#include <Havok/BShkbAnimationGraph.h>
#include <Havok/hkbBehaviorGraph.h>
#include <Havok/hkbNode.h>

#include <cstring>

hkbNodeIterator::hkbNodeIterator(uint32_t aFlags, hkbNode* apRoot) noexcept
{
    using TCtor = hkbNodeIterator*(hkbNodeIterator*, uint32_t, hkbNode*);
    POINTER_SKYRIMSE(TCtor, s_ctor, 63865);
    s_ctor.Get()(this, aFlags, apRoot);
}

hkbNodeIterator::~hkbNodeIterator() noexcept
{
    // As the engine's own walk frees it (63948): the bucket array, then the node lists.
    using TFree = void(void*);
    POINTER_SKYRIMSE(TFree, s_freeBuckets, 68109);
    POINTER_SKYRIMSE(TFree, s_freeLists, 63895);
    s_freeBuckets.Get()(storage + 0x40);
    s_freeLists.Get()(this);
}

hkbNode* hkbNodeIterator::Next() noexcept
{
    using TNext = hkbNode*(hkbNodeIterator*);
    POINTER_SKYRIMSE(TNext, s_next, 63867);
    return s_next.Get()(this);
}

bool hkbVariableBindingSet::IsOutput(const Binding& acBinding, const hkbNode* apNode) noexcept
{
    // 59474 resolves one binding's member offsets, type and output flag into the binding and the set.
    using TResolve = void(hkbVariableBindingSet*, Binding*, const hkbNode*);
    POINTER_SKYRIMSE(TResolve, s_resolve, 59474);
    hkbVariableBindingSet set{};
    Binding binding = acBinding;
    binding.flags = 0;
    s_resolve.Get()(&set, &binding, apNode);
    return binding.offsetInObjectPlusOne > 0 && (binding.flags & Binding::kFlagOutput);
}

namespace GraphVariableWriters
{
namespace
{
bool IsClass(const hkbNode* apNode, const char* acpName) noexcept
{
    const hkClass* pClass = apNode->GetClassType();
    return pClass && pClass->name && std::strcmp(pClass->name, acpName) == 0;
}

const char* ClassName(const hkbNode* apNode) noexcept
{
    const hkClass* pClass = apNode->GetClassType();
    return pClass && pClass->name ? pClass->name : "?";
}

const char* VariableName(const hkbBehaviorGraphNode* apBehaviour, int32_t aIndex) noexcept
{
    if (!apBehaviour->data || !apBehaviour->data->stringData)
        return nullptr;
    const auto& cNames = apBehaviour->data->stringData->variableNames;
    return aIndex >= 0 && aIndex < cNames.size ? cNames.data[aIndex] : nullptr;
}

// "Variable = ..." assigns Variable.
TiltedPhoques::String AssignedName(const char* acpExpression) noexcept
{
    const char* p = acpExpression;
    while (*p == ' ')
        ++p;
    const char* cpStart = p;
    while (*p && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
        ++p;
    TiltedPhoques::String name(cpStart, p - cpStart);
    while (*p == ' ')
        ++p;
    return *p == '=' && p[1] != '=' ? name : TiltedPhoques::String{};
}

void CollectBehaviour(hkbBehaviorGraphNode* apBehaviour, TiltedPhoques::Vector<hkbBehaviorGraphNode*>& aSeen,
                      TiltedPhoques::Vector<Writer>& aOut) noexcept
{
    if (!apBehaviour || !apBehaviour->rootGenerator ||
        std::find(aSeen.begin(), aSeen.end(), apBehaviour) != aSeen.end())
        return;
    aSeen.push_back(apBehaviour);

    const TiltedPhoques::String cBehaviour = apBehaviour->name ? apBehaviour->name : "";
    auto add = [&](int32_t aIndex, const char* acpFallback, TiltedPhoques::String aNode) {
        const char* cpName = VariableName(apBehaviour, aIndex);
        aOut.push_back({cpName ? cpName : (acpFallback ? acpFallback : ""), cBehaviour, std::move(aNode), aIndex});
    };

    TiltedPhoques::Vector<hkbBehaviorGraphNode*> referenced;
    hkbNodeIterator nodes(hkbNodeIterator::kIgnoreReferencedBehaviour, apBehaviour->rootGenerator);
    for (hkbNode* pNode = nodes.Next(); pNode; pNode = nodes.Next())
    {
        const TiltedPhoques::String cNode =
            TiltedPhoques::String(ClassName(pNode)) + " " + (pNode->name ? pNode->name : "");
        if (const hkbVariableBindingSet* pSet = pNode->bindings)
        {
            for (int32_t i = 0; i < pSet->bindings.size; ++i)
            {
                const auto& cBinding = pSet->bindings.data[i];
                if (cBinding.bindingType == hkbVariableBindingSet::Binding::kTypeVariable &&
                    hkbVariableBindingSet::IsOutput(cBinding, pNode))
                    add(cBinding.variableIndex, nullptr,
                        cNode + "." + (cBinding.memberPath ? cBinding.memberPath : ""));
            }
        }

        if (IsClass(pNode, "hkbBehaviorReferenceGenerator"))
            referenced.push_back(static_cast<hkbBehaviorReferenceGenerator*>(pNode)->behavior);
        else if (IsClass(pNode, "hkbStateMachine"))
        {
            const int32_t cSync = static_cast<hkbStateMachineNode*>(pNode)->syncVariableIndex;
            if (cSync >= 0)
                add(cSync, nullptr, cNode + " syncVariable");
        }
        else if (IsClass(pNode, "hkbEvaluateExpressionModifier"))
        {
            const auto* pArray = static_cast<hkbEvaluateExpressionModifier*>(pNode)->expressions;
            for (int32_t i = 0; pArray && i < pArray->expressions.size; ++i)
            {
                const auto& cExpression = pArray->expressions.data[i];
                const char* cpText = cExpression.expression ? cExpression.expression : "";
                const TiltedPhoques::String cAssigned = AssignedName(cpText);
                if (cExpression.assignmentVariableIndex >= 0 || !cAssigned.empty())
                    add(cExpression.assignmentVariableIndex, cAssigned.c_str(), cNode + " '" + cpText + "'");
            }
        }
    }

    for (hkbBehaviorGraphNode* pReferenced : referenced)
        CollectBehaviour(pReferenced, aSeen, aOut);
}
} // namespace

TiltedPhoques::Vector<Writer> Collect(const BShkbAnimationGraph* apGraph) noexcept
{
    TiltedPhoques::Vector<Writer> writers;
    if (!apGraph || !apGraph->behaviorGraph)
        return writers;
    TiltedPhoques::Vector<hkbBehaviorGraphNode*> seen;
    CollectBehaviour(reinterpret_cast<hkbBehaviorGraphNode*>(apGraph->behaviorGraph), seen, writers);
    return writers;
}
} // namespace GraphVariableWriters
