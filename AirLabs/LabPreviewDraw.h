#pragma once
#include "imgui/imgui.h"

namespace airlabs
{
    // Orthographic, rotatable diagram. The exact same 3D segments feed the world overlay.
    inline void DrawLabPreviewDiagram(const LabPreviewData& d, bool balls, bool paths, bool goals,
        float& yaw, float& pitch, float width, float height)
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##spawnprevieworbit", ImVec2(width, height));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
        {
            yaw += ImGui::GetIO().MouseDelta.x * 0.5f;
            pitch = std::clamp(pitch + ImGui::GetIO().MouseDelta.y * 0.4f, -80.f, 80.f);
        }
        const float a = yaw * kDeg, p = pitch * kDeg;
        const Vector right = V(-std::sin(a), std::cos(a), 0.f);
        const Vector view = V(std::cos(p) * std::cos(a), std::cos(p) * std::sin(a), std::sin(p));
        const Vector up = Cross(view, right);
        const float scale = std::min(width, height) * 0.43f / d.scaleRadius;
        auto project = [&](const Vector& v) { return ImVec2(start.x + width * 0.5f + Dot(v, right) * scale,
            start.y + height * 0.5f - Dot(v, up) * scale); };
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(start, ImVec2(start.x + width, start.y + height), true);
        dl->AddRectFilled(start, ImVec2(start.x + width, start.y + height), IM_COL32(12, 16, 25, 255), 8.f);
        for (const auto& l : d.lines)
        {
            if ((l.layer == 0 && !balls) || (l.layer == 1 && !paths) || (l.layer == 2 && !goals)) continue;
            const ImU32 c = l.layer == 0 ? IM_COL32(65, 165, 255, l.lit ? 170 : 20) :
                l.layer == 1 ? IM_COL32(255, 185, 65, l.lit ? 240 : 55) : IM_COL32(190, 110, 255, 28);
            dl->AddLine(project(l.a), project(l.b), c, l.layer == 1 && l.lit ? 2.5f : 1.f);
        }
        if (goals)
            for (size_t i = 0; i < d.goals.size(); ++i)
            {
                const Vector& g = d.goals[i];
                const ImVec2 at = project(g);
                const float radius = i == 0 ? std::max(3.f, d.params.ringRadius * scale) : 6.f;
                dl->AddCircle(at, radius, IM_COL32(200, 125, 255, i == 0 ? 230 : 160), 32, i == 0 ? 2.f : 1.f);
                dl->AddCircleFilled(at, 3.f, IM_COL32(210, 145, 255, 255));
            }
        const ImVec2 origin = project(V(0.f, 0.f, 0.f)), nose = project(Scale(d.nose, d.scaleRadius * 0.18f));
        dl->AddCircleFilled(origin, 4.f, IM_COL32(240, 244, 255, 255));
        dl->AddLine(origin, nose, IM_COL32(240, 244, 255, 240), 2.f);
        dl->AddText(ImVec2(origin.x + 7.f, origin.y + 5.f), IM_COL32(225, 232, 245, 255), "YOU");
        if (d.spawnValid && (balls || paths))
        {
            const ImVec2 at = project(d.example);
            dl->AddCircleFilled(at, 5.f, IM_COL32(245, 247, 255, 255));
            dl->AddCircle(at, 8.f, IM_COL32(70, 170, 255, 255), 20, 2.f);
        }
        dl->PopClipRect();
    }
}
