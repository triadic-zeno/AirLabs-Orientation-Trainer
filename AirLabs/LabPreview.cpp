#include "AirLabs.h"
#include "LabPreviewDraw.h"

using namespace airlabs;

void AirLabs::QueueLabPreview()
{
    if (!*labPreviewEnabled || *labMotion != kLabRespawn || labPreviewQueued.exchange(true)) return;
    const LabParams p = LabParamsNow();
    const unsigned seed = labPreviewSeed.load();
    const bool goals = *labPreviewGoals;
    gameWrapper->Execute([this, p, seed, goals](GameWrapper*) {
        Basis b = LabFacing(V(1.f, 0.f, 0.f));
        Vector hitbox = V(118.f, 84.2f, 36.16f);
        if (gameWrapper->IsInFreeplay())
        {
            auto server = gameWrapper->GetGameEventAsServer();
            if (!server.IsNull())
            {
                auto car = server.GetGameCar();
                if (!car.IsNull())
                {
                    const auto r = car.GetRotation();
                    b = BasisFromRotator(r.Pitch, r.Yaw, r.Roll);
                    Vector ext = car.GetLocalCollisionExtent();
                    if (ext.X > 20.f && ext.X < 400.f && ext.Y > 10.f && ext.Z > 5.f)
                    {
                        if (ext.X < 90.f) ext = Scale(ext, 2.f);
                        hitbox = ext;
                    }
                }
            }
        }
        // Tiny physics jitter must not keep restarting expensive target checks.
        if (labPreviewModel.data.spawnValid && AngleDeg(b.forward, labPreviewModel.data.nose) < 1.f)
            b = LabFacing(labPreviewModel.data.nose);
        if (labPreviewModel.Update(p, b, seed, goals, hitbox))
        {
            auto copy = std::make_shared<const LabPreviewData>(labPreviewModel.data);
            std::lock_guard<std::mutex> lock(labPreviewMutex);
            labPreviewSnapshot = std::move(copy);
        }
        labPreviewQueued = false;
    });
}

void AirLabs::DrawLabPreviewWindow()
{
    if (!*labPreviewEnabled || *labMotion != kLabRespawn) return;
    QueueLabPreview();
    ImGui::SetNextWindowSize(ImVec2(490.f, 460.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(std::max(10.f, ImGui::GetIO().DisplaySize.x - 510.f), 90.f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(360.f, 340.f), ImVec2(1200.f, 1000.f));
    bool open = true;
    if (ImGui::Begin("Spawn preview##airlabspreview", &open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextColored(ImVec4(0.26f, 0.65f, 1.f, 1.f), "Blue: ball spawns"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.f, 0.73f, 0.26f, 1.f), "Amber: paths");
        ImGui::TextColored(ImVec4(0.77f, 0.47f, 1.f, 1.f), "Purple: checked goals");
        ImGui::SameLine();
        if (ImGui::SmallButton("New example")) ++labPreviewSeed;
        std::shared_ptr<const LabPreviewData> data;
        { std::lock_guard<std::mutex> lock(labPreviewMutex); data = labPreviewSnapshot; }
        if (data)
        {
            DrawLabPreviewDiagram(*data, *labPreviewBalls, *labPreviewPaths, *labPreviewGoals,
                labPreviewYaw, labPreviewPitch, ImGui::GetContentRegionAvail().x, std::max(180.f, ImGui::GetContentRegionAvail().y - 62.f));
            ImGui::TextWrapped("%s", data->status.c_str());
        }
        else ImGui::TextUnformatted("Building preview...");
        ImGui::TextDisabled("Drag to orbit. Settings update live. Preview does not change your run.");
    }
    ImGui::End();
    if (!open) SetCvarI("airlabs_lab_preview", 0);
}

void AirLabs::DrawLabPreviewWorld(CanvasWrapper& canvas)
{
    if (*labMotion != kLabRespawn) return;
    std::shared_ptr<const LabPreviewData> data;
    { std::lock_guard<std::mutex> lock(labPreviewMutex); data = labPreviewSnapshot; }
    if (!data) return;
    auto cam = gameWrapper->GetCamera();
    auto server = gameWrapper->GetGameEventAsServer();
    if (cam.IsNull() || server.IsNull()) return;
    auto car = server.GetGameCar();
    if (car.IsNull()) return;
    const Vector origin = car.GetLocation(), camera = cam.GetLocation();
    const auto r = cam.GetRotation();
    const Basis view = BasisFromRotator(r.Pitch, r.Yaw, r.Roll);
    auto front = [&](const Vector& v) { return Dot(Sub(v, camera), view.forward) > 30.f; };
    for (const auto& l : data->lines)
    {
        if ((l.layer == 0 && !*labPreviewBalls) || (l.layer == 1 && !*labPreviewPaths) || (l.layer == 2 && !*labPreviewGoals)) continue;
        // The 3D overlay is deliberately faint; the orbit window shows the full sphere.
        if (l.layer == 0 && !l.lit) continue;
        const Vector a = Add(origin, l.a), b = Add(origin, l.b);
        if (!front(a) || !front(b)) continue;
        canvas.SetColor(l.layer == 0 ? LinearColor(65, 165, 255, 75) : l.layer == 1 ?
            LinearColor(255, 185, 65, l.lit ? 190 : 35) : LinearColor(190, 110, 255, 25));
        canvas.DrawLine(canvas.ProjectF(a), canvas.ProjectF(b), l.layer == 1 && l.lit ? 2.f : 1.f);
    }
    if (*labPreviewGoals)
    for (const Vector& g : data->goals)
    {
        const Vector at = Add(origin, g);
        if (!front(at)) continue;
        const Vector2F point = canvas.ProjectF(at);
        canvas.SetColor(LinearColor(200, 125, 255, 230));
        canvas.SetPosition(Vector2F{ point.X - 3.f, point.Y - 3.f });
        canvas.FillBox(Vector2F{ 6.f, 6.f });
    }
    const Vector example = Add(origin, data->example);
    if (data->spawnValid && (*labPreviewBalls || *labPreviewPaths) && front(example))
    {
        const Vector2F at = canvas.ProjectF(example);
        canvas.SetColor(LinearColor(235, 242, 255, 220));
        canvas.SetPosition(Vector2F{ at.X + 8.f, at.Y + 8.f });
        canvas.DrawString("PREVIEW EXAMPLE", 0.8f, 0.8f, true);
        canvas.DrawLine(Vector2F{ at.X - 5.f, at.Y }, Vector2F{ at.X + 5.f, at.Y }, 2.f);
        canvas.DrawLine(Vector2F{ at.X, at.Y - 5.f }, Vector2F{ at.X, at.Y + 5.f }, 2.f);
    }
}
