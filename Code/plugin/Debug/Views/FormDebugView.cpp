#include <imgui.h>
#include <inttypes.h>
#include <TES.h>

#include <PlayerCharacter.h>
#include <Forms/TESObjectCELL.h>
#include <Forms/TESWorldSpace.h>

#include <DebugService.h>
#include <RTTI.h>

void DebugService::DrawFormDebugView()
{
    // An id, looked up each frame: the form can be freed while the view is open.
    static uint32_t s_lookedUpId = 0;

    ImGui::Begin("Form");

    ImGui::InputScalar("Form ID", ImGuiDataType_U32, &m_formId, 0, 0, "%" PRIx32, ImGuiInputTextFlags_CharsHexadecimal);

    if (ImGui::Button("Look up"))
    {
        s_lookedUpId = m_formId;
    }

    TESForm* pFetchForm = s_lookedUpId ? TESForm::GetById(s_lookedUpId) : nullptr;
    Actor* pRefr = pFetchForm ? Cast<Actor>(pFetchForm) : nullptr;

    if (pFetchForm)
    {
        ImGui::InputScalar("Memory address", ImGuiDataType_U64, (void*)&pFetchForm, 0, 0, "%" PRIx64, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_ReadOnly);
    }

    if (pRefr)
    {
        if (auto* pParentCell = pRefr->GetParentCell())
        {
            const uint32_t cellId = pParentCell->formID;
            ImGui::InputScalar("GetParentCell", ImGuiDataType_U32, (void*)&cellId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
        }

        if (auto* pParentCell = pRefr->parentCell)
        {
            const uint32_t cellId = pParentCell->formID;
            ImGui::InputScalar("parentCell", ImGuiDataType_U32, (void*)&cellId, nullptr, nullptr, "%" PRIx32, ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_CharsHexadecimal);
        }
    }

    ImGui::End();
}
