#include <Services/DebugService.h>

#include <Forms/ActorValueInfo.h>

#include <imgui.h>
#include <RTTI.h>
#include <Utils.h>

void DebugService::DrawActorValuesView()
{
    Actor* pActor = Cast<Actor>(TESForm::GetById(m_formId));
    if (!pActor)
        return;

    ImGui::Begin("Actor values");

    // for (int i = 0; i < ActorValueInfo::kActorValueCount; i++)
    {
        ActorValueOwner& actorValueOwner = pActor->actorValueOwner;
        float health[3]{EngineVirtual(actorValueOwner).GetValue(24), EngineVirtual(actorValueOwner).GetBaseValue(24), EngineVirtual(actorValueOwner).GetPermanentValue(24)};
        ImGui::InputFloat3("Health (val/base/perm)", health, "%.3f", ImGuiInputTextFlags_ReadOnly);
    }

    ImGui::End();
}
