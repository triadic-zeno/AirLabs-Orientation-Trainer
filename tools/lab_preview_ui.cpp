// Render the actual ImGui preview diagram off-game. Dumps its mesh and font atlas for visual QA.
#include <cstdio>
#include <string>
struct Vector { float X, Y, Z; };
#include "TrainerCore.h"
#include "BoostLab.h"
#include "LabWorld.h"
#include "LabPreview.h"
#include "LabPreviewDraw.h"

using namespace airlabs;
int main(int argc, char** argv)
{
    const std::string prefix = argc > 1 ? argv[1] : "build/qa/preview";
    LabParams p;
    p.spawnMin = 1500.f; p.spawnMax = 2000.f; p.bubble = 2060.f; p.floor = 300.f; p.steep = 45.f;
    if (argc > 2) { p.spawnCone = 35.f; p.heightMin = 0.f; p.heightMax = 35.f; }
    LabPreview preview;
    for (int i = 0; i < 20; ++i) preview.Update(p, LabFacing(V(1.f, 0.f, 0.f)), 1701u, true);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.DisplaySize = ImVec2(840, 640); io.DeltaTime = 1.f / 60.f; io.IniFilename = nullptr;
    unsigned char* pixels; int tw, th;
    io.Fonts->GetTexDataAsAlpha8(&pixels, &tw, &th);
    FILE* atlas = std::fopen((prefix + ".atlas").c_str(), "wb");
    std::fwrite(pixels, 1, tw * th, atlas); std::fclose(atlas);
    float yaw = -140.f, pitch = 25.f;
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(840, 640));
        ImGui::Begin("Spawn preview", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        ImGui::TextColored(ImVec4(.26f, .65f, 1.f, 1.f), "BLUE: BALL SPAWNS"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.f, .73f, .26f, 1.f), "AMBER: BALL PATHS"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(.77f, .47f, 1.f, 1.f), "PURPLE: CHECKED GOALS");
        DrawLabPreviewDiagram(preview.data, true, true, true, yaw, pitch, 820, 540);
        ImGui::TextWrapped("%s", preview.data.status.c_str());
        ImGui::Text("Cone %.0f deg, heights %.0f to %.0f, goal %.0f uu", p.spawnCone, p.heightMin, p.heightMax, p.goalDistance);
        ImGui::End(); ImGui::Render();
    }
    FILE* out = std::fopen((prefix + ".json").c_str(), "wb");
    auto* draw = ImGui::GetDrawData();
    std::fprintf(out, "{\"width\":840,\"height\":640,\"tw\":%d,\"th\":%d,\"lists\":[", tw, th);
    for (int li = 0; li < draw->CmdListsCount; ++li)
    {
        const auto* dl = draw->CmdLists[li];
        if (li) std::fprintf(out, ",");
        std::fprintf(out, "{\"v\":[");
        for (int i = 0; i < dl->VtxBuffer.Size; ++i)
        {
            const auto& v = dl->VtxBuffer[i];
            std::fprintf(out, "%s[%.3f,%.3f,%.6f,%.6f,%u]", i ? "," : "", v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col);
        }
        std::fprintf(out, "],\"i\":[");
        for (int i = 0; i < dl->IdxBuffer.Size; ++i) std::fprintf(out, "%s%u", i ? "," : "", dl->IdxBuffer[i]);
        std::fprintf(out, "],\"commands\":[");
        for (int i = 0; i < dl->CmdBuffer.Size; ++i)
        {
            const auto& c = dl->CmdBuffer[i];
            std::fprintf(out, "%s[%u,%u,%u,%.2f,%.2f,%.2f,%.2f]", i ? "," : "", c.IdxOffset, c.ElemCount, c.VtxOffset,
                c.ClipRect.x, c.ClipRect.y, c.ClipRect.z, c.ClipRect.w);
        }
        std::fprintf(out, "]}");
    }
    std::fprintf(out, "]}"); std::fclose(out);
    std::printf("%s: %zu launch examples, %zu checked goals\n", prefix.c_str(), preview.data.velocities.size(), preview.data.goals.size());
    ImGui::DestroyContext();
}
