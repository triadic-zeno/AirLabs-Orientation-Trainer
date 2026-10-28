#include "AirLabs.h"
#include "imgui/imgui.h"
#include "bakkesmod/wrappers/http/HttpWrapper.h"
#include "bakkesmod/wrappers/GameObject/CarComponent/VehicleSimWrapper.h"
#include "bakkesmod/wrappers/GameObject/CarComponent/WheelWrapper.h"
#include "bakkesmod/wrappers/arraywrapper.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <sstream>

using namespace airlabs;

BAKKESMOD_PLUGIN(AirLabs, "AirLabs", "2.8.2", PLUGINTYPE_FREEPLAY)

// Set by the trainer window while it draws, so shared section headings use its font.
ImFont* g_otHeadingFont = nullptr;

namespace
{
    constexpr const char* kVersion = "2.8.2";
    // The public leaderboard (a Cloudflare Worker, see server/ in the source).
    constexpr const char* kLeaderboardUrl = "https://leaderboard.airlabs.workers.dev";
    const char* kTickEvent = "Function TAGame.Car_TA.SetVehicleInput";
    const char* kEndEvent = "Function TAGame.GameEvent_Soccar_TA.Destroyed";
    // Space map: which way runs start (as a goal side, -1 = towards -Y). The galactic centre
    // sits that way in the map's star sky.
    constexpr int kSpaceFacing = -1;


    // Arena interior (uu), used as a safety limit on how far the ball can be placed.
    constexpr float kHalfWidth = 4096.f;
    constexpr float kHalfLength = 5120.f;
    constexpr float kCeiling = 2044.f;
    constexpr float kBallRadius = 92.75f;
    constexpr float kMargin = kBallRadius + 5.f;

    std::string Fmt(const char* fmt, double a)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), fmt, a);
        return buf;
    }
    std::string FmtInt(long long v)
    {
        bool neg = v < 0;
        unsigned long long u = neg ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
        std::string digits = std::to_string(u), out;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            if (i && (digits.size() - i) % 3 == 0) out += ',';
            out += digits[i];
        }
        return neg ? "-" + out : out;
    }
    std::string FmtSigned(long long v) { return (v >= 0 ? "+" : "") + FmtInt(v); }
    std::string FmtClock(double s)
    {
        int t = std::max(0, static_cast<int>(std::ceil(s)));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d:%02d", t / 60, t % 60);
        return buf;
    }
    std::string OrDash(double v, const char* fmt) { return v >= 0.0 ? Fmt(fmt, v) : std::string("--"); }
    std::string NowStamp()
    {
        std::time_t t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        return buf;
    }
    double WallSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    int ToI(const std::string& s) { return static_cast<int>(std::strtol(s.c_str(), nullptr, 10)); }

    // Every setting a saved preset stores (the Custom values).
    const char* const kCustomCvars[] = {
        "airlabs_hold_ms", "airlabs_min_angle", "airlabs_max_angle", "airlabs_timeout", "airlabs_next_delay", "airlabs_aim", "airlabs_dynamic",
        "airlabs_track_speed", "airlabs_track_turn", "airlabs_max_turn", "airlabs_dyn_min", "airlabs_dyn_max", "airlabs_hybrid_track",
        "airlabs_run_length", "airlabs_height_min", "airlabs_height_max", "airlabs_distance", "airlabs_dirs", "airlabs_weak_spot",
        "airlabs_settle", "airlabs_settle_rate", "airlabs_block_jump", "airlabs_chase_min", "airlabs_chase_max",
    };

    // A preset name that's safe to store: no separators or line breaks, trimmed, 32 characters max.
    std::string CleanPresetName(std::string n)
    {
        for (char& c : n)
            if (c == '|' || c == ';' || c == '=' || c == '\n' || c == '\r' || c == ',') c = ' ';
        size_t a = n.find_first_not_of(' '), b = n.find_last_not_of(' ');
        n = a == std::string::npos ? std::string() : n.substr(a, b - a + 1);
        return n.size() > 32 ? n.substr(0, 32) : n;
    }

    // True for a preset run saved before 1.6.0, when ranges arrived (and before that the target,
    // timing and distances changed), so its score can't be compared with any range's records.
    bool OldPresetRules(const std::string& version)
    {
        int major = 0, minor = 0;
        if (std::sscanf(version.c_str(), "%d.%d", &major, &minor) != 2) return true;
        return major < 1 || (major == 1 && minor < 6);
    }
    float ToF(const std::string& s) { return std::strtof(s.c_str(), nullptr); }

    // True if a saved version string is older than major.minor.
    bool VersionBefore(const std::string& version, int wantMajor, int wantMinor)
    {
        int major = 0, minor = 0;
        if (std::sscanf(version.c_str(), "%d.%d", &major, &minor) != 2) return true;
        return major < wantMajor || (major == wantMajor && minor < wantMinor);
    }

    const LinearColor kColTitle(120.f, 200.f, 255.f, 255.f);
    const LinearColor kColText(235.f, 240.f, 245.f, 255.f);
    const LinearColor kColDim(170.f, 178.f, 190.f, 230.f);
    const LinearColor kColGood(80.f, 255.f, 120.f, 255.f);
    const LinearColor kColWarn(255.f, 210.f, 60.f, 255.f);
    const LinearColor kColBad(255.f, 90.f, 70.f, 255.f);
    const LinearColor kColPanel(10.f, 14.f, 22.f, 175.f);
    const LinearColor kColGold(255.f, 200.f, 60.f, 255.f);

    struct HudLine { std::string text; LinearColor color; float scale; };

    int LinesHeight(const std::vector<HudLine>& lines)
    {
        int h = 0;
        for (const auto& l : lines) h += static_cast<int>(17.f * l.scale) + 5;
        return h;
    }
    int DrawLines(CanvasWrapper& canvas, int x, int y, const std::vector<HudLine>& lines)
    {
        for (const auto& l : lines)
        {
            canvas.SetColor(l.color);
            canvas.SetPosition(Vector2{ x, y });
            canvas.DrawString(l.text, l.scale, l.scale, true);
            y += static_cast<int>(17.f * l.scale) + 5;
        }
        return y;
    }
    void FillRect(CanvasWrapper& c, int x, int y, int w, int h, const LinearColor& col)
    {
        c.SetColor(col);
        c.SetPosition(Vector2{ x, y });
        c.FillBox(Vector2{ w, h });
    }
    // The big red prompt at the top centre of the screen ("POINT AT THE BALL TO START").
    void DrawPrompt(CanvasWrapper& canvas, const Vector2& screen, const std::string& main, const std::string& sub)
    {
        const LinearColor red(255.f, 45.f, 45.f, 255.f);
        auto centred = [&](const std::string& t, float scale, int y) {
            Vector2F sz = canvas.GetStringSize(t, scale, scale);
            canvas.SetColor(red);
            canvas.SetPosition(Vector2{ static_cast<int>((screen.X - sz.X) * 0.5f), y });
            canvas.DrawString(t, scale, scale, true);
            return static_cast<int>(sz.Y);
        };
        int py = static_cast<int>(screen.Y * 0.06f);
        py += centred(main, 3.f, py) + 6;
        if (!sub.empty()) centred(sub, 2.2f, py);
    }
    void Bar(CanvasWrapper& c, int x, int y, int w, float frac, const LinearColor& col)
    {
        FillRect(c, x, y, w, 8, LinearColor(255.f, 255.f, 255.f, 60.f));
        frac = std::clamp(frac, 0.f, 1.f);
        if (frac > 0.f) FillRect(c, x, y, static_cast<int>(w * frac), 8, col);
    }

    // ---- settings page helpers ----
    const ImVec4 kUiGood(0.35f, 0.95f, 0.50f, 1.f);
    const ImVec4 kUiWarn(1.f, 0.82f, 0.25f, 1.f);
    const ImVec4 kUiHead(0.55f, 0.78f, 1.f, 1.f);
    const ImVec4 kUiSub(0.85f, 0.88f, 0.92f, 1.f);
    const ImVec4 kUiDesc(0.62f, 0.66f, 0.72f, 1.f);

    void Heading(const char* text)
    {
        ImGui::Spacing();
        if (g_otHeadingFont)
        {
            ImGui::PushFont(g_otHeadingFont);
            ImGui::TextColored(ImVec4(0.91f, 0.93f, 0.96f, 1.f), "%s", text);
            ImGui::PopFont();
        }
        else ImGui::TextColored(kUiHead, "%s", text);
        ImGui::Separator();
    }
    void SubHeading(const char* text)
    {
        ImGui::Spacing();
        ImGui::TextColored(kUiSub, "%s", text);
    }
    // A plain-language explanation under a setting.
    void Desc(const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, kUiDesc);
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 560.f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    // A button that stays highlighted while it's the current choice.
    // The run's flags, minus the ones that aren't reasons (No air roll is a mode, not a problem).
    std::string WhyNotCounted(std::string f)
    {
        for (const char* tag : { "noroll " })
        {
            size_t at;
            while ((at = f.find(tag)) != std::string::npos) f.erase(at, std::strlen(tag));
        }
        return f;
    }

    bool Choice(const char* label, bool selected, const ImVec2& size)
    {
        const ImVec4 accent(0.16f, 0.47f, 0.86f, 1.f);
        if (selected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        }
        bool pressed = ImGui::Button(label, size);
        if (selected) ImGui::PopStyleColor(2);
        return pressed;
    }
    // Keep the next item on this line if it fits, otherwise wrap.
    void SameLineIfFits(float nextWidth)
    {
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        float x2 = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth;
        if (x2 < right) ImGui::SameLine();
    }

    // Short / Medium / Long buttons. Returns true if the choice changed.
    bool RangeButtons(int& range, const char* idSuffix)
    {
        bool changed = false;
        ImGui::Text("Range:");
        for (int r = 0; r < kRangeCount; ++r)
        {
            ImGui::SameLine();
            std::string label = std::string(RangeName(r)) + Fmt(" (%.0f uu)", RangeDistance(r)) + "##" + idSuffix + std::to_string(r);
            if (Choice(label.c_str(), range == r, ImVec2(130, 26)) && range != r) { range = r; changed = true; }
        }
        return changed;
    }

    // A round step for axis labels (1, 2, 2.5 or 5 times a power of ten), about `span / 4`.
    float NiceStep(float span)
    {
        float raw = std::max(1.f, span / 4.f);
        float mag = std::pow(10.f, std::floor(std::log10(raw)));
        float n = raw / mag;
        float nice = n <= 1.f ? 1.f : n <= 2.f ? 2.f : n <= 2.5f ? 2.5f : n <= 5.f ? 5.f : 10.f;
        return nice * mag;
    }

    struct GraphMark { float value; const char* name; };

    // Score graph: the scores of a preset's runs, oldest on the left, with the score axis down
    // the left side, any marked scores as faint lines, a gold dot on your best run, and the exact score of any
    // run when you hover over it. firstRun is the run number of v[0].
    void ScoreGraph(const char* id, const float* v, int n, int firstRun, const std::vector<GraphMark>& marks, ImVec2 size)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        if (n <= 0) return;

        // Score range: a little room around your scores, on round numbers.
        float mn = *std::min_element(v, v + n), mx = *std::max_element(v, v + n);
        float span = std::max(mx - mn, std::max(100.f, 0.1f * mx));
        float step = NiceStep(span * 1.2f);
        float lo = std::max(0.f, std::floor((mn - 0.1f * span) / step) * step);
        float hi = std::ceil((mx + 0.1f * span) / step) * step;
        if (hi <= lo) hi = lo + step;

        const ImU32 colText = IM_COL32(170, 178, 190, 255), colGrid = IM_COL32(255, 255, 255, 28);
        const ImU32 colAxis = IM_COL32(255, 255, 255, 90), colLine = IM_COL32(90, 170, 255, 255);
        const ImU32 colBest = IM_COL32(255, 200, 60, 255), colMark = IM_COL32(255, 200, 60, 60);
        const float labelW = ImGui::CalcTextSize(FmtInt(static_cast<long long>(hi)).c_str()).x + 12.f;
        const float lineH = ImGui::GetTextLineHeight();
        const ImVec2 p0(origin.x + labelW, origin.y + lineH * 0.5f);
        const ImVec2 p1(origin.x + size.x - 6.f, origin.y + size.y - lineH - 8.f);
        auto yOf = [&](float s) { return p1.y - (s - lo) / (hi - lo) * (p1.y - p0.y); };
        auto xOf = [&](int i) { return n == 1 ? 0.5f * (p0.x + p1.x) : p0.x + (p1.x - p0.x) * i / static_cast<float>(n - 1); };

        dl->AddRectFilled(p0, p1, IM_COL32(18, 22, 30, 220));
        // Score axis: gridlines with their scores on the left.
        for (float t = lo; t <= hi + 0.5f * step; t += step)
        {
            float y = yOf(t);
            dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), colGrid);
            std::string label = FmtInt(std::llround(t));
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText(ImVec2(p0.x - ts.x - 6.f, y - 0.5f * ts.y), colText, label.c_str());
        }
        // Marked scores that fall inside the range, named at the right.
        for (const auto& m : marks)
        {
            if (m.value <= lo || m.value >= hi) continue;
            float y = yOf(m.value);
            dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), colMark, 1.5f);
            ImVec2 ts = ImGui::CalcTextSize(m.name);
            dl->AddText(ImVec2(p1.x - ts.x - 4.f, y - ts.y - 1.f), colMark | IM_COL32(0, 0, 0, 255), m.name);
        }
        dl->AddLine(ImVec2(p0.x, p0.y), ImVec2(p0.x, p1.y), colAxis);
        dl->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), colAxis);

        // The scores.
        std::vector<ImVec2> pts(n);
        int best = 0;
        for (int i = 0; i < n; ++i)
        {
            pts[i] = ImVec2(xOf(i), yOf(v[i]));
            if (v[i] > v[best]) best = i;
        }
        if (n > 1) dl->AddPolyline(pts.data(), n, colLine, false, 2.f);
        if (n <= 60)
            for (const auto& p : pts) dl->AddCircleFilled(p, 2.5f, colLine);
        dl->AddCircleFilled(pts[best], 5.f, colBest);

        // Run numbers along the bottom.
        std::string left = "run " + std::to_string(firstRun), right = "run " + std::to_string(firstRun + n - 1);
        dl->AddText(ImVec2(p0.x, p1.y + 4.f), colText, left.c_str());
        if (n > 1) dl->AddText(ImVec2(p1.x - ImGui::CalcTextSize(right.c_str()).x, p1.y + 4.f), colText, right.c_str());

        // Hover: the nearest run's exact score.
        if (hovered)
        {
            float mx2 = ImGui::GetMousePos().x;
            int i = n == 1 ? 0 : std::clamp(static_cast<int>(std::lround((mx2 - p0.x) / (p1.x - p0.x) * (n - 1))), 0, n - 1);
            dl->AddLine(ImVec2(pts[i].x, p0.y), ImVec2(pts[i].x, p1.y), IM_COL32(255, 255, 255, 60));
            dl->AddCircleFilled(pts[i], 5.f, IM_COL32(255, 255, 255, 255));
            ImGui::SetTooltip("Run %d: %s%s", firstRun + i, FmtInt(std::llround(v[i])).c_str(), i == best ? "  (best shown)" : "");
        }
    }
}

// ===========================================================================
// Load / unload
// ===========================================================================

void AirLabs::onLoad()
{
    rng.seed(std::random_device{}());
    RegisterCvars();

    auto notifier = [this](const char* name, std::function<void()> fn, const char* desc) {
        cvarManager->registerNotifier(name, [fn](std::vector<std::string>) { fn(); }, desc, PERMISSION_ALL);
    };
    notifier("airlabs_start", [this] { Start(); }, "Go to the start ball (begins a run)");
    notifier("airlabs_stop", [this] { Stop(); }, "Stop the trainer");
    notifier("airlabs_toggle", [this] { Toggle(); }, "Start, or stop the run in progress");
    notifier("airlabs_next", [this] { SkipTarget(); }, "Skip the current target (the run then won't count toward records)");
    notifier("airlabs_mode_toggle", [this] {
        CVarWrapper cv = cvarManager->getCvar("airlabs_mode");
        if (!cv.IsNull()) cv.setValue((cv.getIntValue() + 1) % kModeCount);
    }, "Cycle Flick -> Track -> Hybrid");
    notifier("airlabs_preset_next", [this] {
        int m = std::clamp(*mode, 0, kModeCount - 1);
        CVarWrapper cv = cvarManager->getCvar(PresetCvar(m));
        if (!cv.IsNull()) cv.setValue((PresetIndex(m) + 1) % (PresetCount(m) + 1)); // includes Custom
    }, "Next preset in the current mode (including Custom)");
    notifier("airlabs_laser_toggle", [this] {
        CVarWrapper cv = cvarManager->getCvar("airlabs_laser");
        if (!cv.IsNull()) cv.setValue(cv.getBoolValue() ? 0 : 1);
    }, "Turn the laser on or off");
    notifier("airlabs_aim_toggle", [this] {
        if (CurrentSettings().mode != kFlick) return;
        int aim = CurrentSettings().aim;
        MakeCustom();
        SetCvarI("airlabs_aim", aim == 1 ? 0 : 1);
    }, "Flick: switch between nose and wheels aim (makes the run Custom)");
    notifier("airlabs_reset_session", [this] { ResetSession(); }, "Reset this session's run counter");
    notifier("airlabs_reset_directions", [this] { ResetDirectionStats(); }, "Clear the saved times-by-direction grid");

    notifier("airlabs_space", [this] { SetCvarI("airlabs_map", 0); LoadSpaceMap(); }, "Go to the AirLabs space map (Freeplay, car held in the air); Start takes you there from now on");
    notifier("airlabs_standard", [this] { SetCvarI("airlabs_map", 1); LoadStandardMap(); }, "Go back to the standard field (DFH Stadium Freeplay); Start uses it from now on");
    notifier("airlabs_menu", [this] { ToggleWindow(); }, "Open or close the AirLabs window (bind it: bind O AL_menu)");
    notifier("AL_menu", [this] { ToggleWindow(); }, "Open or close the AirLabs window: bind <any key> AL_menu");
    notifier("al_menu", [this] { ToggleWindow(); }, "Open or close the AirLabs window (same as AL_menu)");
    notifier("airlabs_lab", [this] { LabToggle(); }, "Start or stop the Boost Lab (test): fly at the ball with real momentum");
    notifier("airlabs_lab_botprobe", [this] { BotProbeStart(); }, "Test: spawn a bot car and hold it beside you for 20 s (checks whether a simulated player can work)");
    notifier("airlabs_rank", [this] { RefreshRealRanks("asked"); }, "Print your Rocket League ranks as the game reports them (1v1, 2v2, 3v3)");
    notifier("airlabs_update", [this] { StartUpdate(); }, "Install the newest AirLabs from bakkesplugins (when the window says one is out)");
    cvarManager->registerNotifier("airlabs_watch", [this](std::vector<std::string> args) {
        if (args.size() > 1) WatchReplay(std::atoi(args[1].c_str()));
    }, "Watch a leaderboard run's replay: airlabs_watch <run number>", PERMISSION_ALL);
    LoadFonts();

    LoadRegionStats();
    MigrateOldData();
    LoadRecords();
    LoadSavedPresets();
    LoadLeaderboardFile();
    SchedulePendingRetry(15.f); // runs left over from a session without internet go up soon after loading
    // Your Rocket League ranks (once the game has loaded them), your saved bests, and updates.
    {
        auto alive = lbAlive;
        gameWrapper->SetTimeout([this, alive](GameWrapper*) { if (alive->load()) RefreshRealRanks("30 s after loading"); }, 30.f);
        gameWrapper->SetTimeout([this, alive](GameWrapper*) { if (alive->load()) SendSavedBests(); }, 40.f);
        gameWrapper->SetTimeout([this, alive](GameWrapper*) { if (alive->load()) RefreshRealRanks("3 min after loading"); }, 180.f);
        gameWrapper->SetTimeout([this, alive](GameWrapper*) { if (alive->load()) CheckForUpdate(); }, 8.f);
    }
    {
        std::error_code ec;
        haveSpaceMap = std::filesystem::exists(SpaceMapPath(), ec);
    }

    // Fires once per physics tick for every car, before its input is applied.
    gameWrapper->HookEventWithCaller<CarWrapper>(kTickEvent,
        [this](CarWrapper car, void* params, std::string) { OnPhysicsTick(car, params); });
    // Leaving freeplay tears the game event down; release the pin.
    gameWrapper->HookEvent(kEndEvent, [this](std::string) {
        if (labOn && WallSeconds() - labStartWall > 30.0) labOn = false; // (just started: it's loading the map)
        labNeedsStart = true;
        state = RunState::Idle;
        hasTarget = false;
        spaceStopped = false;
        replayOver = false;
        spaceCheckWall = -10.0; // the map is changing: look again
    });
    gameWrapper->RegisterDrawable([this](CanvasWrapper canvas) { Render(canvas); });

    cvarManager->log("[AirLabs] loaded. Open the window (airlabs_menu) and press Start: it takes you to the space map.");
}

void AirLabs::onUnload()
{
    lbAlive->store(false);
    state = RunState::Idle;
    gameWrapper->UnhookEvent(kTickEvent);
    gameWrapper->UnhookEvent(kEndEvent);
    gameWrapper->UnregisterDrawables();
}

const char* AirLabs::PluginVersion() { return kVersion; }

void AirLabs::RegisterCvars()
{
    auto mkI = [](int v) { return std::make_shared<int>(v); };
    auto mkF = [](float v) { return std::make_shared<float>(v); };
    auto mkB = [](bool v) { return std::make_shared<bool>(v); };
    auto reg = [this](const char* name, const std::string& def, const char* desc, float lo, float hi) {
        return cvarManager->registerCvar(name, def, desc, true, true, lo, true, hi);
    };
    auto maxIdx = [](int m) { return static_cast<float>(PresetCount(m)); };

    mode = mkI(kTrack); // new players start on Track Control, Medium range: the best place to start
    presetFlick = mkI(4); presetTrack = mkI(1); presetHybrid = mkI(0); presetChase = mkI(1);
    baseFlick = mkI(4); baseTrack = mkI(1); baseHybrid = mkI(0); baseChase = mkI(1); rangeSel = mkI(kMedium);
    chaseMin = mkF(kChaseMoveMin); chaseMax = mkF(kChaseMoveMax);
    holdMs = mkI(200); minAngleDeg = mkF(30.f); maxAngleDeg = mkF(180.f);
    timeoutSec = mkF(kPresetTimeout); nextDelayMs = mkI(static_cast<int>(std::lround(kPresetDelaySec * 1000.f))); aimMode = mkI(0); customDynamic = mkI(0);
    trackSpeed = mkF(30.f); trackTurnEvery = mkF(1.f); maxTurnChange = mkF(90.f);
    dynMin = mkF(15.f); dynMax = mkF(45.f); hybridTrackSec = mkF(3.f); runLength = mkF(60.f);
    heightMin = mkF(-90.f); heightMax = mkF(90.f); targetDistance = mkF(kDefaultCustomDistance); settleRate = mkF(25.f);
    dirMask = mkI(static_cast<int>(kAllRegions));
    weakSpot = mkB(false); settleMode = mkB(false);
    laserEnabled = mkB(false); showAimMarker = mkB(false); guideInvert = mkB(false); readout = mkB(false);
    noAirRoll = mkB(false);
    mapPref = mkI(0);
    hudGrid = mkB(false); blockJumpBoost = mkB(true);
    laserLength = mkF(6000.f); laserWidth = mkF(3.f); guideAccel = mkF(450.f); guideMode = mkI(0);
    laserToBall = mkB(true); laserColorMode = mkI(0); laserColor = std::make_shared<std::string>("#5AA0FF");
    lbPost = mkB(true); lbUrl = std::make_shared<std::string>(kLeaderboardUrl);
    lbCustomName = std::make_shared<std::string>();
    showTips = mkB(true);
    labMotion = mkI(kLabRespawn); labBubble = mkF(800.f); labPass = mkF(250.f); labSpeed = mkF(800.f); labHang = mkF(0.75f);
    labSpeedMin = mkF(400.f); labSpeedMax = mkF(1400.f); labCurve = mkF(0.f); labRun = mkF(0.f); labCone = mkF(180.f);
    labHeightMin = mkF(-90.f); labHeightMax = mkF(90.f); labSteep = mkF(45.f); labFloor = mkF(300.f);
    labDynamic = mkB(false); labDust = mkB(false); labMarkers = mkB(true); labNavball = mkB(false); labSpeedLimit = mkB(false); labFlips = mkI(kLabFlipsReal); labDebug = mkB(true);

    reg("airlabs_mode", "1", "0 = Flick, 1 = Track (default), 2 = Hybrid, 3 = Chase", 0.f, static_cast<float>(kModeCount - 1)).bindTo(mode);
    reg("airlabs_preset_flick", "4", "Flick preset (last = Custom)", 0.f, maxIdx(kFlick)).bindTo(presetFlick);
    reg("airlabs_preset_track", "1", "Track preset (last = Custom)", 0.f, maxIdx(kTrack)).bindTo(presetTrack);
    reg("airlabs_preset_hybrid", "0", "Hybrid preset (last = Custom)", 0.f, maxIdx(kHybrid)).bindTo(presetHybrid);
    reg("airlabs_preset_chase", "1", "Chase preset (last = Custom)", 0.f, maxIdx(kChase)).bindTo(presetChase);
    reg("airlabs_range", "1", "Preset range: 0 = Short (400 uu), 1 = Medium (600 uu), 2 = Long (900 uu)", 0.f, 2.f).bindTo(rangeSel);
    reg("airlabs_custom_base_flick", "4", "Preset the Flick Custom settings started from", 0.f, maxIdx(kFlick) - 1.f).bindTo(baseFlick);
    reg("airlabs_custom_base_track", "1", "Preset the Track Custom settings started from", 0.f, maxIdx(kTrack) - 1.f).bindTo(baseTrack);
    reg("airlabs_custom_base_hybrid", "0", "Preset the Hybrid Custom settings started from", 0.f, maxIdx(kHybrid) - 1.f).bindTo(baseHybrid);
    reg("airlabs_custom_base_chase", "1", "Preset the Chase Custom settings started from", 0.f, maxIdx(kChase) - 1.f).bindTo(baseChase);

    // Custom values
    reg("airlabs_hold_ms", "200", "Custom: hold to confirm (ms)", 0.f, 2000.f).bindTo(holdMs);
    reg("airlabs_min_angle", "30", "Custom: smallest flick turn (deg)", 0.f, 180.f).bindTo(minAngleDeg);
    reg("airlabs_max_angle", "180", "Custom: biggest flick turn (deg)", 0.f, 180.f).bindTo(maxAngleDeg);
    reg("airlabs_timeout", "5", "Custom: time limit per flick target, 0 = none (s)", 0.f, 30.f).bindTo(timeoutSec);
    reg("airlabs_next_delay", "20", "Custom: delay between targets (ms)", 0.f, 3000.f).bindTo(nextDelayMs);
    reg("airlabs_aim", "0", "Custom Flick: 0 = nose, 1 = wheels", 0.f, 1.f).bindTo(aimMode);
    reg("airlabs_dynamic", "0", "Custom Track/Hybrid: 0 = steady speed, 1 = changing speed", 0.f, 1.f).bindTo(customDynamic);
    reg("airlabs_track_speed", "30", "Custom: ball speed (deg/s, max 90)", 5.f, kMaxBallSpeed).bindTo(trackSpeed);
    reg("airlabs_track_turn", "1", "Custom: seconds between direction changes", 0.3f, 10.f).bindTo(trackTurnEvery);
    reg("airlabs_max_turn", "90", "Custom: biggest direction change (deg)", 0.f, 180.f).bindTo(maxTurnChange);
    reg("airlabs_dyn_min", "15", "Custom: slowest speed when changing (deg/s)", 5.f, kMaxBallSpeed).bindTo(dynMin);
    reg("airlabs_dyn_max", "45", "Custom: fastest speed when changing (deg/s)", 5.f, kMaxBallSpeed).bindTo(dynMax);
    reg("airlabs_hybrid_track", "4", "Custom Hybrid: seconds of tracking after lock-on", 1.f, 15.f).bindTo(hybridTrackSec);
    reg("airlabs_chase_min", std::to_string(kChaseMoveMin), "Custom Chase: shortest dash (s)", 0.2f, 5.f).bindTo(chaseMin);
    reg("airlabs_chase_max", std::to_string(kChaseMoveMax), "Custom Chase: longest dash (s)", 0.2f, 5.f).bindTo(chaseMax);
    reg("airlabs_run_length", "60", "Custom: run length, 0 = endless (s)", 0.f, 600.f).bindTo(runLength);
    reg("airlabs_height_min", "-90", "Custom: lowest ball height above the horizon (deg)", -90.f, 90.f).bindTo(heightMin);
    reg("airlabs_height_max", "90", "Custom: highest ball height above the horizon (deg)", -90.f, 90.f).bindTo(heightMax);
    reg("airlabs_distance", "600", "Custom: ball distance (uu)", 300.f, kMaxBallDistance).bindTo(targetDistance);
    reg("airlabs_dirs", std::to_string(kAllRegions), "Custom: allowed directions, one bit each (511 = all)", 0.f, static_cast<float>(kAllRegions)).bindTo(dirMask);
    reg("airlabs_weak_spot", "0", "Custom: more targets in your slowest directions", 0.f, 1.f).bindTo(weakSpot);
    reg("airlabs_settle", "0", "Custom: must stop on the target", 0.f, 1.f).bindTo(settleMode);
    reg("airlabs_settle_rate", "25", "Custom: counts as stopped below this turning speed (deg/s)", 5.f, 120.f).bindTo(settleRate);

    // Assists (never affect whether a run counts)
    reg("airlabs_laser", "0", "Laser along your aim", 0.f, 1.f).bindTo(laserEnabled);
    reg("airlabs_noroll", "0", "No air roll: roll input is blocked during runs, with their own records (nose aim only)", 0.f, 1.f).bindTo(noAirRoll);
    reg("airlabs_map", "0", "Where Start takes you: 0 = the AirLabs space map (default), 1 = the standard field (DFH Stadium Freeplay)", 0.f, 1.f).bindTo(mapPref);
    reg("airlabs_laser_length", "6000", "Laser length (uu)", 500.f, 20000.f).bindTo(laserLength);
    reg("airlabs_laser_width", "3", "Laser thickness (px)", 1.f, 12.f).bindTo(laserWidth);
    reg("airlabs_laser_to_ball", "1", "The laser ends at the ball's distance (0 = it reaches as far as airlabs_laser_length)", 0.f, 1.f).bindTo(laserToBall);
    reg("airlabs_laser_colormode", "0", "Laser colour: 0 = red/yellow/green with your aim, 1 = one colour (airlabs_laser_color)", 0.f, 1.f).bindTo(laserColorMode);
    cvarManager->registerCvar("airlabs_laser_color", "#5AA0FF", "Laser colour when it's one colour (#RRGGBB)", true, false, 0.f, false, 0.f, true).bindTo(laserColor);
    reg("airlabs_show_nose", "0", "Aim marker (white cross)", 0.f, 1.f).bindTo(showAimMarker);
    reg("airlabs_guide", "0", "Stick guide in Custom runs", 0.f, 1.f).bindTo(guideMode);
    reg("airlabs_guide_invert", "0", "Stick guide: you play with inverted pitch", 0.f, 1.f).bindTo(guideInvert);
    reg("airlabs_guide_accel", "450", "Stick guide brake cue timing; lower = earlier", 300.f, 1500.f).bindTo(guideAccel);
    reg("airlabs_readout", "0", "Readout: stick %, spin speed, ball speed, physics rate", 0.f, 1.f).bindTo(readout);
    reg("airlabs_hud_grid", "0", "Show the times-by-direction grid during Flick and Hybrid", 0.f, 1.f).bindTo(hudGrid);
    reg("airlabs_block_jump", "1", "Custom runs: block jump and boost while pinned", 0.f, 1.f).bindTo(blockJumpBoost);
    reg("airlabs_tips", "1", "Show a training tip on the Play page", 0.f, 1.f).bindTo(showTips);

    // Boost Lab (test)
    reg("airlabs_lab_motion", "0", "Boost Lab: 0 = Respawn (touch it and a new ball appears), 1 = Constant (the ball never stops)", 0.f, 1.f).bindTo(labMotion);
    reg("airlabs_lab_bubble", "800", "Boost Lab: radius of the bubble around your car that the ball stays in (uu)", 300.f, 2000.f).bindTo(labBubble);
    reg("airlabs_lab_pass", "250", "Boost Lab: the closest a launched or bounced ball's path comes to your car (uu)", 0.f, 600.f).bindTo(labPass);
    reg("airlabs_lab_speed", "800", "Boost Lab: the ball's speed after a launch or bounce, as you see it (uu/s)", 100.f, 2300.f).bindTo(labSpeed);
    reg("airlabs_lab_dynamic", "0", "Boost Lab: a random ball speed for each launch or bounce, between airlabs_lab_speed_min and _max", 0.f, 1.f).bindTo(labDynamic);
    reg("airlabs_lab_speed_min", "400", "Boost Lab, Dynamic: slowest ball speed (uu/s)", 100.f, 2300.f).bindTo(labSpeedMin);
    reg("airlabs_lab_speed_max", "1400", "Boost Lab, Dynamic: fastest ball speed (uu/s)", 100.f, 2300.f).bindTo(labSpeedMax);
    reg("airlabs_lab_curve", "0", "Boost Lab: how fast the ball curves while it flies on its own (deg/s, 0 = straight)", 0.f, 120.f).bindTo(labCurve);
    reg("airlabs_lab_run", "0", "Boost Lab: run length (s, 0 = endless)", 0.f, 600.f).bindTo(labRun);
    reg("airlabs_lab_cone", "180", "Boost Lab: new balls appear within this angle of your nose (deg, 180 = anywhere)", 10.f, 180.f).bindTo(labCone);
    reg("airlabs_lab_height_min", "-90", "Boost Lab: lowest a new ball appears (deg above the horizon)", -90.f, 90.f).bindTo(labHeightMin);
    reg("airlabs_lab_height_max", "90", "Boost Lab: highest a new ball appears (deg above the horizon)", -90.f, 90.f).bindTo(labHeightMax);
    reg("airlabs_lab_steep", "45", "Boost Lab: the steepest a launched or bounced ball travels, up or down from level (deg)", 0.f, 90.f).bindTo(labSteep);
    reg("airlabs_lab_floor", "300", "Boost Lab: how far below your car the ball can go; it bounces off this floor (uu, 2000 = no floor)", 100.f, 2000.f).bindTo(labFloor);
    reg("airlabs_lab_speed_limit", "0", "Boost Lab: the 2300 speed cap and the ball's air drag, as on a field (off: open space, only how you move relative to the ball counts)", 0.f, 1.f).bindTo(labSpeedLimit);
    reg("airlabs_lab_flips", "1", "Boost Lab flips: 0 = off, 1 = real (one for each new ball, one for each flip reset), 2 = infinite", 0.f, 2.f).bindTo(labFlips);
    reg("airlabs_lab_debug", "1", "Boost Lab: a debug line on the HUD (jump presses, flips, flip resets, what the game says)", 0.f, 1.f).bindTo(labDebug);
    reg("airlabs_lab_dust", "0", "Boost Lab: specks of dust that streak past as you move", 0.f, 1.f).bindTo(labDust);
    reg("airlabs_lab_markers", "1", "Boost Lab: markers for how you're moving relative to the ball (on it = you'll hit it)", 0.f, 1.f).bindTo(labMarkers);
    reg("airlabs_lab_navball", "0", "Boost Lab: navball (your orientation, the ball and the markers, all the way round)", 0.f, 1.f).bindTo(labNavball);
    reg("airlabs_lab_hang", "0.75", "Boost Lab, Respawn: how long a new ball waits before it launches (s)", 0.f, 3.f).bindTo(labHang);

    // Leaderboard
    cvarManager->registerCvar("airlabs_lb_name", "", "The name shown on the leaderboard (blank = your Rocket League name)", true, false, 0.f, false, 0.f, true).bindTo(lbCustomName);
    reg("airlabs_lb_post", "1", "Post your preset runs to the public AirLabs leaderboard (your best per preset, with its replay, under your Rocket League name). On by default", 0.f, 1.f).bindTo(lbPost);
    // Not saved to the config: a new version's server address always applies.
    cvarManager->registerCvar("airlabs_lb_url", kLeaderboardUrl, "AirLabs leaderboard server", true, false, 0.f, false, 0.f, false).bindTo(lbUrl);
}

// ===========================================================================
// Settings: a preset, or the Custom values
// ===========================================================================

const char* AirLabs::PresetCvar(int m)
{
    switch (m)
    {
    case kFlick: return "airlabs_preset_flick";
    case kTrack: return "airlabs_preset_track";
    case kChase: return "airlabs_preset_chase";
    default:     return "airlabs_preset_hybrid";
    }
}

const char* AirLabs::BaseCvar(int m)
{
    switch (m)
    {
    case kFlick: return "airlabs_custom_base_flick";
    case kTrack: return "airlabs_custom_base_track";
    case kChase: return "airlabs_custom_base_chase";
    default:     return "airlabs_custom_base_hybrid";
    }
}

int AirLabs::BaseIndex(int m) const
{
    int v = m == kFlick ? *baseFlick : m == kTrack ? *baseTrack : m == kChase ? *baseChase : *baseHybrid;
    return std::clamp(v, 0, PresetCount(m) - 1);
}

int AirLabs::PresetIndex(int m) const
{
    int v = m == kFlick ? *presetFlick : m == kTrack ? *presetTrack : m == kChase ? *presetChase : *presetHybrid;
    return std::clamp(v, 0, PresetCount(m));
}

std::string AirLabs::PresetLabel(int m, int preset)
{
    if (preset < 0 || preset >= PresetCount(m)) return "Custom";
    std::string group = PresetGroup(m, preset);
    return group.empty() ? std::string(PresetName(m, preset)) : group + " " + PresetName(m, preset);
}

std::string AirLabs::RangeLabel(int m, int preset, int range)
{
    if (preset < 0 || preset >= PresetCount(m)) return "";                       // Custom
    if (!PresetUsesRange(m, preset)) return Fmt("%.0f uu", PresetDistance(m, preset, 0)); // Flip Reset
    if (range < 0) return "";                                                     // saved before ranges
    return RangeName(range);
}

std::string AirLabs::PresetRangeLabel(int m, int preset, int range)
{
    std::string rl = RangeLabel(m, preset, range);
    return rl.empty() ? PresetLabel(m, preset) : PresetLabel(m, preset) + " (" + rl + ")";
}

std::string AirLabs::RunLabel(int m, int preset, int range, int userPreset)
{
    if (userPreset > 0)
    {
        std::string name = SavedPresetName(userPreset);
        return name.empty() ? std::string("deleted preset") : name;
    }
    return PresetRangeLabel(m, preset, range);
}

std::string AirLabs::CurrentPresetLabel(const DrillSettings& s) const
{
    if (!s.custom) return PresetRangeLabel(s.mode, s.preset, s.range);
    auto* self = const_cast<AirLabs*>(this);
    if (s.userPreset > 0)
    {
        std::string name = self->SavedPresetName(s.userPreset);
        return name.empty() ? std::string("Custom") : name;
    }
    if (!detachedFrom[s.mode].empty()) return "Custom (based on " + detachedFrom[s.mode] + ")";
    int base = BaseIndex(s.mode);
    return "Custom (based on " + PresetLabel(s.mode, base) + ")";
}

AirLabs::DrillSettings AirLabs::CurrentSettings() const
{
    DrillSettings s;
    s.mode = std::clamp(*mode, 0, kModeCount - 1);
    s.preset = PresetIndex(s.mode);
    s.custom = s.preset >= PresetCount(s.mode);
    s.range = std::clamp(*rangeSel, 0, kRangeCount - 1);

    if (!s.custom)
    {
        // A preset: everything fixed, so runs compare between players.
        s.holdSec = kPresetHoldSec;
        s.timeout = s.mode == kChase ? kChaseTimeout : kPresetTimeout;
        s.delaySec = kPresetDelaySec;
        s.runLength = kPresetRunSec;
        s.distance = PresetDistance(s.mode, s.preset, s.range);
        s.hybridTrack = kHybridTrackSec;
        s.blockJump = true;
        if (s.mode == kFlick)
        {
            const FlickPreset& p = kFlickPresets[s.preset];
            s.minTurn = p.minTurn; s.maxTurn = p.maxTurn; s.aim = p.aim; s.band = p.band;
        }
        else
        {
            const MovePreset& p = *MovePresetFor(s.mode, s.preset);
            s.path.dynamic = p.dynamic;
            s.path.speed = p.dynamic ? (p.vMin + p.vMax) * 0.5f : p.speed;
            s.path.vMin = p.vMin;
            s.path.vMax = p.vMax;
            s.path.changeEvery = p.dynamic ? ChangeIntervalForSpeed(s.path.speed) : p.changeEvery;
            s.path.maxTurn = p.maxTurn;
            if (s.mode == kHybrid) { s.minTurn = kHybridFlickMin; s.maxTurn = kHybridFlickMax; }
        }
        s.target = BallTargetDeg(s.distance); // the ball's own size at that distance
        s.path.band = s.band;
        s.noRoll = *noAirRoll && s.aim == 0; // wheels aim needs roll, so No air roll doesn't apply to it
        return s;
    }

    // Custom: the values you've set.
    s.holdSec = std::max(0, *holdMs) / 1000.f;
    s.minTurn = std::min(*minAngleDeg, *maxAngleDeg);
    s.maxTurn = std::max(*minAngleDeg, *maxAngleDeg);
    s.timeout = std::max(0.f, *timeoutSec);
    s.delaySec = std::max(0, *nextDelayMs) / 1000.f;
    s.aim = s.mode == kFlick ? std::clamp(*aimMode, 0, 1) : 0; // wheels aim is Flick only
    s.band.minDeg = std::clamp(std::min(*heightMin, *heightMax), -90.f, 90.f);
    s.band.maxDeg = std::clamp(std::max(*heightMin, *heightMax), -90.f, 90.f);
    s.path.dynamic = s.mode != kFlick && *customDynamic != 0;
    s.path.speed = std::clamp(*trackSpeed, 1.f, kMaxBallSpeed);
    s.path.vMin = std::clamp(std::min(*dynMin, *dynMax), 1.f, kMaxBallSpeed);
    s.path.vMax = std::clamp(std::max(*dynMin, *dynMax), 1.f, kMaxBallSpeed);
    s.path.changeEvery = std::max(0.1f, *trackTurnEvery);
    s.path.maxTurn = std::clamp(*maxTurnChange, 0.f, 180.f);
    s.hybridTrack = std::max(0.5f, *hybridTrackSec);
    s.chaseMin = std::clamp(std::min(*chaseMin, *chaseMax), 0.2f, 5.f);
    s.chaseMax = std::clamp(std::max(*chaseMin, *chaseMax), 0.2f, 5.f);
    s.runLength = std::max(0.f, *runLength);
    s.distance = std::clamp(*targetDistance, 300.f, kMaxBallDistance);
    s.target = BallTargetDeg(s.distance); // the ball's own size at that distance
    s.settle = *settleMode && s.mode != kTrack;
    s.settleRate = *settleRate;
    s.weakSpot = *weakSpot && s.mode != kTrack;
    s.dirMask = static_cast<unsigned>(*dirMask) & kAllRegions;
    if (s.dirMask == 0u) s.dirMask = kAllRegions;
    s.blockJump = *blockJumpBoost;
    s.path.band = s.band;
    s.path.dirMask = s.mode == kFlick ? 0u : (s.dirMask & 0xFFu);
    s.userPreset = userSel[s.mode].load();
    s.noRoll = *noAirRoll && s.aim == 0; // wheels aim needs roll, so No air roll doesn't apply to it
    return s;
}

void AirLabs::SetCvarF(const char* cvar, float v)
{
    CVarWrapper cv = cvarManager->getCvar(cvar);
    if (!cv.IsNull()) cv.setValue(v);
}

void AirLabs::SetCvarI(const char* cvar, int v)
{
    CVarWrapper cv = cvarManager->getCvar(cvar);
    if (!cv.IsNull()) cv.setValue(v);
}

void AirLabs::CopySettingsToCustom(const DrillSettings& s)
{
    // Start Custom from exactly what the preset was doing.
    SetCvarF("airlabs_height_min", s.band.minDeg);
    SetCvarF("airlabs_height_max", s.band.maxDeg);
    SetCvarF("airlabs_run_length", s.runLength);
    SetCvarF("airlabs_distance", s.distance);
    SetCvarI("airlabs_dirs", static_cast<int>(kAllRegions));
    SetCvarI("airlabs_weak_spot", 0);
    SetCvarI("airlabs_settle", 0);
    if (s.mode != kTrack)
    {
        SetCvarI("airlabs_hold_ms", static_cast<int>(std::lround(s.holdSec * 1000.f)));
        SetCvarF("airlabs_min_angle", s.minTurn);
        SetCvarF("airlabs_max_angle", s.maxTurn);
        SetCvarF("airlabs_timeout", s.timeout);
        SetCvarI("airlabs_next_delay", static_cast<int>(std::lround(s.delaySec * 1000.f)));
        if (s.mode == kFlick) SetCvarI("airlabs_aim", s.aim);
    }
    if (s.mode != kFlick)
    {
        SetCvarI("airlabs_dynamic", s.path.dynamic ? 1 : 0);
        SetCvarF("airlabs_track_speed", s.path.speed);
        SetCvarF("airlabs_track_turn", s.path.dynamic ? 1.3f : s.path.changeEvery);
        SetCvarF("airlabs_max_turn", s.path.maxTurn);
        SetCvarF("airlabs_dyn_min", s.path.dynamic ? s.path.vMin : std::max(5.f, s.path.speed - 15.f));
        SetCvarF("airlabs_dyn_max", s.path.dynamic ? s.path.vMax : std::min(kMaxBallSpeed, s.path.speed + 15.f));
        SetCvarF("airlabs_hybrid_track", s.hybridTrack);
    }
    if (s.mode == kChase)
    {
        SetCvarF("airlabs_chase_min", s.chaseMin);
        SetCvarF("airlabs_chase_max", s.chaseMax);
    }
}

void AirLabs::MakeCustom()
{
    // Called just before any setting changes.
    const DrillSettings s = CurrentSettings();
    if (s.custom)
    {
        // Saved presets are locked: changing one turns the run into Custom based on it,
        // which can then be saved as a new preset.
        if (s.userPreset > 0)
        {
            detachedFrom[s.mode] = SavedPresetName(s.userPreset);
            userSel[s.mode] = 0;
        }
        return;
    }
    userSel[s.mode] = 0;
    detachedFrom[s.mode].clear();
    CopySettingsToCustom(s);
    SetCvarI(BaseCvar(s.mode), s.preset);
    SetCvarI(PresetCvar(s.mode), PresetCount(s.mode));
}

// ===========================================================================
// Run flow: Idle -> Ready (point at the start ball for 1.5 s) -> Running -> Results
// ===========================================================================

void AirLabs::Start()
{
    replayOver = false;
    if (labOn) LabEnd(); // gives the game its ball and car back
    gameWrapper->Execute([this](GameWrapper* gw) {
        const bool inFreeplay = gw->IsInFreeplay();
        spaceCheckWall = -10.0; // look at the map now, not up to a second ago
        // The space map is the default: Start takes you there from anywhere (the main menu, or
        // Freeplay on another map), and the run begins as it loads.
        if (WantSpaceMap() && !(inFreeplay && OnSpaceMap()))
        {
            LoadSpaceMap();
            return;
        }
        // Standard field chosen: from the main menu or the space map, load it and begin once
        // it's up.
        if (!inFreeplay || (haveSpaceMap && !WantSpaceMap() && OnSpaceMap()))
        {
            startOnLoad = true;
            startOnLoadWall = WallSeconds();
            LoadStandardMap();
            return;
        }
        ServerWrapper server = gw->GetGameEventAsServer();
        if (server.IsNull()) return;
        CarWrapper car = server.GetGameCar();
        if (car.IsNull()) return;
        showResults = false;
        readyLockUntil = 0.0;
        spaceStopped = false;
        EnterReady(car, CurrentSettings());
    });
}

void AirLabs::Stop()
{
    spaceStopped = true; // on the space map the car stays held in the air, without a start ball
    replayOver = false;
    if (state == RunState::Idle) return;
    state = RunState::Idle;
    showResults = false;
    hasTarget = false;
    hybridTracking = false;
    locking = false;
    cvarManager->log("[AirLabs] stopped.");
}

void AirLabs::Toggle()
{
    if (labOn) { LabEnd(); return; }
    if (state == RunState::Idle || state == RunState::Results) Start();
    else Stop();
}

void AirLabs::SkipTarget()
{
    if (state != RunState::Running || runMode == kTrack || runMode == kChase) return;
    runSkipped = true; // a skipped target means the run won't count toward records
    hybridTracking = false;
    hasTarget = false;
    locking = false;
    holdProgress = 0.f;
    nextSpawnAt = gameTime;
}

void AirLabs::EnterReady(CarWrapper car, const DrillSettings& s)
{
    // Reset the car: centre pin, level, turned 45 degrees away from the goal it attacks (the
    // start ball sits between the car and that goal), not spinning.
    // Blue (always the case in Freeplay) attacks the orange goal at +Y.
    int goalSign = 1;
    PriWrapper pri = car.GetPRI();
    if (!pri.IsNull() && pri.GetTeamNum() == 1) goalSign = -1;
    // The space map has no goals: face the galactic centre instead (the brightest part of the
    // sky, at -Y), so every run starts looking at the Milky Way. The drills are the same in any
    // direction with no floor, so scores aren't affected.
    if (OnSpaceMap()) goalSign = kSpaceFacing;
    const int yaw = StartYaw(goalSign);
    pinLocation = V(0.f, 0.f, kArenaCentreZ);
    car.SetLocation(pinLocation);
    car.SetRotation(Rotator(0, yaw, 0));
    car.SetVelocity(V(0.f, 0.f, 0.f));
    car.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
    const Basis b = BasisFromRotator(0, yaw, 0);
    havePrevBasis = false; // the reset isn't a turn: keep it out of the spin readout and stick learning
    spinDeg = 0.f;

    runMode = s.mode;
    runPreset = s.preset;
    runAim = s.aim;
    runRange = s.range;
    runUserPreset = s.userPreset;
    runNoRoll = s.noRoll;
    // Your best on this preset, for the HUD's best-score marker (looked up once here, not
    // every frame). Plain Custom runs have no records, so no marker.
    runBest = -1;
    if (!s.custom || s.userPreset > 0)
    {
        std::lock_guard<std::mutex> lock(recordsMutex);
        auto it = bestScore.find(RunKey(s.mode, s.preset, s.range, s.userPreset, s.noRoll));
        if (it != bestScore.end()) runBest = it->second;
    }
    // The start ball: straight ahead, between the car and the goal.
    startDir = GoalStartDir(goalSign, s.band);
    targetLocation = BallSpot(startDir, s);
    readyHeld = 0.f;
    hasTarget = true;
    locking = false;
    holdProgress = 0.f;
    hybridTracking = false;
    spawnFallback = false;
    guideOn = false;
    lastFlash = Flash::None;
    prevAim = AimVec(b, s.aim);
    aimSpeedDeg = 0.f;
    prevErr = -1.f;
    rateWindowStart = 0.0; // re-measure the tick rate during the 1.5 s start (needs 1 s)
    state = RunState::Ready;
}

void AirLabs::BeginRunning(const Basis& b, const DrillSettings& s)
{
    state = RunState::Running;
    showResults = false;
    runStart = gameTime;
    runEnd = s.runLength > 0.f ? gameTime + s.runLength : 1e18;
    run = RunStats{};
    runSkipped = false;
    lastFlash = Flash::None;
    hybridTracking = false;

    // Every run is random: new targets and new ball paths each time, in every mode, so
    // nothing can be learnt by heart. (Over a one-minute run the luck evens out to a few %.)
    rng.seed(std::random_device{}());

    // Preset runs are recorded tick by tick, for the leaderboard.
    recording = !s.custom;
    recClockTicks = 0;
    recReplay = Replay{};
    if (recording) recReplay.samples.reserve(static_cast<std::size_t>(kPresetRunSec * 120.f) + 8);

    if (s.mode == kTrack)
    {
        // The start ball becomes the moving ball.
        PathStart(path, rng, startDir, b, s.path, gameTime);
        hasTarget = true;
        locking = false;
        prevErr = -1.f;
        closingDeg = 0.f;
    }
    else if (s.mode == kChase)
    {
        // You're on the start ball, so it dashes off straight away.
        chase = ChaseState{};
        chase.path.pos = startDir;
        chaseOnSince = -1.0;
        ChaseLaunch(chase, rng, b, ChaseParamsFor(s), gameTime);
        RecEvent(kEvChaseLaunch);
        hasTarget = true;
        locking = true;
        prevErr = -1.f;
        closingDeg = 0.f;
    }
    else
    {
        hasTarget = false;
        nextSpawnAt = gameTime;
    }
    cvarManager->log(std::string("[AirLabs] run started: ") + ModeName(s.mode) + " / " + CurrentPresetLabel(s));
}

void AirLabs::EndRun(CarWrapper car, const Basis& b, const DrillSettings& s)
{
    if (hybridTracking) EndHybridRep(s); // count the part-finished tracking
    RefreshRealRanks("after a run");

    RunResult r;
    r.valid = true;
    r.mode = s.mode;
    r.preset = s.preset;
    r.aim = s.aim;
    r.custom = s.custom;
    r.range = s.custom ? -1 : s.range; // Custom runs aren't in any range's records
    r.userPreset = s.userPreset;
    r.noRoll = s.noRoll;
    if (s.noRoll) r.flags += "noroll ";
    if (runSkipped) r.flags += "skipped a target ";
    // Marked so keyboard runs can be told apart. (It doesn't affect the score.)
    if (run.inputTicks > 240 && run.analogTicks < run.inputTicks / 50) r.flags += "keyboard ";
    r.counted = (!s.custom || s.userPreset > 0) && !runSkipped; // saved presets keep their own records
    r.score = static_cast<int>(std::lround(run.score));
    r.targets = run.targets;
    r.misses = run.misses;
    r.hybReps = run.hybReps;
    r.avgTime = run.targets ? static_cast<float>(run.sumTime / run.targets) : -1.f;
    r.avgReaction = run.reactionN ? static_cast<float>(run.sumReaction / run.reactionN) : -1.f;
    r.avgOvershoot = run.overshootN ? static_cast<float>(run.sumOvershoot / run.overshootN) : -1.f;
    r.onPct = run.trackTime > 0.0 ? static_cast<float>(100.0 * run.onTime / run.trackTime) : -1.f;
    r.nearPct = run.trackTime > 0.0 ? static_cast<float>(100.0 * run.nearTime / run.trackTime) : -1.f;
    r.avgErr = run.trackTime > 0.0 ? static_cast<float>(run.errInt / run.trackTime) : -1.f;
    r.reacquire = run.offN ? static_cast<float>(run.offSum / run.offN) : -1.f;
    r.hybAvgPct = run.hybReps ? static_cast<float>(run.hybPctSum / run.hybReps) : -1.f;
    if (s.mode == kTrack && s.path.dynamic)
    {
        r.hasBands = true;
        for (int i = 0; i < kSpeedBands; ++i)
            r.bandPct[i] = run.bandTotal[i] > 0.25 ? static_cast<float>(100.0 * run.bandOn[i] / run.bandTotal[i]) : -1.f;
    }

    RunRecord rec;
    rec.when = NowStamp();
    rec.version = kVersion;
    rec.mode = r.mode; rec.preset = r.preset; rec.aim = r.aim; rec.range = r.range; rec.userPreset = r.userPreset; rec.counted = r.counted;
    rec.score = r.score; rec.tier = r.tier; rec.targets = r.targets; rec.misses = r.misses; rec.hybReps = r.hybReps;
    rec.avgTime = r.avgTime; rec.avgReaction = r.avgReaction; rec.avgOvershoot = r.avgOvershoot;
    rec.onPct = r.onPct; rec.nearPct = r.nearPct; rec.avgErr = r.avgErr; rec.reacquire = r.reacquire; rec.hybPct = r.hybAvgPct;
    rec.flags = r.flags;

    {
        std::lock_guard<std::mutex> lock(recordsMutex);
        auto it = bestScore.find(RunKey(s.mode, s.preset, s.range, s.userPreset, s.noRoll));
        r.prevBest = (r.counted && it != bestScore.end()) ? it->second : -1;
        r.newBest = r.counted && (r.prevBest < 0 || r.score > r.prevBest);
        r.share = BuildShareLine(r);
        AddRecordLocked(rec);
        lastResult = r;
        ++sessionRuns;
    }
    AppendRecordToFile(rec);
    cvarManager->log("[AirLabs] " + r.share);

    // Leaderboard: a preset run that counts, posted if you've opted in and it might be a new best.
    if (recording)
    {
        recording = false;
        Replay& rp = recReplay;
        rp.mode = static_cast<std::uint8_t>(s.mode); rp.preset = static_cast<std::uint8_t>(s.preset);
        rp.range = static_cast<std::uint8_t>(PresetUsesRange(s.mode, s.preset) ? s.range : 0);
        rp.aim = static_cast<std::uint8_t>(s.aim);
        rp.flags = static_cast<std::uint8_t>((s.noRoll ? kRfNoRoll : 0) | (r.flags.find("keyboard") != std::string::npos ? kRfKeyboard : 0) |
                                             (recClockTicks == 0 ? kRfTickTiming : 0));
        rp.distance = s.distance; rp.targetDeg = s.target;
        rp.runSec = static_cast<float>(gameTime - runStart);
        rp.tickRate = measuredRate;
        rp.score = r.score;
        rp.targets = static_cast<std::uint16_t>(r.targets); rp.misses = static_cast<std::uint16_t>(r.misses); rp.hybReps = static_cast<std::uint16_t>(r.hybReps);
        rp.version = kVersion;
        // With posting off, your best on each board is kept and goes up once you turn it on.
        if (r.counted && !s.custom && s.userPreset == 0) UploadRun(rp, *lbPost);
    }

    // Reset the car and the ball for the next round, with the results kept on screen until
    // the next run begins. The start ball can't be locked for a moment, so there's time to
    // read the results before the next run can start.
    hybridTracking = false;
    holdProgress = 0.f;
    EnterReady(car, s);
    showResults = true;
    readyLockUntil = gameTime + kReadyLockSec;
}

// ===========================================================================
// Physics tick
// ===========================================================================

void AirLabs::OnPhysicsTick(CarWrapper car, void* params)
{
    if (state == RunState::Idle && !labOn && botProbeUntil < WallSeconds() && !*readout && !startOnLoad && pendingReplayRun == 0 && !OnSpaceMap()) return;
    if (!gameWrapper->IsInFreeplay())
    {
        if (state != RunState::Idle) { state = RunState::Idle; hasTarget = false; }
        return;
    }
    ServerWrapper server = gameWrapper->GetGameEventAsServer();
    if (server.IsNull() || car.IsNull()) return;
    CarWrapper myCar = server.GetGameCar();
    if (!myCar.IsNull() && myCar.memory_address != car.memory_address && botProbeUntil >= WallSeconds())
    {
        BotProbeTick(car, params);
        return;
    }
    if (myCar.IsNull() || myCar.memory_address != car.memory_address) return;

    // Space map: hide the ball's own effects actor. It draws Rocket League's ball location
    // beam (a vertical line from the ball down to a ring on the ground), which with no ground
    // and the ball held still is just a stray line under the ball. The ball itself stays.
    if (OnSpaceMap())
    {
        BallWrapper ball = server.GetBall();
        if (!ball.IsNull())
        {
            FXActorWrapper fx = ball.GetFXActor();
            if (!fx.IsNull() && !fx.GetbHidden()) fx.SetHidden2(1);
        }
    }

    // ---- timing ----
    // Rocket League's physics runs at a fixed 120 ticks per second on every PC. When this
    // hook fires once per tick, each call is exactly 1/120 s of game time: runs are measured
    // the same at any frame rate, and pausing the game pauses the run. The rate is measured
    // continuously; if it ever looks wrong, the PC clock is used instead.
    double wall = WallSeconds();
    if (rateWindowStart <= 0.0 || wall - lastWall > 0.25) { rateWindowStart = wall; rateTicks = 0; } // start, pause or hitch
    float wallDt = static_cast<float>(std::clamp(wall - lastWall, 0.0, 0.05));
    lastWall = wall;
    ++rateTicks;
    if (wall - rateWindowStart >= 1.0)
    {
        measuredRate = static_cast<float>(rateTicks / (wall - rateWindowStart));
        tickTiming = measuredRate > 100.f && measuredRate < 140.f;
        rateWindowStart = wall;
        rateTicks = 0;
    }
    const float dt = tickTiming ? 1.f / 120.f : wallDt;
    gameTime += dt;

    if (labOn)
    {
        LabTick(car, server, params, dt);
        return;
    }

    const DrillSettings s = CurrentSettings();

    // ---- space map: there's no floor, so the car is never left to fall ----
    // You spawn straight into the start of a run; after Stop the car is still held, ball parked.
    bool spaceHold = false;
    // Start loaded the standard field: begin the run now it's up.
    if (startOnLoad)
    {
        if (state == RunState::Idle && !OnSpaceMap() && WallSeconds() - startOnLoadWall < 30.0)
        {
            showResults = false;
            readyLockUntil = 0.0;
            spaceStopped = false;
            EnterReady(car, s);
        }
        startOnLoad = false;
    }
    // Watch was pressed outside Freeplay: the map has loaded, so the replay starts now.
    if (pendingReplayRun != 0 && !play.samples.empty())
    {
        if (WallSeconds() - replayOnLoadWall < 30.0 && state != RunState::Running) StartReplay(car);
        pendingReplayRun = 0;
    }
    if (state == RunState::Idle && OnSpaceMap())
    {
        if (!spaceStopped)
        {
            showResults = false;
            readyLockUntil = 0.0;
            EnterReady(car, s);
        }
        else
        {
            spaceHold = true;
        }
    }
    const bool active = state != RunState::Idle;

    // ---- input: read it for the guide and readout, then (during a run) strip jump and boost ----
    if (params)
    {
        auto* input = reinterpret_cast<ControllerInput*>(params);
        inPitch = input->Pitch;
        inYaw = input->Yaw;
        inRoll = input->Roll;
        // Keyboard check: a keyboard only ever sends full or nothing; a stick spends time in between.
        if (state == RunState::Running)
        {
            float a[3] = { std::fabs(input->Pitch), std::fabs(input->Yaw), std::fabs(input->Roll) };
            bool any = false, partial = false;
            for (float v : a) { any |= v > 0.02f; partial |= v > 0.02f && v < 0.98f; }
            if (any) ++run.inputTicks;
            if (partial) ++run.analogTicks;
        }
        if (state == RunState::Replay)
        {
            // Watching a replay: the replay drives the car, so your own input does nothing.
            input->Pitch = input->Yaw = input->Roll = input->Throttle = input->Steer = 0.f;
            input->Jump = 0; input->ActivateBoost = 0; input->HoldingBoost = 0; input->Handbrake = 0;
        }
        if (active && s.noRoll) input->Roll = 0; // No air roll: air roll left/right and free air roll do nothing
        if (active && s.blockJump)
        {
            input->Jump = 0;
            input->ActivateBoost = 0;
            input->HoldingBoost = 0;
        }
    }

    // ---- pin position only; rotation and spin are left to the game's own air physics ----
    if (active || spaceHold)
    {
        pinLocation = V(0.f, 0.f, kArenaCentreZ);
        car.SetLocation(pinLocation);
        car.SetVelocity(V(0.f, 0.f, 0.f));
    }

    // ---- orientation and how it's changing ----
    Rotator rot = car.GetRotation();
    const Basis b = BasisFromRotator(rot.Pitch, rot.Yaw, rot.Roll);
    const Vector aim = AimVec(b, active ? runAim : s.aim);
    lastAim = aim;
    if (dt > 0.f)
    {
        aimSpeedDeg += std::min(1.f, dt * 15.f) * (AngleDeg(prevAim, aim) / dt - aimSpeedDeg);
        aimVel = Scale(Sub(aim, prevAim), 1.f / dt);
    }
    prevAim = aim;
    if (havePrevBasis && dt > 0.f)
    {
        // Whole-car spin speed, from how far the orientation turned this tick.
        float tr = Dot(b.forward, prevBasis.forward) + Dot(b.right, prevBasis.right) + Dot(b.up, prevBasis.up);
        float turned = std::acos(std::clamp((tr - 1.f) * 0.5f, -1.f, 1.f)) / kDeg;
        spinDeg += std::min(1.f, dt * 15.f) * (turned / dt - spinDeg);

        // Learn which way each stick input turns the car, so the guide's "your stick" square
        // is right without assuming the game's sign conventions.
        Vector df = Scale(Sub(b.forward, prevBasis.forward), 1.f / dt);
        Vector du = Scale(Sub(b.up, prevBasis.up), 1.f / dt);
        if (std::fabs(inPitch) > 0.3f) calPitch += inPitch * Dot(df, b.up);  // nose moving up
        if (std::fabs(inYaw) > 0.3f) calYaw += inYaw * Dot(df, b.right);     // nose moving right
        if (std::fabs(inRoll) > 0.3f) calRoll += inRoll * Dot(du, b.right);  // roof tipping right
    }
    prevBasis = b;
    havePrevBasis = true;

    if (spaceHold)
    {
        // Park the ball in front, where the next start ball will be, so it doesn't fall away.
        BallWrapper ball = server.GetBall();
        if (!ball.IsNull())
        {
            ball.SetLocation(BallSpot(GoalStartDir(kSpaceFacing, s.band), s));
            ball.SetVelocity(V(0.f, 0.f, 0.f));
            ball.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
        }
    }
    if (!active) return;

    // ---- changing mode, preset or aim restarts from the start ball ----
    if ((state == RunState::Ready || state == RunState::Running) &&
        (s.mode != runMode || s.preset != runPreset || s.aim != runAim || s.range != runRange || s.userPreset != runUserPreset || s.noRoll != runNoRoll))
    {
        cvarManager->log("[AirLabs] settings changed: back to the start ball.");
        EnterReady(car, s);
        return;
    }


    switch (state)
    {
    case RunState::Ready:
        ReadyTick(aim, s, dt);
        if (state == RunState::Running) BeginRunning(b, s);
        break;
    case RunState::Running:
        if (s.mode == kTrack)
        {
            TrackingStep(b, aim, s, dt);
        }
        else if (s.mode == kChase)
        {
            ChaseStep(b, aim, s, dt);
        }
        else if (s.mode == kHybrid && hybridTracking)
        {
            TrackingStep(b, aim, s, dt);
            hybTotal += dt;
            if (locking) hybOn += dt;
            if (gameTime >= hybridTrackEnd) EndHybridRep(s);
        }
        else
        {
            FlickTick(b, aim, s, dt);
        }
        if (state == RunState::Running && recording)
        {
            if (!tickTiming) ++recClockTicks;
            RecordTick(rot);
        }
        if (state == RunState::Running && gameTime >= runEnd) EndRun(car, b, s);
        break;
    case RunState::Replay:
        ReplayTick(car);
        break;
    default:
        break;
    }

    // ---- hold the ball where the drill wants it ----
    BallWrapper ball = server.GetBall();
    if (!ball.IsNull())
    {
        ball.SetLocation(targetLocation);
        ball.SetVelocity(V(0.f, 0.f, 0.f));
        ball.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
    }
}

void AirLabs::ReadyTick(const Vector& aim, const DrillSettings& s, float dt)
{
    aimErrorDeg = AngleDeg(aim, startDir);
    locking = aimErrorDeg <= s.target;
    readyHeld = locking ? readyHeld + dt : 0.f; // looking away resets the countdown
    if (gameTime < readyLockUntil) readyHeld = 0.f; // just after a run: a moment to read the results
    if (readyHeld >= kReadyHoldSec) state = RunState::Running; // BeginRunning follows in the tick
}

void AirLabs::FlickTick(const Basis& b, const Vector& aim, const DrillSettings& s, float dt)
{
    if (!hasTarget)
    {
        if (gameTime < nextSpawnAt) return;
        SpawnTarget(b, s);
    }

    Vector toTarget = Norm(Sub(targetLocation, pinLocation));
    float err = AngleDeg(aim, toTarget);
    aimErrorDeg = err;

    // Reaction time: from the target appearing until you start turning toward it.
    if (!reactionDone && dt > 0.f)
    {
        if (prevErrRaw >= 0.f && (prevErrRaw - err) / dt > 40.f)
        {
            reactionDone = true;
            run.sumReaction += gameTime - spawnTime;
            ++run.reactionN;
        }
        prevErrRaw = err;
    }

    if (guideOn) UpdateGuide(b, aim, toTarget, dt, s, false);

    bool inCone = err <= s.target;
    if (inCone && !reached)
    {
        reached = true;
        firstReach = gameTime;
        maxErrAfterReach = err;
    }
    if (reached) maxErrAfterReach = std::max(maxErrAfterReach, err);

    bool still = !s.settle || aimSpeedDeg <= s.settleRate;
    settling = inCone && !still;
    if (inCone && still)
    {
        if (!locking) { locking = true; lockStart = gameTime; }
        double held = gameTime - lockStart;
        holdProgress = s.holdSec > 0.f ? static_cast<float>(std::min(1.0, held / s.holdSec)) : 1.f;
        if (held >= s.holdSec)
        {
            FinishTarget(true, b, s);
            return;
        }
    }
    else
    {
        locking = false;
        holdProgress = 0.f;
    }

    if (s.timeout > 0.f && gameTime - spawnTime > s.timeout) FinishTarget(false, b, s);
}

void AirLabs::TrackingStep(const Basis& b, const Vector& aim, const DrillSettings& s, float dt)
{
    PathAdvance(path, rng, b, 0, s.path, gameTime, dt);
    targetLocation = BallSpot(path.pos, s);

    float err = AngleDeg(aim, path.pos);
    aimErrorDeg = err;
    bool on = err <= s.target;
    locking = on;

    // Score: 1000 points per second on target, partial credit just outside.
    run.score += 1000.0 * TrackCredit(err, s.target) * dt;
    run.trackTime += dt;
    run.errInt += err * dt;
    if (on) run.onTime += dt;
    if (err <= kNearDeg) run.nearTime += dt; // near: yellow or green

    // Reacquire time: how long each spell off the target lasts.
    if (!on) run.curOff += dt;
    else if (run.curOff > 0.0) { run.offSum += run.curOff; ++run.offN; run.curOff = 0.0; }

    if (s.path.dynamic)
    {
        int band = SpeedBand(path.speed);
        run.bandTotal[band] += dt;
        if (on) run.bandOn[band] += dt;
    }

    guideOn = s.custom && *guideMode != 0;
    if (guideOn) UpdateGuide(b, aim, path.pos, dt, s, true);
}

ChaseParams AirLabs::ChaseParamsFor(const DrillSettings& s)
{
    ChaseParams p;
    p.randomSpeed = s.path.dynamic;
    p.speed = s.path.speed;
    p.vMin = s.path.vMin;
    p.vMax = s.path.vMax;
    p.moveMin = s.chaseMin;
    p.moveMax = s.chaseMax;
    p.band = s.band;
    return p;
}

void AirLabs::ChaseStep(const Basis& b, const Vector& aim, const DrillSettings& s, float dt)
{
    const ChaseParams cp = ChaseParamsFor(s);
    const bool wasMoving = chase.moving;
    ChaseAdvance(chase, rng, b, cp, gameTime, dt);
    if (wasMoving && !chase.moving) RecEvent(kEvChaseStop);
    targetLocation = BallSpot(chase.path.pos, s);

    float err = AngleDeg(aim, chase.path.pos);
    aimErrorDeg = err;
    bool on = err <= s.target;
    locking = on;

    // Scored like Track: 1000 points per second on the ball, partial credit just outside.
    run.score += 1000.0 * TrackCredit(err, s.target) * dt;
    run.trackTime += dt;
    run.errInt += err * dt;
    if (on) run.onTime += dt;
    if (err <= kNearDeg) run.nearTime += dt; // near: yellow or green
    if (!on) run.curOff += dt;
    else if (run.curOff > 0.0) { run.offSum += run.curOff; ++run.offN; run.curOff = 0.0; }

    if (on) { if (chaseOnSince < 0.0) chaseOnSince = gameTime; }
    else chaseOnSince = -1.0;
    if (chase.moving) return;
    // The hold (kChaseHoldSec, 150 ms) only counts once the ball has stopped: being on it as it stops doesn't
    // send it off again straight away.
    if (chaseOnSince >= 0.0 && gameTime - std::max(chaseOnSince, chase.stoppedAt) >= kChaseHoldSec)
    {
        // Caught it (held your aim on it for a moment): how long it sat still before you got
        // there, then off it goes again.
        chaseOnSince = -1.0;
        double t = gameTime - chase.stoppedAt;
        ++run.targets;
        run.sumTime += t;
        run.lastTime = t;
        if (run.bestTime < 0.0 || t < run.bestTime) run.bestTime = t;
        lastFlash = Flash::Hit;
        RecEvent(kEvChaseCatch, static_cast<float>(t));
        ChaseLaunch(chase, rng, b, cp, gameTime);
        RecEvent(kEvChaseLaunch);
    }
    else if (s.timeout > 0.f && gameTime - chase.stoppedAt > s.timeout)
    {
        ++run.misses; // lost it: it moves on anyway
        lastFlash = Flash::Miss;
        RecEvent(kEvChaseMiss);
        ChaseLaunch(chase, rng, b, cp, gameTime);
        RecEvent(kEvChaseLaunch);
    }
}

void AirLabs::SpawnTarget(const Basis& b, const DrillSettings& s)
{
    SpawnResult r = SpawnDirection(rng, b, s.aim, s.minTurn, s.maxTurn, s.band, s.dirMask, RegionWeights(s));
    spawnFallback = r.fallback;
    targetLocation = BallSpot(r.dir, s);
    targetRegion = r.region;
    targetAngle = AngleDeg(AimVec(b, s.aim), r.dir);
    RecEvent(kEvSpawn, targetAngle);

    hasTarget = true;
    locking = false;
    holdProgress = 0.f;
    reached = false;
    settling = false;
    maxErrAfterReach = 0.f;
    spawnTime = gameTime;
    reactionDone = false;
    prevErrRaw = -1.f;
    guideOn = s.custom && *guideMode != 0;
    prevErr = -1.f;
    closingDeg = 0.f;
}

void AirLabs::FinishTarget(bool success, const Basis& b, const DrillSettings& s)
{
    if (success)
    {
        // Time from the target appearing to first being on it. With "must stop on target" it
        // runs until you were on it and still, so swinging through costs you. The hold
        // itself isn't counted.
        double reachT = (reached ? firstReach : lockStart) - spawnTime;
        double t = s.settle ? lockStart - spawnTime : reachT;
        const float pts = FlickPoints(t, targetAngle);
        run.score += pts;
        RecEvent(kEvHit, pts);
        ++run.targets;
        run.sumTime += t;
        run.lastTime = t;
        if (run.bestTime < 0.0 || t < run.bestTime) run.bestTime = t;
        ++run.streak;
        run.bestStreak = std::max(run.bestStreak, run.streak);
        run.sumOvershoot += maxErrAfterReach;
        ++run.overshootN;
        lastFlash = Flash::Hit;
        RecordRegion(true, t, s.aim);
    }
    else
    {
        ++run.misses;
        RecEvent(kEvMiss, static_cast<float>(std::min<double>(run.score, kMissPenalty)));
        run.score = std::max(0.0, run.score - kMissPenalty); // a miss costs points as well as time
        run.streak = 0;
        lastFlash = Flash::Miss;
        RecordRegion(false, std::max(1.f, s.timeout) * 1.5, s.aim); // a miss counts as a slow rep for weak-spot mode
    }

    hasTarget = false;
    locking = false;
    settling = false;
    holdProgress = 0.f;
    nextSpawnAt = gameTime + s.delaySec;

    // Hybrid: a successful flick launches the ball and the rep continues with tracking.
    if (success && s.mode == kHybrid) StartHybridTrack(b, s);
}

void AirLabs::StartHybridTrack(const Basis& b, const DrillSettings& s)
{
    PathStart(path, rng, Norm(Sub(targetLocation, pinLocation)), b, HybridPath(s.path), gameTime); // holds its take-off direction
    hybridTracking = true;
    RecEvent(kEvHybridStart);
    hybridTrackEnd = gameTime + s.hybridTrack;
    hybOn = hybTotal = 0.0;
    hasTarget = true;
    locking = true;
    prevErr = -1.f;
    closingDeg = 0.f;
}

void AirLabs::EndHybridRep(const DrillSettings& s)
{
    double pct = hybTotal > 0.0 ? 100.0 * hybOn / hybTotal : 0.0;
    RecEvent(kEvHybridEnd, static_cast<float>(pct));
    run.hybPctSum += pct;
    ++run.hybReps;
    run.lastHybPct = pct;
    hybridTracking = false;
    hasTarget = false;
    locking = false;
    nextSpawnAt = gameTime + s.delaySec;
}

float AirLabs::ArenaLimit(const Vector& from, const Vector& dir) const
{
    float limit = 1e9f;
    auto axis = [&](float p, float d, float lo, float hi) {
        if (d > 1e-4f)       limit = std::min(limit, (hi - p) / d);
        else if (d < -1e-4f) limit = std::min(limit, (lo - p) / d);
    };
    axis(from.X, dir.X, -kHalfWidth + kMargin, kHalfWidth - kMargin);
    axis(from.Y, dir.Y, -kHalfLength + kMargin, kHalfLength - kMargin);
    axis(from.Z, dir.Z, kMargin, kCeiling - kMargin);
    return limit;
}

Vector AirLabs::BallSpot(const Vector& dir, const DrillSettings& s) const
{
    // From the centre pin, every distance up to kMaxBallDistance fits; the arena limit is
    // only a safety net.
    float dist = std::max(150.f, std::min(s.distance, ArenaLimit(pinLocation, dir)));
    return Add(pinLocation, Scale(dir, dist));
}

// ===========================================================================
// Boost Lab (test): physics in BoostLab.h
// ===========================================================================

namespace
{
    constexpr float kDustBox = 1500.f; // the specks fill a cube this far out from the car each way
    constexpr float kGameDodgeTime = 1.25f; // Rocket League's time to flip after a jump (restored when the lab stops)
    constexpr int kDustCount = 110;
}

LabParams AirLabs::LabParamsNow() const
{
    LabParams p;
    p.motion = std::clamp(*labMotion, 0, 1);
    p.bubble = std::clamp(*labBubble, 300.f, 2000.f);
    p.pass = std::clamp(*labPass, 0.f, 600.f);
    p.speed = std::clamp(*labSpeed, 100.f, 2300.f);
    p.dynamic = *labDynamic;
    p.speedMin = std::clamp(*labSpeedMin, 100.f, 2300.f);
    p.speedMax = std::clamp(*labSpeedMax, 100.f, 2300.f);
    p.curve = std::clamp(*labCurve, 0.f, 120.f);
    p.hangSec = std::clamp(*labHang, 0.f, 3.f);
    p.spawnCone = std::clamp(*labCone, 10.f, 180.f);
    p.heightMin = std::clamp(*labHeightMin, -90.f, 90.f);
    p.heightMax = std::clamp(*labHeightMax, -90.f, 90.f);
    p.runSec = std::clamp(*labRun, 0.f, 600.f);
    p.steep = std::clamp(*labSteep, 0.f, 90.f);
    p.floor = *labFloor >= 1999.f ? 5000.f : std::clamp(*labFloor, 100.f, 2000.f);
    p.speedLimit = *labSpeedLimit;
    p.flips = std::clamp(*labFlips, 0, 2);
    return p;
}

void AirLabs::LabToggle()
{
    gameWrapper->Execute([this](GameWrapper*) {
        if (labOn) LabEnd();
        else LabBegin();
    });
}

void AirLabs::LabBegin()
{
    if (state != RunState::Idle) Stop(); // ends a run or a replay
    replayOver = false;
    labOn = true;
    labNeedsStart = true;
    labStartWall = WallSeconds();
    cvarManager->log("[AirLabs] Boost Lab started.");
    // Like Start: the space map unless you chose the standard field. The lab begins once the
    // map is up (the tick sets it up).
    const bool inFreeplay = gameWrapper->IsInFreeplay();
    spaceCheckWall = -10.0;
    if (WantSpaceMap() && !(inFreeplay && OnSpaceMap())) { LoadSpaceMap(); return; }
    if (!inFreeplay) LoadStandardMap();
}

void AirLabs::LabEnd()
{
    // Give the game its ball and car back as they were.
    if (gameWrapper->IsInFreeplay())
    {
        ServerWrapper server = gameWrapper->GetGameEventAsServer();
        if (!server.IsNull())
        {
            BallWrapper ball = server.GetBall();
            if (!ball.IsNull()) { ball.SetbFrozen(0); ball.SetbCollideActors(1); }
            CarWrapper car = server.GetGameCar();
            if (!car.IsNull()) car.SetMaxTimeForDodge(kGameDodgeTime);
        }
    }
    labOn = false;
    labNeedsStart = true;
    spaceStopped = true; // on the space map the car stays held, with the ball parked in front
    cvarManager->log("[AirLabs] Boost Lab stopped.");
}

void AirLabs::LabTick(CarWrapper car, ServerWrapper server, void* params, float dt)
{
    const LabParams p = LabParamsNow();

    // ---- input: everything goes to RocketSim, which flies your car. The game car only shows it.
    // Boost still reaches the game, so you see and hear it; jump doesn't (RocketSim does the flips).
    LabControls in;
    if (params)
    {
        auto* input = reinterpret_cast<ControllerInput*>(params);
        if (noAirRoll && *noAirRoll) input->Roll = 0.f;
        inPitch = input->Pitch;
        inYaw = input->Yaw;
        inRoll = input->Roll;
        in.throttle = input->Throttle;
        in.steer = input->Steer;
        in.pitch = input->Pitch;
        in.yaw = input->Yaw;
        in.roll = input->Roll;
        in.boost = input->HoldingBoost || input->ActivateBoost;
        in.jump = input->Jump != 0;
        in.handbrake = input->Handbrake != 0;
        if (in.jump && !labJumpHeld) ++labJumpPresses;
        labJumpHeld = in.jump;
        input->Jump = 0;
    }
    labBoosting = in.boost;

    // ---- did the game move its ball or turn its car since the last tick? (it shouldn't: its
    // ball is frozen and doesn't collide, and its car can't flip) ----
    if (labHavePlaced && labWorld.Ready())
    {
        BallWrapper gb = server.GetBall();
        if (!gb.IsNull())
        {
            const float moved = Len(Sub(gb.GetLocation(), labPlacedBall));
            if (moved > 5.f)
            {
                ++labGameMovedBall;
                if (gameTime - labLastDiagLog > 0.5)
                {
                    labLastDiagLog = gameTime;
                    cvarManager->log("[AirLabs] Boost Lab: the GAME moved its ball " + Fmt("%.0f uu", moved) + " since the last tick (it should be frozen)");
                }
            }
        }
        const Rotator nr = car.GetRotation();
        const Basis nb = BasisFromRotator(nr.Pitch, nr.Yaw, nr.Roll);
        const float turned = std::max(AngleDeg(nb.forward, labSetBasis.forward), AngleDeg(nb.up, labSetBasis.up));
        if (turned > 3.f)
        {
            ++labGameTurnedCar;
            if (gameTime - labLastDiagLog > 0.5)
            {
                labLastDiagLog = gameTime;
                cvarManager->log("[AirLabs] Boost Lab: the GAME turned the car " + Fmt("%.0f deg", turned) + " since the last tick (it shouldn't flip on its own)");
            }
        }
    }

    pinLocation = V(0.f, 0.f, kArenaCentreZ);
    if (labNeedsStart || !labWorld.Ready())
    {
        labNeedsStart = false;
        const int yaw = StartYaw(OnSpaceMap() ? kSpaceFacing : 1);
        havePrevBasis = false;
        // Your car's hitbox picks the closest RocketSim car (full sizes, or half on some game versions).
        Vector ext = car.GetLocalCollisionExtent();
        if (ext.X > 20.f && ext.X < 400.f && ext.Y > 10.f && ext.Z > 5.f) { if (ext.X < 90.f) ext = Scale(ext, 2.f); }
        else ext = V(118.f, 84.2f, 36.16f);
        labWorld.Start(p, BasisFromRotator(0, yaw, 0), ext, rng, gameTime);
        lab = labWorld.s;
        cvarManager->log(std::string("[AirLabs] Boost Lab: RocketSim running, car ") + labWorld.carName +
                         Fmt(" (your car's hitbox %.1f", ext.X) + Fmt(" x %.1f", ext.Y) + Fmt(" x %.1f)", ext.Z));
        std::uniform_real_distribution<float> u(-kDustBox, kDustBox);
        labSpecks.clear();
        for (int i = 0; i < kDustCount; ++i) labSpecks.push_back(V(u(rng), u(rng), u(rng)));
        labTouchFlash = labMissFlash = labResetFlash = -10.0;
        labJumpPresses = 0;
        labHavePlaced = false;
        labGameMovedBall = labGameTurnedCar = labBallJumps = 0;
    }

    // A finished run: boost to go again (after a moment to read it).
    if (lab.phase == kLabDone && in.boost && gameTime - lab.runEnd > 1.0)
    {
        labWorld.Start(p, labWorld.CarBasis(), V(118.f, 84.2f, 36.16f), rng, gameTime);
        labTouchFlash = labMissFlash = labResetFlash = -10.0;
    }

    // ---- RocketSim runs at 120 ticks a second, like the game: one step per physics tick ----
    const int steps = tickTiming ? 1 : std::clamp(static_cast<int>(std::lround(dt * 120.f)), 1, 4);
    for (int i = 0; i < steps; ++i)
    {
        const Vector relBefore = labWorld.s.ballPos;
        const int ballsBefore = labWorld.s.balls, bouncesBefore = labWorld.s.bounces;
        labWorld.Step(p, in, rng, gameTime - (steps - 1 - i) / 120.0);
        // RocketSim's ball jumping to a huge speed relative to you (not a new ball or a bounce): log it.
        const float relSpeed = Len(Sub(labWorld.s.ballPos, relBefore)) * 120.f;
        if (relSpeed > 6000.f && labWorld.s.balls == ballsBefore && labWorld.s.bounces == bouncesBefore)
        {
            ++labBallJumps;
            const RocketSim::CarState cs = labWorld.GetCarState();
            cvarManager->log("[AirLabs] Boost Lab: RocketSim's ball jumped at " + Fmt("%.0f uu/s", relSpeed) +
                             Fmt(" (car %.0f", Len(labWorld.s.carVel)) + Fmt(", ball %.0f", Len(labWorld.s.ballVel)) +
                             Fmt(", spin %.1f", cs.angVel.Length()) + Fmt(", %.0f ticks this call", steps) +
                             Fmt("). Input: pitch %.2f", in.pitch) + Fmt(" yaw %.2f", in.yaw) + Fmt(" roll %.2f", in.roll) +
                             Fmt(" throttle %.2f", in.throttle) + (in.jump ? " jump" : "") + (in.boost ? " boost" : "") + (in.handbrake ? " handbrake" : ""));
        }
        if (labWorld.s.touchedNow) labTouchFlash = gameTime;
        if (labWorld.s.missedNow) labMissFlash = gameTime;
        if (labWorld.resetNow) labResetFlash = gameTime;
    }
    lab = labWorld.s;

    // ---- the game car shows RocketSim's car: its rotation, held in place ----
    const Basis b = labWorld.CarBasis();
    int rp, ry, rr;
    RotatorFromBasis(b, rp, ry, rr);
    car.SetRotation(Rotator(rp, ry, rr));
    car.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
    labSetBasis = BasisFromRotator(rp, ry, rr);
    car.SetLocation(pinLocation);
    car.SetVelocity(V(0.f, 0.f, 0.f));
    lastAim = b.forward;
    // The game car must never flip on its own (RocketSim does the flips): with the car pinned, a
    // flip of the game's own fights RocketSim. So the game always thinks its flip is used up.
    car.SetbJumped(1);
    car.SetbDoubleJumped(1);
    car.SetMaxTimeForDodge(0.f);
    BoostWrapper bw = car.GetBoostComponent();
    if (!bw.IsNull() && bw.GetCurrentBoostAmount() < 0.5f) bw.SetCurrentBoostAmount(1.f); // the flames never run out

    // The specks drift opposite to your movement; one that leaves the cube comes back in on
    // the far side, so there are always some around you.
    if (*labDust)
    {
        const Vector shift = Scale(lab.carVel, -dt);
        auto wrap = [](float v) { return v > kDustBox ? v - 2.f * kDustBox : v < -kDustBox ? v + 2.f * kDustBox : v; };
        for (Vector& d : labSpecks)
        {
            d = Add(d, shift);
            d = V(wrap(d.X), wrap(d.Y), wrap(d.Z));
        }
    }

    // ---- the ball, where it is relative to you ----
    BallWrapper ball = server.GetBall();
    if (!ball.IsNull())
    {
        Vector rel = lab.ballPos;
        if (!OnSpaceMap()) // the standard field has walls, a floor and a roof: keep it inside
        {
            float l = Len(rel), lim = ArenaLimit(pinLocation, Norm(rel));
            if (l > lim) rel = Scale(Norm(rel), lim);
        }
        // The game's own ball is frozen (as before a kickoff: visible, its own physics stopped)
        // and doesn't collide: RocketSim alone decides where it goes. (Touching, the car and ball
        // overlap by a fraction of a uu for a tick or two, as in any physics engine; without this
        // the game sees its car inside its ball and fires the ball off.)
        ball.SetbFrozen(1);
        ball.SetbCollideActors(0);
        ball.SetLocation(Add(pinLocation, rel));
        ball.SetVelocity(V(0.f, 0.f, 0.f));
        ball.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
        labPlacedBall = ball.GetLocation();
        labHavePlaced = true;
    }
}

namespace
{
    const LinearColor kColMarker(80.f, 235.f, 160.f, 240.f);  // relative prograde / retrograde
    const LinearColor kColDrift(90.f, 200.f, 255.f, 230.f);   // your own drift (navball)

    void Ring(CanvasWrapper& canvas, Vector2F c, float r, float thick, int seg = 28)
    {
        for (int i = 0; i < seg; ++i)
        {
            float a0 = 2.f * kPi * i / seg, a1 = 2.f * kPi * (i + 1) / seg;
            canvas.DrawLine(Vector2F{ c.X + std::cos(a0) * r, c.Y + std::sin(a0) * r }, Vector2F{ c.X + std::cos(a1) * r, c.Y + std::sin(a1) * r }, thick);
        }
    }
    // Prograde: a circle with three ticks (up, left, right). Retrograde: a circle with an X.
    void ProgradeMark(CanvasWrapper& canvas, Vector2F m, float r, float thick)
    {
        Ring(canvas, m, r, thick, 20);
        canvas.DrawLine(Vector2F{ m.X, m.Y - r }, Vector2F{ m.X, m.Y - r * 1.8f }, thick);
        canvas.DrawLine(Vector2F{ m.X - r, m.Y }, Vector2F{ m.X - r * 1.8f, m.Y }, thick);
        canvas.DrawLine(Vector2F{ m.X + r, m.Y }, Vector2F{ m.X + r * 1.8f, m.Y }, thick);
    }
    void RetrogradeMark(CanvasWrapper& canvas, Vector2F m, float r, float thick)
    {
        Ring(canvas, m, r, thick, 20);
        const float k = r * 0.65f;
        canvas.DrawLine(Vector2F{ m.X - k, m.Y - k }, Vector2F{ m.X + k, m.Y + k }, thick);
        canvas.DrawLine(Vector2F{ m.X + k, m.Y - k }, Vector2F{ m.X - k, m.Y + k }, thick);
    }
}

void AirLabs::DrawLab(CanvasWrapper& canvas, const Vector2& screen)
{
    const float speed = Len(lab.carVel);
    const Vector rel = LabRelVel(lab);               // the ball's velocity as you see it
    const Vector you = Scale(rel, -1.f);             // yours, relative to the ball
    const float relSpeed = Len(rel);
    const float dist = Len(lab.ballPos);
    const Vector toBall = dist > 1.f ? Scale(lab.ballPos, 1.f / dist) : V(1.f, 0.f, 0.f);
    const float closing = Dot(you, toBall);
    CameraWrapper cam = gameWrapper->GetCamera();
    if (!cam.IsNull())
    {
        const Vector camLoc = cam.GetLocation();
        const Rotator cr = cam.GetRotation();
        const Basis cb = BasisFromRotator(cr.Pitch, cr.Yaw, cr.Roll);
        auto inFront = [&](const Vector& w) { return Dot(Sub(w, camLoc), cb.forward) > 20.f; };
        auto onScreen = [&](const Vector& w, Vector2F& out) {
            if (!inFront(w)) return false;
            out = canvas.ProjectF(w);
            return out.X >= 0.f && out.Y >= 0.f && out.X <= screen.X && out.Y <= screen.Y;
        };

        // ---- dust (optional): specks streaking past, so you can see your own movement ----
        if (*labDust)
        {
            const float streak = std::min(0.06f, 0.03f + speed / 60000.f);
            for (const Vector& d : labSpecks)
            {
                const float fade = std::clamp(1.f - Len(d) / kDustBox, 0.f, 1.f);
                if (fade <= 0.02f) continue;
                const Vector w = Add(pinLocation, d);
                const Vector tail = Add(w, Scale(lab.carVel, streak));
                if (!inFront(w) || !inFront(tail)) continue;
                Vector2F a = canvas.ProjectF(w), t = canvas.ProjectF(tail);
                canvas.SetColor(LinearColor(200.f, 220.f, 255.f, 40.f + 170.f * fade));
                if (speed < 40.f) { canvas.SetPosition(Vector2F{ a.X - 1.f, a.Y - 1.f }); canvas.FillBox(Vector2F{ 2.f, 2.f }); }
                else canvas.DrawLine(a, t, 1.5f + fade);
            }
        }

        // ---- relative markers: where you're heading relative to the ball (put it on the ball
        // and you'll hit it), and the opposite way (boost there to slow your approach). Drawn at
        // the ball's distance, so "on the ball" lines up exactly on screen.
        if (*labMarkers && relSpeed > 30.f && lab.phase != kLabDone)
        {
            const Vector dir = Scale(you, 1.f / relSpeed);
            const float at = std::max(300.f, dist);
            Vector2F m;
            canvas.SetColor(kColMarker);
            if (onScreen(Add(pinLocation, Scale(dir, at)), m))
            {
                ProgradeMark(canvas, m, 11.f, 2.f);
                canvas.SetPosition(Vector2F{ m.X - 16.f, m.Y + 16.f });
                canvas.DrawString(Fmt("%.0f", relSpeed), 1.f, 1.f, true);
            }
            canvas.SetColor(LinearColor(kColMarker.R, kColMarker.G, kColMarker.B, 150.f));
            if (onScreen(Add(pinLocation, Scale(dir, -at)), m)) RetrogradeMark(canvas, m, 11.f, 2.f);
        }

        // ---- the ball: a ring around it (or an arrow at the screen's edge), and while it
        // waits, an arrow showing which way it's about to go ----
        const Vector ballW = Add(pinLocation, lab.ballPos);
        LinearColor ringCol = lab.phase == kLabHang ? kColWarn : lab.phase == kLabTouched ? kColGood : LinearColor(255.f, 255.f, 255.f, 200.f);
        if (gameTime - labTouchFlash < 0.35) ringCol = kColGood;
        Vector2F bc;
        if (lab.phase == kLabDone) {}
        else if (onScreen(ballW, bc))
        {
            if (lab.phase == kLabHang) // only a waiting ball gets a ring (yellow, pulsing); a flying ball has none
            {
                Vector2F e = canvas.ProjectF(Add(ballW, Scale(cb.right, kLabBallRadius + 12.f)));
                float r = std::clamp(std::sqrt((e.X - bc.X) * (e.X - bc.X) + (e.Y - bc.Y) * (e.Y - bc.Y)), 8.f, 500.f);
                r *= 1.f + 0.12f * static_cast<float>(std::sin(gameTime * 12.0));
                canvas.SetColor(ringCol);
                Ring(canvas, bc, r, 2.5f);
            }
        }
        else
        {
            Vector relCam = Sub(ballW, camLoc);
            float x = Dot(relCam, cb.right), y = -Dot(relCam, cb.up); // screen y grows downwards
            float l = std::sqrt(x * x + y * y);
            if (l < 1e-3f) { x = 0.f; y = 1.f; l = 1.f; }
            x /= l; y /= l;
            const float cx = screen.X * 0.5f, cy = screen.Y * 0.5f, rx = screen.X * 0.42f, ry = screen.Y * 0.40f;
            Vector2F tip{ cx + x * rx, cy + y * ry };
            Vector2F side{ -y * 11.f, x * 11.f };
            Vector2F base{ tip.X - x * 22.f, tip.Y - y * 22.f };
            canvas.SetColor(ringCol);
            canvas.DrawLine(tip, Vector2F{ base.X + side.X, base.Y + side.Y }, 3.f);
            canvas.DrawLine(tip, Vector2F{ base.X - side.X, base.Y - side.Y }, 3.f);
            canvas.DrawLine(Vector2F{ base.X + side.X, base.Y + side.Y }, Vector2F{ base.X - side.X, base.Y - side.Y }, 3.f);
        }
        if (lab.phase == kLabHang)
        {
            const Vector a0 = Add(ballW, Scale(lab.launchDir, kLabBallRadius + 15.f));
            const Vector a1 = Add(ballW, Scale(lab.launchDir, kLabBallRadius + 15.f + 120.f + lab.launchSpeed * 0.2f));
            if (inFront(a0) && inFront(a1))
            {
                Vector2F p0 = canvas.ProjectF(a0), p1 = canvas.ProjectF(a1);
                float dx = p1.X - p0.X, dy = p1.Y - p0.Y, l = std::sqrt(dx * dx + dy * dy);
                if (l > 4.f)
                {
                    dx /= l; dy /= l;
                    const float h = std::min(18.f, l * 0.4f);
                    canvas.SetColor(kColWarn);
                    canvas.DrawLine(p0, p1, 4.f);
                    canvas.DrawLine(p1, Vector2F{ p1.X - dx * h - dy * h * 0.6f, p1.Y - dy * h + dx * h * 0.6f }, 4.f);
                    canvas.DrawLine(p1, Vector2F{ p1.X - dx * h + dy * h * 0.6f, p1.Y - dy * h - dx * h * 0.6f }, 4.f);
                }
            }
        }
    }

    if (*labNavball) DrawNavball(canvas, screen);

    // ---- panel ----
    std::vector<HudLine> L;
    auto add = [&](const std::string& t, const LinearColor& c = kColText, float sc = 1.3f) { L.push_back({ t, c, sc }); };
    const LabParams p = LabParamsNow();
    add(std::string("BOOST LAB (TEST)  |  ") + (p.motion == kLabConstant ? "Constant" : "Respawn"), kColTitle, 1.4f);
    if (lab.phase == kLabDone)
    {
        add("RUN OVER", kColGold, 1.6f);
        add(p.motion == kLabConstant ? Fmt("Touches  %.0f", lab.touches) : Fmt("Touches  %.0f", lab.touches) + Fmt("    Got away  %.0f", lab.misses));
        add("Boost to go again.", kColDim, 1.15f);
    }
    else
    {
        add(Fmt("Your speed   %.0f uu/s", speed) + (labBoosting ? "   BOOST" : ""), labBoosting ? kColWarn : kColText);
        add(Fmt("Ball         %.0f uu away", dist) + (closing >= 0.f ? Fmt(", closing at %.0f uu/s", closing) : Fmt(", pulling away at %.0f uu/s", -closing)));
        add(p.motion == kLabConstant ? Fmt("Touches  %.0f", lab.touches) : Fmt("Touches  %.0f", lab.touches) + Fmt("    Got away  %.0f", lab.misses));
        if (lab.runEnd < 1e17) add(Fmt("Time left  %.0f s", std::max(0.0, std::min(static_cast<double>(p.runSec), lab.runEnd - gameTime))));
        if (lab.touches > 0) add(Fmt("Last touch: met it at %.0f uu/s", lab.lastClosing) + Fmt(", it left you at %.0f", lab.lastBallSpeed), kColDim, 1.15f);
        // Boosting at a ball that's faster than a car can go: say so, so it doesn't look broken.
        if (*labDebug)
        {
            add(Fmt("Debug: jump presses %.0f", labJumpPresses) + Fmt(", flip resets %.0f", labWorld.flipResets) +
                (labWorld.carHasFlip ? ", you have a flip" : ", no flip") + std::string(", car: ") + labWorld.carName + Fmt(", wheels on the ball %.0f", labWorld.wheelsOnBall) +
                Fmt(", car spin %.1f rad/s", labWorld.Ready() ? labWorld.GetCarState().angVel.Length() : 0.f), kColDim, 1.0f);
            add(Fmt("Debug: the game moved its ball %.0f", labGameMovedBall) + Fmt(" times, turned the car %.0f", labGameTurnedCar) +
                Fmt(" times; RocketSim ball jumps %.0f", labBallJumps) + Fmt("; physics %.0f ticks/s", measuredRate), kColDim, 1.0f);
        }
        if (p.flips == kLabFlipsInfinite) add("Flips: infinite", kColDim, 1.15f);
        else if (p.flips == kLabFlipsReal)
            add(lab.flipReady ? "Flip: ready" : "Flip: used  (wheels on the ball for a flip reset)", lab.flipReady ? kColGood : kColDim, 1.15f);
        if (p.speedLimit && labBoosting && speed > kCarTopSpeed - 10.f && closing < 0.f && Dot(Scale(lab.carVel, 1.f / speed), toBall) > 0.9f)
            add(Fmt("The ball's faster than your top speed (2300): it's at %.0f", Len(lab.ballVel)) + " and slowing", kColWarn, 1.15f);
        add("Hold boost to fly. Let go and you keep drifting.", kColDim, 1.05f);
    }
    add(menuKey.empty() ? "Stop: the AirLabs window, or airlabs_lab" : "Stop: " + menuKey + " for the menu", kColDim, 1.05f);
    const int w = 500, x = 30, y = static_cast<int>(screen.Y * 0.12f);
    FillRect(canvas, x - 12, y - 10, w, LinesHeight(L) + 16, kColPanel);
    DrawLines(canvas, x, y, L);

    // ---- flashes ----
    auto centred = [&](const std::string& t, const LinearColor& c, float scale, int yy) {
        Vector2F sz = canvas.GetStringSize(t, scale, scale);
        canvas.SetColor(c);
        canvas.SetPosition(Vector2F{ (screen.X - sz.X) * 0.5f, static_cast<float>(yy) });
        canvas.DrawString(t, scale, scale, true);
    };
    if (gameTime - labResetFlash < 0.8) centred("FLIP RESET", kColTitle, 2.2f, static_cast<int>(screen.Y * 0.28f));
    if (gameTime - labTouchFlash < 0.5) centred(Fmt("TOUCH  %.0f uu/s", lab.lastClosing), kColGood, 2.2f, static_cast<int>(screen.Y * 0.22f));
    else if (gameTime - labMissFlash < 0.7) centred("GOT AWAY", kColBad, 2.2f, static_cast<int>(screen.Y * 0.22f));
}

// The navball: every direction around you on one disc. The middle is where your nose points,
// the rim is straight behind you, and the distance from the middle is the angle from your nose
// (so a marker halfway out is 90 degrees off). Sky and ground are split by the horizon (the
// space map's "up" is the same as the standard field's).
void AirLabs::DrawNavball(CanvasWrapper& canvas, const Vector2& screen)
{
    ServerWrapper server = gameWrapper->GetGameEventAsServer();
    if (server.IsNull()) return;
    CarWrapper car = server.GetGameCar();
    if (car.IsNull()) return;
    const Rotator rot = car.GetRotation();
    const Basis b = BasisFromRotator(rot.Pitch, rot.Yaw, rot.Roll);
    const float R = std::clamp(screen.Y * 0.09f, 70.f, 130.f);
    const Vector2F c{ screen.X * 0.5f, screen.Y - R - 30.f };

    auto map = [&](const Vector& world) {
        const Vector d = Norm(world);
        const float x = Dot(d, b.right), y = Dot(d, b.up), z = Dot(d, b.forward);
        const float th = std::acos(std::clamp(z, -1.f, 1.f));
        const float pl = std::sqrt(x * x + y * y);
        const float r = R * th / kPi;
        if (pl < 1e-5f) return Vector2F{ c.X, c.Y - (z > 0.f ? 0.f : r) };
        return Vector2F{ c.X + r * x / pl, c.Y - r * y / pl };
    };

    // Sky and ground, in small squares: the direction under each square decides its colour.
    const float cell = std::max(5.f, R / 16.f);
    for (float py = -R; py < R; py += cell)
        for (float px = -R; px < R; px += cell)
        {
            const float mx = px + cell * 0.5f, my = py + cell * 0.5f;
            const float rr = std::sqrt(mx * mx + my * my);
            if (rr > R) continue;
            const float th = kPi * rr / R;
            const float ux = rr > 1e-4f ? mx / rr : 0.f, uy = rr > 1e-4f ? -my / rr : 0.f;
            const Vector d = Add(Scale(b.forward, std::cos(th)), Scale(Add(Scale(b.right, ux), Scale(b.up, uy)), std::sin(th)));
            const float shade = 0.75f + 0.25f * (1.f - rr / R); // darker towards the rim (behind you)
            canvas.SetColor(d.Z >= 0.f ? LinearColor(60.f * shade, 110.f * shade, 170.f * shade, 190.f)
                                       : LinearColor(120.f * shade, 85.f * shade, 55.f * shade, 190.f));
            canvas.SetPosition(Vector2F{ c.X + px, c.Y + py });
            canvas.FillBox(Vector2F{ cell + 0.5f, cell + 0.5f });
        }
    // The horizon line.
    canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 200.f));
    Vector2F prev{};
    bool havePrev = false;
    for (int i = 0; i <= 90; ++i)
    {
        const float a = 2.f * kPi * i / 90.f;
        const Vector2F q = map(V(std::cos(a), std::sin(a), 0.f));
        if (havePrev && std::fabs(q.X - prev.X) + std::fabs(q.Y - prev.Y) < R * 0.6f) canvas.DrawLine(prev, q, 1.5f);
        prev = q;
        havePrev = true;
    }
    canvas.SetColor(LinearColor(220.f, 228.f, 240.f, 230.f));
    Ring(canvas, c, R, 2.f, 48);
    Ring(canvas, c, R * 0.5f, 1.f, 36); // 90 degrees off your nose

    // Your nose, in the middle.
    canvas.SetColor(kColWarn);
    canvas.DrawLine(Vector2F{ c.X - 16.f, c.Y }, Vector2F{ c.X - 5.f, c.Y }, 2.5f);
    canvas.DrawLine(Vector2F{ c.X + 5.f, c.Y }, Vector2F{ c.X + 16.f, c.Y }, 2.5f);
    canvas.DrawLine(Vector2F{ c.X, c.Y - 5.f }, Vector2F{ c.X, c.Y + 5.f }, 2.5f);

    // The markers: your drift (small blue dot), the relative markers, and the ball.
    if (lab.phase != kLabDone)
    {
        if (Len(lab.carVel) > 50.f)
        {
            const Vector2F m = map(lab.carVel);
            canvas.SetColor(kColDrift);
            canvas.SetPosition(Vector2F{ m.X - 3.f, m.Y - 3.f });
            canvas.FillBox(Vector2F{ 6.f, 6.f });
        }
        const Vector you = Scale(LabRelVel(lab), -1.f);
        if (Len(you) > 30.f)
        {
            canvas.SetColor(kColMarker);
            ProgradeMark(canvas, map(you), 7.f, 2.f);
            canvas.SetColor(LinearColor(kColMarker.R, kColMarker.G, kColMarker.B, 160.f));
            RetrogradeMark(canvas, map(Scale(you, -1.f)), 7.f, 2.f);
        }
        const Vector2F bm = map(lab.ballPos);
        canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 255.f));
        Ring(canvas, bm, 8.f, 2.5f, 20);
        canvas.SetPosition(Vector2F{ bm.X - 2.f, bm.Y - 2.f });
        canvas.FillBox(Vector2F{ 4.f, 4.f });
    }
    canvas.SetColor(kColDim);
    const std::string hint = "middle = nose, rim = behind you";
    Vector2F hs = canvas.GetStringSize(hint, 0.9f, 0.9f);
    canvas.SetPosition(Vector2F{ c.X - hs.X * 0.5f, c.Y + R + 6.f });
    canvas.DrawString(hint, 0.9f, 0.9f, true);
}

// ---- simulated player feasibility check ----

void AirLabs::BotProbeStart()
{
    auto alive = lbAlive;
    gameWrapper->Execute([this, alive](GameWrapper* gw) {
        if (!gw->IsInFreeplay()) { cvarManager->log("[AirLabs] bot probe: go to Freeplay (or the space map) first."); return; }
        ServerWrapper server = gw->GetGameEventAsServer();
        if (server.IsNull()) { cvarManager->log("[AirLabs] bot probe: no game."); return; }
        cvarManager->log(std::string("[AirLabs] bot probe: spawning a bot (CanSpawnBots says ") + (server.CanSpawnBots() ? "yes" : "no") + ")");
        server.SpawnBot(23, "AirLabs Bot"); // 23 = Octane
        botProbeUntil = WallSeconds() + 20.0;
        botProbeTicks = 0;
        for (float at : { 1.f, 4.f, 10.f, 19.f })
        {
            gw->SetTimeout([this, alive, at](GameWrapper* g) {
                if (!alive->load()) return;
                ServerWrapper sv = g->GetGameEventAsServer();
                if (sv.IsNull()) { cvarManager->log("[AirLabs] bot probe: game gone"); return; }
                CarWrapper mine = sv.GetGameCar();
                ArrayWrapper<CarWrapper> cars = sv.GetCars();
                std::string line = "[AirLabs] bot probe at " + Fmt("%.0f s", at) + Fmt(": %.0f cars, ", cars.Count()) + Fmt("%.0f ticks held", botProbeTicks);
                for (int i = 0; i < cars.Count(); ++i)
                {
                    CarWrapper cw = cars.Get(i);
                    if (cw.IsNull()) continue;
                    Vector l = cw.GetLocation();
                    Rotator r = cw.GetRotation();
                    line += std::string("; ") + (!mine.IsNull() && cw.memory_address == mine.memory_address ? "you" : "bot") +
                            Fmt(" at %.0f", l.X) + Fmt(" %.0f", l.Y) + Fmt(" %.0f", l.Z) + Fmt(" yaw %.0f", r.Yaw);
                }
                cvarManager->log(line);
            }, at);
        }
    });
}

// Holds the bot beside you (it should stand still at the spot, turning slowly), with its own
// controls switched off. If the log shows it at that spot and turning, a simulated player works.
void AirLabs::BotProbeTick(CarWrapper bot, void* params)
{
    if (params)
    {
        auto* input = reinterpret_cast<ControllerInput*>(params);
        *input = ControllerInput{};
    }
    ++botProbeTicks;
    bot.SetLocation(V(350.f, -500.f, kArenaCentreZ + 100.f));
    bot.SetVelocity(V(0.f, 0.f, 0.f));
    bot.SetRotation(Rotator(0, (botProbeTicks * 90) % 65536, 0)); // a full turn every ~6 s
    bot.SetAngularVelocity(V(0.f, 0.f, 0.f), false);
}

// ===========================================================================
// Direction grid and weak-spot mode
// ===========================================================================

std::array<double, kRegionCount> AirLabs::RegionWeights(const DrillSettings& s) const
{
    std::array<double, kRegionCount> w;
    w.fill(1.0);
    if (!s.weakSpot) return w;

    const RegionStat* stats = regionStats[std::clamp(s.aim, 0, 1)];
    double mean = 0.0;
    int n = 0;
    for (int i = 0; i < kRegionCount; ++i)
        if (stats[i].samples >= 3) { mean += stats[i].avgNorm; ++n; }
    if (n == 0) return w;
    mean /= n;
    for (int i = 0; i < kRegionCount; ++i)
    {
        if (stats[i].samples < 3) { w[i] = 2.0; continue; }        // not enough reps yet: explore it
        double ratio = stats[i].avgNorm / std::max(1e-3, mean);
        w[i] = std::clamp(ratio * ratio, 0.25, 4.0);                 // slower than average = more reps
    }
    return w;
}

void AirLabs::RecordRegion(bool hit, double seconds, int aim)
{
    if (targetRegion < 0 || targetRegion >= kRegionCount) return;
    RegionStat& s = regionStats[std::clamp(aim, 0, 1)][targetRegion];
    // Plain average for the first 5 reps, then a moving average that favours recent reps.
    float norm = NormalisedFlickTime(seconds, targetAngle);
    ++s.samples;
    float a = s.samples <= 5 ? 1.f / s.samples : 0.2f;
    s.avgNorm += a * (norm - s.avgNorm);
    if (hit)
    {
        ++s.hits;
        float ar = s.hits <= 5 ? 1.f / s.hits : 0.2f;
        s.avgRaw += ar * (static_cast<float>(seconds) - s.avgRaw);
    }
    SaveRegionStats();
}

std::filesystem::path AirLabs::DataDir() const
{
    // GetDataFolderW is always declared by the SDK; GetDataFolder depends on include order.
    return std::filesystem::path(gameWrapper->GetDataFolderW()) / "AirLabs";
}

std::filesystem::path AirLabs::SpaceMapPath() const
{
    return DataDir() / "AirLabs_Space.udk";
}

bool AirLabs::OnSpaceMap()
{
    const double now = WallSeconds();
    if (now - spaceCheckWall > 1.0)
    {
        spaceCheckWall = now;
        std::string m = gameWrapper->GetCurrentMap();
        std::transform(m.begin(), m.end(), m.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        spaceMap = m.find("airlabs_space") != std::string::npos;
    }
    return spaceMap;
}

#ifdef _WIN32
// From kernel32 (always linked); declared here so AirLabs.cpp doesn't pull in windows.h.
extern "C" __declspec(dllimport) unsigned long __stdcall GetShortPathNameW(const wchar_t* longPath, wchar_t* shortPath, unsigned long size);
#endif

namespace
{
    bool HasSpace(const std::string& s) { return s.find(' ') != std::string::npos; }
}

// The game's "start <map>" command can't take a path with spaces in it, and the map lives
// under your Windows user folder (C:\Users\<name>\AppData\...), so a user name like
// "Jane Doe" would break it. Returns a space-free path to the map, or "" if there's no way:
//  1. the path as it is, if it has no spaces;
//  2. the folder's short (8.3) form, C:\Users\JANEDO~1\..., with the file name kept, since the
//     game names the map after its file (AirLabs_Space) and that's how AirLabs recognises it;
//  3. a copy in C:\ProgramData\AirLabs (made once, refreshed if the map changes).
std::string AirLabs::SpaceMapUrlPath(const std::filesystem::path& map)
{
    const std::string full = map.string();
    if (!HasSpace(full)) return full;
    std::error_code ec;
#ifdef _WIN32
    {
        const std::wstring dir = map.parent_path().wstring();
        wchar_t buf[1024];
        const unsigned long n = GetShortPathNameW(dir.c_str(), buf, 1024);
        if (n > 0 && n < 1024)
        {
            const std::string shortPath = (std::filesystem::path(std::wstring(buf, n)) / map.filename()).string();
            if (!HasSpace(shortPath) && std::filesystem::exists(shortPath, ec)) return shortPath;
        }
    }
#endif
    if (const char* pd = std::getenv("ProgramData"))
    {
        const std::filesystem::path dir = std::filesystem::path(pd) / "AirLabs";
        const std::filesystem::path copy = dir / map.filename();
        if (!HasSpace(copy.string()))
        {
            std::filesystem::create_directories(dir, ec);
            const bool fresh = std::filesystem::exists(copy, ec) &&
                               std::filesystem::file_size(copy, ec) == std::filesystem::file_size(map, ec) &&
                               std::filesystem::last_write_time(copy, ec) >= std::filesystem::last_write_time(map, ec);
            if (fresh) return copy.string();
            ec.clear();
            std::filesystem::copy_file(map, copy, std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) return copy.string();
            cvarManager->log("[AirLabs] couldn't copy the space map to " + copy.string() + ": " + ec.message());
        }
    }
    return "";
}

void AirLabs::LoadSpaceMap()
{
    std::error_code ec;
    const std::filesystem::path p = SpaceMapPath();
    haveSpaceMap = std::filesystem::exists(p, ec);
    if (!haveSpaceMap)
    {
        cvarManager->log("[AirLabs] space map not found: " + p.string());
        return;
    }
    const std::string path = SpaceMapUrlPath(p);
    if (path.empty())
    {
        cvarManager->log("[AirLabs] can't open the space map: its path has spaces in it (" + p.string() + ")");
        return;
    }
    // The game's own "start <map>?options" command.
    const std::string cmd = "start " + path + "?Game=TAGame.GameInfo_Soccar_TA?GameTags=Freeplay";
    gameWrapper->Execute([this, cmd](GameWrapper* gw) {
        spaceStopped = false;
        spaceCheckWall = -10.0;
        gw->ExecuteUnrealCommand(cmd);
    });
}

// The standard field: Freeplay on DFH Stadium, the game's own default Freeplay arena.
void AirLabs::LoadStandardMap()
{
    const std::string cmd = "start Stadium_P?Game=TAGame.GameInfo_Soccar_TA?GameTags=Freeplay";
    gameWrapper->Execute([this, cmd](GameWrapper* gw) {
        spaceCheckWall = -10.0;
        gw->ExecuteUnrealCommand(cmd);
    });
}

bool AirLabs::WantSpaceMap() const
{
    return haveSpaceMap && (!mapPref || *mapPref == 0);
}

void AirLabs::MigrateOldData()
{
    // Before 2.0 the plugin was called Orientation Trainer and kept its files in
    // data/OrientationTrainer. Copy them across once; the old folder is left as it was.
    try
    {
        std::filesystem::path oldDir = std::filesystem::path(gameWrapper->GetDataFolderW()) / "OrientationTrainer";
        std::filesystem::path newDir = DataDir();
        if (!std::filesystem::exists(oldDir) || std::filesystem::exists(newDir / "runs.csv")) return;
        std::filesystem::create_directories(newDir);
        int copied = 0;
        for (const char* f : { "runs.csv", "presets.txt", "direction_stats_v2.txt" })
            if (std::filesystem::exists(oldDir / f) && !std::filesystem::exists(newDir / f))
            {
                std::filesystem::copy_file(oldDir / f, newDir / f);
                ++copied;
            }
        if (copied) cvarManager->log("[AirLabs] brought over your runs and settings from Orientation Trainer.");
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't copy the old Orientation Trainer files."); }
}

void AirLabs::LoadRegionStats()
{
    try
    {
        std::ifstream in(DataDir() / "direction_stats_v2.txt");
        if (!in) return;
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ss(line);
            int aim, region;
            RegionStat s;
            if (!(ss >> aim >> region >> s.samples >> s.avgNorm >> s.hits >> s.avgRaw)) continue;
            if (aim < 0 || aim > 1 || region < 0 || region >= kRegionCount) continue;
            regionStats[aim][region] = s;
        }
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't read the saved direction grid."); }
}

void AirLabs::SaveRegionStats() const
{
    try
    {
        std::filesystem::create_directories(DataDir());
        std::ofstream out(DataDir() / "direction_stats_v2.txt", std::ios::trunc);
        out << "# aim region samples avg_norm hits avg_raw\n";
        for (int a = 0; a < 2; ++a)
            for (int r = 0; r < kRegionCount; ++r)
            {
                const RegionStat& s = regionStats[a][r];
                out << a << ' ' << r << ' ' << s.samples << ' ' << s.avgNorm << ' ' << s.hits << ' ' << s.avgRaw << '\n';
            }
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't save the direction grid."); }
}

void AirLabs::ResetDirectionStats()
{
    for (auto& perAim : regionStats)
        for (auto& s : perAim) s = RegionStat{};
    SaveRegionStats();
    cvarManager->log("[AirLabs] direction grid cleared.");
}

// ===========================================================================
// Records: every finished run is appended to runs.csv in BakkesMod's data folder
// ===========================================================================

// ===========================================================================
// Saved presets ("My presets"), kept in presets.txt: id|mode|name|cvar=value;cvar=value...
// ===========================================================================

void AirLabs::LoadSavedPresets()
{
    try
    {
        std::ifstream in(DataDir() / "presets.txt");
        if (!in) return;
        std::lock_guard<std::mutex> lock(presetsMutex);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, '|')) f.push_back(cell);
            if (f.size() < 4) continue;
            SavedPreset p;
            p.id = ToI(f[0]);
            p.mode = ToI(f[1]);
            p.name = CleanPresetName(f[2]);
            if (p.id <= 0 || p.mode < 0 || p.mode >= kModeCount || p.name.empty()) continue;
            std::stringstream vs(f[3]);
            std::string kv;
            while (std::getline(vs, kv, ';'))
            {
                size_t eq = kv.find('=');
                if (eq == std::string::npos) continue;
                std::string name = kv.substr(0, eq);
                if (name.rfind("ot_", 0) == 0) name = "airlabs_" + name.substr(3); // saved by Orientation Trainer
                p.values.emplace_back(name, kv.substr(eq + 1));
            }
            nextPresetId = std::max(nextPresetId, p.id + 1);
            savedPresets.push_back(p);
        }
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't read saved presets."); }
}

void AirLabs::WriteSavedPresets()
{
    try
    {
        std::filesystem::create_directories(DataDir());
        std::ofstream out(DataDir() / "presets.txt", std::ios::trunc);
        out << "# AirLabs saved presets: id|mode|name|setting=value;...\n";
        for (const auto& p : savedPresets)
        {
            out << p.id << '|' << p.mode << '|' << p.name << '|';
            for (size_t i = 0; i < p.values.size(); ++i) out << (i ? ";" : "") << p.values[i].first << '=' << p.values[i].second;
            out << '\n';
        }
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't save presets."); }
}

int AirLabs::SaveCurrentAsPreset(const std::string& rawName)
{
    const DrillSettings s = CurrentSettings();
    if (!s.custom) return 0; // only a changed (Custom) run can be saved
    SavedPreset p;
    p.mode = s.mode;
    p.name = CleanPresetName(rawName);
    for (const char* c : kCustomCvars)
    {
        CVarWrapper cv = cvarManager->getCvar(c);
        if (!cv.IsNull()) p.values.emplace_back(c, cv.getStringValue());
    }
    {
        std::lock_guard<std::mutex> lock(presetsMutex);
        p.id = nextPresetId++;
        if (p.name.empty()) p.name = "My preset " + std::to_string(p.id);
        savedPresets.push_back(p);
        WriteSavedPresets();
    }
    userSel[s.mode] = p.id;
    detachedFrom[s.mode].clear();
    cvarManager->log("[AirLabs] saved preset \"" + p.name + "\".");
    return p.id;
}

void AirLabs::SelectSavedPreset(int mode, int id)
{
    SavedPreset p;
    {
        std::lock_guard<std::mutex> lock(presetsMutex);
        auto it = std::find_if(savedPresets.begin(), savedPresets.end(), [id](const SavedPreset& x) { return x.id == id; });
        if (it == savedPresets.end()) return;
        p = *it;
    }
    // Load its settings into the Custom values, then play it as Custom with this preset attached.
    for (const auto& kv : p.values)
    {
        CVarWrapper cv = cvarManager->getCvar(kv.first);
        if (!cv.IsNull()) cv.setValue(kv.second);
    }
    SetCvarI(PresetCvar(mode), PresetCount(mode));
    userSel[mode] = id;
    detachedFrom[mode].clear();
}

void AirLabs::DeleteSavedPreset(int id)
{
    std::string name;
    {
        std::lock_guard<std::mutex> lock(presetsMutex);
        auto it = std::find_if(savedPresets.begin(), savedPresets.end(), [id](const SavedPreset& x) { return x.id == id; });
        if (it == savedPresets.end()) return;
        name = it->name;
        savedPresets.erase(it);
        WriteSavedPresets();
    }
    // Its runs stay in History. If it's selected, the run just becomes Custom with its settings.
    for (int m = 0; m < kModeCount; ++m)
        if (userSel[m] == id) { userSel[m] = 0; detachedFrom[m] = name; }
    cvarManager->log("[AirLabs] deleted preset \"" + name + "\".");
}

std::string AirLabs::SavedPresetName(int id)
{
    std::lock_guard<std::mutex> lock(presetsMutex);
    for (const auto& p : savedPresets)
        if (p.id == id) return p.name;
    return "";
}

void AirLabs::LoadRecords()
{
    try
    {
        std::ifstream in(DataDir() / "runs.csv");
        if (!in) return;
        std::lock_guard<std::mutex> lock(recordsMutex);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line.rfind("when,", 0) == 0) continue;
            std::vector<std::string> f;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, ',')) f.push_back(cell);
            if (f.size() < 18) continue;
            RunRecord r;
            r.when = f[0]; r.version = f[1];
            r.mode = ToI(f[2]); r.preset = ToI(f[3]); r.aim = ToI(f[4]); r.counted = ToI(f[5]) != 0;
            r.score = ToI(f[6]); r.tier = ToI(f[7]); r.targets = ToI(f[8]); r.misses = ToI(f[9]);
            r.avgTime = ToF(f[10]); r.avgReaction = ToF(f[11]); r.avgOvershoot = ToF(f[12]);
            r.onPct = ToF(f[13]); r.avgErr = ToF(f[14]); r.reacquire = ToF(f[15]);
            r.hybReps = ToI(f[16]); r.hybPct = ToF(f[17]);
            if (f.size() > 18) r.flags = f[18];
            if (f.size() > 19) r.range = ToI(f[19]); // 1.6.0 and later
            if (f.size() > 20) r.userPreset = ToI(f[20]); // 1.8.0 and later: which saved preset
            if (f.size() > 21) r.nearPct = ToF(f[21]);    // 2.3.0 and later: time near the ball

            // Runs saved by 1.2.0, which had Dynamic as its own mode (2) and Hybrid as mode 3.
            if (r.version == "1.2.0")
            {
                if (r.mode == 2) { r.mode = kTrack; r.preset += 5; }
                else if (r.mode == 3) r.mode = kHybrid;
                // Custom used to be the index after the last preset; keep it Custom.
                if (r.preset >= PresetCount(r.mode)) r.preset = PresetCount(r.mode);
                // Insane's direction changes were 180 degrees then, so those scores aren't comparable.
                if ((r.mode == kTrack && r.preset == 4) || (r.mode == kHybrid && r.preset == 2)) r.counted = false;
            }
            // 1.6.0 brought ranges (Short / Medium / Long), and 1.5.x changed the target,
            // timing and distances, so older preset scores aren't comparable with any range.
            // They stay in History but no longer count as records.
            // 2.4.0: Hybrid's ball holds its take-off direction and is tracked for 4 s (was 3 s,
            // with turns), so older Hybrid scores aren't comparable.
            if (r.counted && r.mode == kHybrid && VersionBefore(r.version, 2, 4))
            {
                r.counted = false;
                r.flags += "older rules ";
            }
            // 2.3.0: Flick and Hybrid targets time out after 5 s (was 7.5 s) and a miss costs
            // 500 points. A run with no misses scores the same either way and stays; older runs
            // with misses aren't comparable.
            if (r.counted && r.misses > 0 && (r.mode == kFlick || r.mode == kHybrid) && VersionBefore(r.version, 2, 3))
            {
                r.counted = false;
                r.flags += "older rules ";
            }
            if (r.counted && OldPresetRules(r.version))
            {
                r.counted = false;
                r.flags += "older rules ";
            }
            // Mode 3 was the old Rebound mode (removed in 1.5) until Chase took the number in 2.1.
            if (r.mode == kChase && VersionBefore(r.version, 2, 1)) continue;
            if (r.mode < 0 || r.mode >= kModeCount) continue;
            r.tier = -1; // AirLabs ranks are gone (2.8.1); the column stays in runs.csv
            AddRecordLocked(r);
        }
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't read saved runs."); }
}

void AirLabs::AddRecordLocked(const RunRecord& r)
{
    records.push_back(r);
    if (!r.counted) return;
    int key;
    if (r.userPreset > 0) key = UserKey(r.userPreset); // a saved preset: its own records
    else
    {
        if (r.preset < 0 || r.preset >= PresetCount(r.mode)) return;
        if (r.range < 0 && PresetUsesRange(r.mode, r.preset)) return; // no range saved: can't file it
        key = RecordKey(r.mode, r.preset, r.range);
    }
    if (r.NoRoll()) key += kNoRollKey;
    auto it = bestScore.find(key);
    if (it == bestScore.end() || r.score > it->second) bestScore[key] = r.score;
    ++runCount[key];
    history[key].push_back(static_cast<float>(r.score));
    if (r.onPct >= 0.f)
    {
        auto b = bestOnPct.find(key);
        if (b == bestOnPct.end() || r.onPct > b->second) bestOnPct[key] = r.onPct;
    }
}

void AirLabs::AppendRecordToFile(const RunRecord& r) const
{
    try
    {
        std::filesystem::create_directories(DataDir());
        auto file = DataDir() / "runs.csv";
        bool fresh = !std::filesystem::exists(file);
        std::ofstream out(file, std::ios::app);
        if (fresh)
            out << "when,version,mode,preset,aim,counted,score,tier,targets,misses,avg_time,avg_reaction,"
                   "avg_overshoot,on_target_pct,avg_error,reacquire,hybrid_reps,hybrid_track_pct,flags,range,user_preset,near_pct\n";
        out << r.when << ',' << r.version << ',' << r.mode << ',' << r.preset << ',' << r.aim << ','
            << (r.counted ? 1 : 0) << ',' << r.score << ',' << r.tier << ',' << r.targets << ',' << r.misses << ','
            << r.avgTime << ',' << r.avgReaction << ',' << r.avgOvershoot << ',' << r.onPct << ',' << r.avgErr << ','
            << r.reacquire << ',' << r.hybReps << ',' << r.hybPct << ',' << r.flags << ',' << r.range << ',' << r.userPreset << ',' << r.nearPct << '\n';
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't save the run."); }
}

std::string AirLabs::BuildShareLine(const RunResult& r) const
{
    std::string line = std::string("AirLabs ") + kVersion + " | " + ModeName(r.mode) + ": " +
                       const_cast<AirLabs*>(this)->RunLabel(r.mode, r.preset, r.range, r.userPreset) + (r.noRoll ? " (No air roll)" : "") + " | " + FmtInt(r.score);
    switch (r.mode)
    {
    case kFlick:
        line += " | " + std::to_string(r.targets) + " targets, " + OrDash(r.avgTime, "%.2f s") + " avg";
        break;
    case kHybrid:
        line += " | " + std::to_string(r.hybReps) + " reps, flicks " + OrDash(r.avgTime, "%.2f s") + ", tracking " + OrDash(r.hybAvgPct, "%.0f%%");
        break;
    case kChase:
        line += " | " + OrDash(r.onPct, "%.0f%%") + " on target, " + OrDash(r.nearPct, "%.0f%%") + " near, " + std::to_string(r.targets) + " catches";
        break;
    default:
        line += " | " + OrDash(r.onPct, "%.0f%%") + " on target, " + OrDash(r.nearPct, "%.0f%%") + " near";
        break;
    }
    if (r.userPreset > 0) line += " | MY PRESET";
    else if (r.custom) line += " | CUSTOM";
    else if (r.counted) line += " | PRESET";
    else line += " | didn't count";
    return line;
}

void AirLabs::ResetSession()
{
    std::lock_guard<std::mutex> lock(recordsMutex);
    sessionRuns = 0;
}

// ===========================================================================
// Stick guide: the input that gets you onto the ball fastest right now (Custom runs)
// ===========================================================================

void AirLabs::UpdateGuide(const Basis& car, const Vector& aim, const Vector& toTarget, float dt,
                                     const DrillSettings& s, bool tracking)
{
    float err = AngleDeg(aim, toTarget);
    if (prevErr >= 0.f && dt > 0.f)
        closingDeg += std::min(1.f, dt * 12.f) * ((prevErr - err) / dt - closingDeg);
    prevErr = err;

    // Which way each input moves the aim:
    //   nose aim:   nose-up pitch swings it toward the roof, yaw right toward the right side.
    //   wheels aim: nose-up pitch swings the underside toward the front, roll right toward the
    //               left. Yaw spins the car around its wheels axis, so it can't help.
    const bool wheels = s.aim == 1;
    Vector moveX = wheels ? Scale(car.right, -1.f) : car.right;
    Vector moveY = wheels ? car.forward : car.up;

    Vector g = Sub(toTarget, Scale(aim, Dot(aim, toTarget)));
    if (Len(g) < 1e-3f) g = moveY;
    g = Norm(g);

    // The stick controls how fast the spin speeds up or slows down, not where you point, so the
    // best input is "toward the ball" until the remaining angle equals your stopping distance
    // (speed^2 / (2 x braking rate)), then "away from it" to brake.
    float speed = Len(aimVel) / kDeg;
    float stopDeg = closingDeg > 0.f ? closingDeg * closingDeg / (2.f * *guideAccel) : 0.f;
    if (err <= s.target && (tracking || speed < 40.f))               guideState = GuideState::Hold;
    else if (closingDeg > 0.f && err - 0.5f * s.target <= stopDeg)   guideState = GuideState::Brake;
    else                                                             guideState = GuideState::Go;

    Vector c;
    if (guideState == GuideState::Hold)
    {
        c = tracking ? Scale(g, 0.5f * err / std::max(0.1f, s.target))
                     : Scale(Norm(Scale(aimVel, -1.f)), std::min(1.f, speed / 40.f) * 0.6f);
    }
    else
    {
        // Also cancel sideways drift, so the path curves straight onto the ball.
        Vector side = Sub(aimVel, Add(Scale(g, Dot(aimVel, g)), Scale(aim, Dot(aimVel, aim))));
        float sideDeg = Len(side) / kDeg;
        c = guideState == GuideState::Brake ? Scale(g, -1.f) : g;
        if (sideDeg > 1.f) c = Sub(c, Scale(Norm(side), std::min(1.f, sideDeg / 45.f)));
    }
    float x = Dot(c, moveX), y = Dot(c, moveY);
    float l = std::sqrt(x * x + y * y);
    if ((guideState != GuideState::Hold && l > 0.05f) || l > 1.f) { x /= l; y /= l; }
    guideX = x;
    guideY = y;
}

// ===========================================================================
// HUD
// ===========================================================================

void AirLabs::DrawLaser(CanvasWrapper& canvas)
{
    ServerWrapper server = gameWrapper->GetGameEventAsServer();
    if (server.IsNull()) return;
    CarWrapper car = server.GetGameCar();
    CameraWrapper cam = gameWrapper->GetCamera();
    if (car.IsNull() || cam.IsNull()) return;

    Rotator r = car.GetRotation();
    Basis cb = BasisFromRotator(r.Pitch, r.Yaw, r.Roll);
    Vector carLoc = car.GetLocation();
    int aim = state != RunState::Idle ? runAim : CurrentSettings().aim;
    Vector dir = AimVec(cb, aim);
    Vector a = Add(carLoc, Scale(dir, aim == 1 ? 25.f : 75.f));
    // How far it reaches: to the ball's distance (so its tip touches the ball when you're on it),
    // or the Laser length setting.
    float len = *laserLength;
    if (laserToBall && *laserToBall && labOn)
        len = Len(lab.ballPos);
    else if (laserToBall && *laserToBall)
        len = state == RunState::Replay ? play.distance : hasTarget ? Len(Sub(targetLocation, pinLocation)) : CurrentSettings().distance;
    Vector b = Add(carLoc, Scale(dir, std::max(len, aim == 1 ? 60.f : 110.f)));

    // Clip against a plane just in front of the camera: projecting a point behind the
    // camera would flip it across the screen.
    Vector camLoc = cam.GetLocation();
    Rotator cr = cam.GetRotation();
    Vector camFwd = BasisFromRotator(cr.Pitch, cr.Yaw, cr.Roll).forward;
    const float nearDist = 10.f;
    float da = Dot(Sub(a, camLoc), camFwd), db = Dot(Sub(b, camLoc), camFwd);
    if (da < nearDist && db < nearDist) return;
    if (da < nearDist) a = Add(a, Scale(Sub(b, a), (nearDist - da) / (db - da)));
    if (db < nearDist) b = Add(b, Scale(Sub(a, b), (nearDist - db) / (da - db)));
    Vector2F pa = canvas.ProjectF(a), pb = canvas.ProjectF(b);

    // Red off target, yellow within kNearDeg (25 degrees) (or on it but still turning when you must
    // stop on it), green when locked on.
    float lr = 255.f, lg = 40.f, lb = 40.f;
    if (laserColorMode && *laserColorMode == 1) // one colour, whatever you're aiming at
    {
        unsigned rgb = 0x5AA0FF;
        if (laserColor && laserColor->size() >= 7 && (*laserColor)[0] == '#') rgb = static_cast<unsigned>(std::strtoul(laserColor->c_str() + 1, nullptr, 16));
        lr = static_cast<float>((rgb >> 16) & 255); lg = static_cast<float>((rgb >> 8) & 255); lb = static_cast<float>(rgb & 255);
    }
    else if (state != RunState::Idle && hasTarget)
    {
        if (locking)                             { lr = 80.f;  lg = 255.f; lb = 120.f; }
        else if (settling || aimErrorDeg < kNearDeg) { lr = 255.f; lg = 210.f; lb = 60.f; }
    }
    float w = *laserWidth;
    canvas.SetColor(LinearColor(lr, lg, lb, 70.f));
    canvas.DrawLine(pa, pb, w * 3.f);
    canvas.SetColor(LinearColor(lr, lg, lb, 255.f));
    canvas.DrawLine(pa, pb, w);
    canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 200.f));
    canvas.DrawLine(pa, pb, std::max(1.f, w * 0.35f));
}

void AirLabs::DrawGuide(CanvasWrapper& canvas, const Vector2& screen)
{
    const bool wheels = runAim == 1;
    const int R = 56;
    const Vector2 c{ screen.X / 2, static_cast<int>(screen.Y * 0.78f) };

    // (x = right, y = nose up) as a stick position. With normal pitch, pushing the stick up
    // tips the nose down, so "nose up" is the bottom of the ring.
    auto toRing = [&](float x, float noseUp) {
        float up = *guideInvert ? noseUp : -noseUp;
        float l = std::sqrt(x * x + up * up);
        if (l > 1.f) { x /= l; up /= l; }
        return Vector2{ c.X + static_cast<int>(x * R), c.Y - static_cast<int>(up * R) };
    };

    FillRect(canvas, c.X - R - 16, c.Y - R - 34, 2 * R + 32, 2 * R + 64, LinearColor(10.f, 14.f, 22.f, 150.f));
    canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 130.f));
    const int seg = 40;
    for (int i = 0; i < seg; ++i)
    {
        float a0 = 2.f * kPi * i / seg, a1 = 2.f * kPi * (i + 1) / seg;
        canvas.DrawLine(Vector2{ c.X + static_cast<int>(std::cos(a0) * R), c.Y + static_cast<int>(std::sin(a0) * R) },
                        Vector2{ c.X + static_cast<int>(std::cos(a1) * R), c.Y + static_cast<int>(std::sin(a1) * R) }, 2.f);
    }
    canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 50.f));
    canvas.DrawLine(Vector2{ c.X - R, c.Y }, Vector2{ c.X + R, c.Y }, 1.f);
    canvas.DrawLine(Vector2{ c.X, c.Y - R }, Vector2{ c.X, c.Y + R }, 1.f);

    const char* word = "GO";
    LinearColor col = kColGood;
    if (guideState == GuideState::Brake)     { word = "BRAKE"; col = kColBad; }
    else if (guideState == GuideState::Hold) { word = "HOLD";  col = kColText; }

    Vector2 gp = toRing(guideX, guideY);
    canvas.SetColor(col);
    canvas.DrawLine(c, gp, 3.f);
    canvas.SetPosition(Vector2{ gp.X - 8, gp.Y - 8 });
    canvas.FillBox(Vector2{ 16, 16 });

    // Your actual stick, with signs learned from how the car responds.
    float noseUpIn = (calPitch >= 0.0 ? 1.f : -1.f) * inPitch;
    float xIn = wheels ? (calRoll >= 0.0 ? 1.f : -1.f) * inRoll : (calYaw >= 0.0 ? 1.f : -1.f) * inYaw;
    Vector2 sp = toRing(xIn, noseUpIn);
    canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 255.f));
    canvas.SetPosition(Vector2{ sp.X - 7, sp.Y - 7 });
    canvas.DrawBox(Vector2{ 14, 14 });

    canvas.SetColor(col);
    canvas.SetPosition(Vector2{ c.X - R - 4, c.Y - R - 28 });
    canvas.DrawString(word, 1.4f, 1.4f, true);
    canvas.SetColor(kColDim);
    canvas.SetPosition(Vector2{ c.X - R - 4, c.Y + R + 8 });
    canvas.DrawString(wheels ? "left/right = air roll" : "left/right = yaw", 1.0f, 1.0f, true);
}

void AirLabs::DrawRegionGrid(CanvasWrapper& canvas, int x, int y)
{
    // 3x3 grid as seen from the driver's seat; the middle cell is "behind".
    static const int layout[3][3] = {
        { kUpLeft,   kUp,     kUpRight   },
        { kLeft,     kBehind, kRight     },
        { kDownLeft, kDown,   kDownRight },
    };
    static const char* labels[kRegionCount] = { "R", "UR", "U", "UL", "L", "DL", "D", "DR", "Back" };
    const int cw = 96, ch = 32, gap = 4;
    const RegionStat* stats = regionStats[std::clamp(runAim, 0, 1)];

    double mean = 0.0;
    int n = 0;
    for (int i = 0; i < kRegionCount; ++i)
        if (stats[i].samples >= 3) { mean += stats[i].avgNorm; ++n; }
    if (n > 0) mean /= n;

    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            int r = layout[row][col];
            int px = x + col * (cw + gap), py = y + row * (ch + gap);
            const RegionStat& s = stats[r];
            LinearColor bg(70.f, 76.f, 90.f, 190.f);
            if (s.samples >= 3 && n > 0)
            {
                double ratio = s.avgNorm / std::max(1e-3, mean);
                if (ratio < 0.9)      bg = LinearColor(40.f, 150.f, 80.f, 210.f);
                else if (ratio > 1.1) bg = LinearColor(170.f, 50.f, 45.f, 210.f);
                else                  bg = LinearColor(95.f, 95.f, 75.f, 210.f);
            }
            FillRect(canvas, px, py, cw, ch, bg);
            if (hasTarget && r == targetRegion)
            {
                canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 255.f));
                canvas.SetPosition(Vector2{ px, py });
                canvas.DrawBox(Vector2{ cw, ch });
            }
            canvas.SetColor(kColText);
            canvas.SetPosition(Vector2{ px + 6, py + 8 });
            canvas.DrawString(std::string(labels[r]) + "  " + (s.hits > 0 ? Fmt("%.2fs", s.avgRaw) : std::string("--")), 1.1f, 1.1f, true);
        }
}

void AirLabs::DrawReadout(CanvasWrapper& canvas, const Vector2& screen)
{
    std::vector<HudLine> L;
    auto add = [&](const std::string& t, const LinearColor& c = kColText) { L.push_back({ t, c, 1.15f }); };
    add("READOUT", kColTitle);
    add(Fmt("Stick   pitch %3.0f%%", std::fabs(inPitch) * 100.f) + Fmt("   yaw %3.0f%%", std::fabs(inYaw) * 100.f) +
        Fmt("   roll %3.0f%%", std::fabs(inRoll) * 100.f));
    add(Fmt("Spin    %.0f deg/s", spinDeg) + Fmt("  (%.0f%% of max)", PctOfMaxSpin(spinDeg)));
    if (state == RunState::Running && (runMode == kTrack || hybridTracking))
        add(Fmt("Ball    %.0f deg/s", path.speed) + Fmt("  (%.0f%% of max)", PctOfMaxSpin(path.speed)));
    if (state == RunState::Running && runMode == kChase)
        add(chase.moving ? Fmt("Ball    %.0f deg/s", chase.dashSpeed) + Fmt("  (%.0f%% of max)", PctOfMaxSpin(chase.dashSpeed)) : std::string("Ball    stopped"));
    if (state == RunState::Running && run.trackTime > 0.0)
        add(Fmt("On target  %.0f%%", 100.0 * run.onTime / run.trackTime));
    add(tickTiming ? Fmt("Physics  %.0f ticks/s", measuredRate)
                   : (measuredRate > 0.f ? Fmt("Physics  PC clock (%.0f/s measured)", measuredRate) : std::string("Physics  measuring...")),
        tickTiming ? kColText : kColWarn);

    const int w = 380, x = screen.X - w - 30, y = static_cast<int>(screen.Y * 0.12f);
    FillRect(canvas, x - 12, y - 10, w, LinesHeight(L) + 16, kColPanel);
    DrawLines(canvas, x, y, L);
}

void AirLabs::Render(CanvasWrapper canvas)
{
    if (!gameWrapper->IsInFreeplay()) return;
    if (*laserEnabled) DrawLaser(canvas);
    Vector2 screen = canvas.GetSize();
    if (labOn)
    {
        DrawLab(canvas, screen);
        return;
    }
    if (*readout) DrawReadout(canvas, screen);
    if (state == RunState::Idle)
    {
        if (replayOver) DrawReplayOver(canvas, screen);
        return;
    }

    const DrillSettings s = CurrentSettings();

    // ---- world markers: aim cross, and a ring the size of the real hit zone ----
    CameraWrapper cam = gameWrapper->GetCamera();
    if (!cam.IsNull())
    {
        Vector camLoc = cam.GetLocation();
        Rotator cr = cam.GetRotation();
        Basis cb = BasisFromRotator(cr.Pitch, cr.Yaw, cr.Roll);

        auto onScreen = [&](const Vector& world, Vector2& out) -> bool {
            if (Dot(Sub(world, camLoc), cb.forward) <= 1.f) return false;
            out = canvas.Project(world);
            return out.X >= 0 && out.Y >= 0 && out.X <= screen.X && out.Y <= screen.Y;
        };

        if (*showAimMarker)
        {
            Vector2 m;
            if (onScreen(Add(pinLocation, Scale(lastAim, 1200.f)), m))
            {
                canvas.SetColor(LinearColor(255.f, 255.f, 255.f, 220.f));
                canvas.DrawLine(Vector2{ m.X - 10, m.Y }, Vector2{ m.X + 10, m.Y }, 2.f);
                canvas.DrawLine(Vector2{ m.X, m.Y - 10 }, Vector2{ m.X, m.Y + 10 }, 2.f);
            }
        }

        // A ring whose radius is the given angle as seen from the car (on screen only).
        auto drawRing = [&](const Vector& world, float angleDeg, const LinearColor& col, float thickness) {
            Vector2 c;
            if (onScreen(world, c))
            {
                float worldR = Len(Sub(world, pinLocation)) * std::tan(angleDeg * kDeg);
                Vector2 e = canvas.Project(Add(world, Scale(cb.right, worldR)));
                float dx = static_cast<float>(e.X - c.X), dy = static_cast<float>(e.Y - c.Y);
                float rad = std::clamp(std::sqrt(dx * dx + dy * dy), 6.f, 400.f);
                canvas.SetColor(col);
                const int seg = 36;
                for (int i = 0; i < seg; ++i)
                {
                    float a0 = 2.f * kPi * i / seg, a1 = 2.f * kPi * (i + 1) / seg;
                    canvas.DrawLine(Vector2{ c.X + static_cast<int>(std::cos(a0) * rad), c.Y + static_cast<int>(std::sin(a0) * rad) },
                                    Vector2{ c.X + static_cast<int>(std::cos(a1) * rad), c.Y + static_cast<int>(std::sin(a1) * rad) }, thickness);
                }
            }
            // Off screen: nothing. Rocket League's own ball arrow already shows where it is.
        };

        if (hasTarget && state != RunState::Results)
        {
            LinearColor col = locking ? kColGood : (settling || aimErrorDeg < kNearDeg) ? kColWarn : kColBad;
            if (state == RunState::Ready && !locking) col = kColWarn;
            drawRing(targetLocation, state == RunState::Replay ? play.targetDeg : s.target, col, 2.5f);
        }
    }

    if (state == RunState::Running && hasTarget && guideOn) DrawGuide(canvas, screen);

    // ---- panel ----
    std::vector<HudLine> L;
    auto add = [&](const std::string& t, const LinearColor& c = kColText, float sc = 1.4f) { L.push_back({ t, c, sc }); };
    std::string promptMain, promptSub; // start prompt, shown at the top centre
    const bool customRun = runPreset >= PresetCount(runMode);
    std::string title = std::string(ModeName(runMode)) + "  |  " +
                        (runUserPreset > 0 ? RunLabel(runMode, runPreset, runRange, runUserPreset)
                                           : customRun ? "Custom" + Fmt(" (%.0f uu)", s.distance) : PresetRangeLabel(runMode, runPreset, runRange));
    if (runAim == 1) title += "  |  wheels";
    if (runNoRoll) title += "  |  NO AIR ROLL";
    if (state != RunState::Replay) add(title, kColTitle);

    bool showBar = false, bestBar = false;
    float barFrac = 0.f, bestFill = 0.f, bestMark = 0.f, paceMark = 0.f;
    LinearColor barCol = kColGood;
    auto badge = [&]() {
        if (runUserPreset > 0) add("My preset: counts toward its own records", kColGood, 1.1f);
        else add(customRun ? "Custom: practice only" : "Counts toward your records", customRun ? kColWarn : kColGood, 1.1f);
    };

    // The finished run's results (shown while you line up the next start ball).
    auto addResults = [&]() {
        const RunResult& r = lastResult;
        add(FmtInt(r.score), kColText, 3.0f);
        if (r.counted)
        {
            if (r.userPreset > 0) add("My preset: " + RunLabel(r.mode, r.preset, r.range, r.userPreset), kColTitle);
            if (r.newBest) add(r.prevBest < 0 ? "Your first score on this preset" : "New personal best!  (" + FmtSigned(r.score - r.prevBest) + ")", kColGood);
            else add("Personal best " + FmtInt(r.prevBest) + "  (" + FmtSigned(r.score - r.prevBest) + ")", kColDim);
        }
        else if (r.custom) add("Custom settings: practice only, not saved to records", kColWarn, 1.2f);
        else add("Didn't count: " + WhyNotCounted(r.flags), kColWarn, 1.2f);

        if (r.mode == kFlick)
        {
            add("Targets " + std::to_string(r.targets) + "   Missed " + std::to_string(r.misses));
            add("Avg time " + OrDash(r.avgTime, "%.2f s") + "   Reaction " + OrDash(r.avgReaction, "%.2f s"));
            add("Overshoot " + OrDash(r.avgOvershoot, "%.1f deg"));
        }
        else if (r.mode == kHybrid)
        {
            add("Reps " + std::to_string(r.hybReps) + "   Missed flicks " + std::to_string(r.misses));
            add("Flick avg " + OrDash(r.avgTime, "%.2f s") + "   Track avg " + OrDash(r.hybAvgPct, "%.0f%%"));
        }
        else if (r.mode == kChase)
        {
            add("Catches " + std::to_string(r.targets) + "   Missed " + std::to_string(r.misses));
            add("On target " + OrDash(r.onPct, "%.0f%%") + "   Near " + OrDash(r.nearPct, "%.0f%%") + "   Avg catch " + OrDash(r.avgTime, "%.2f s"));
        }
        else
        {
            add("On target " + OrDash(r.onPct, "%.0f%%") + "   Near " + OrDash(r.nearPct, "%.0f%%") + "   Avg error " + OrDash(r.avgErr, "%.1f deg"));
            add("Reacquire " + OrDash(r.reacquire, "%.2f s"));
            if (r.hasBands)
            {
                std::string curve = "By speed:";
                for (int i = 0; i < kSpeedBands; ++i) curve += " " + OrDash(r.bandPct[i], "%.0f%%");
                add(curve, kColDim, 1.2f);
            }
        }
        {
            std::lock_guard<std::mutex> lock(lbMutex);
            if (!lbStatus.empty() && WallSeconds() - lbStatusWall < 120.0) add(lbStatus, lbStatusGood ? kColGood : kColWarn, 1.1f);
        }
        add("Copy result: F2 > Plugins > AirLabs", kColDim, 1.1f);
    };

    if (state == RunState::Ready)
    {
        if (showResults && lastResult.valid)
        {
            addResults();
            add(" ", kColDim, 0.6f);
        }
        badge();
        // The start prompt goes big and red at the top centre of the screen (drawn below).
        promptMain = showResults ? "POINT AT THE BALL TO GO AGAIN" : "POINT AT THE BALL TO START";
        promptSub = gameTime < readyLockUntil ? std::string("Get ready...")
                                              : Fmt("Hold for %.1f s", std::max(0.f, kReadyHoldSec - readyHeld));
        add("Stop: airlabs_toggle key or F2", kColDim, 1.1f);
        if (!showResults && !customRun)
            add(runBest >= 0 ? "Your best: " + FmtInt(runBest) : std::string("No best yet on this preset"), kColGold, 1.2f);
        showBar = true;
        barFrac = readyHeld / kReadyHoldSec;
    }
    else if (state == RunState::Running)
    {
        badge();
        std::string clock = runEnd < 1e17 ? FmtClock(runEnd - gameTime) + " left" : std::string("Endless");
        add(clock + "      Score " + FmtInt(std::llround(run.score)));
        // Best-score marker: your best on this preset, and whether you're ahead of the pace
        // that would beat it (your best spread evenly over the run).
        if (runBest >= 0)
        {
            long long sc = std::llround(run.score);
            double frac = runEnd < 1e17 ? std::clamp((gameTime - runStart) / std::max(1e-3, runEnd - runStart), 0.0, 1.0) : 1.0;
            long long pace = std::llround(runBest * frac);
            if (sc > runBest)
                add("NEW BEST  " + FmtSigned(sc - runBest), kColGood, 1.3f);
            else
                add("Best " + FmtInt(runBest) + "     " + (sc >= pace ? "ahead of pace " + FmtSigned(sc - pace) : "behind pace " + FmtInt(pace - sc)),
                    sc >= pace ? kColGood : kColWarn, 1.2f);
            float scale = 1.1f * static_cast<float>(std::max<long long>(runBest, sc));
            bestBar = true;
            bestFill = static_cast<float>(sc) / scale;
            bestMark = static_cast<float>(runBest) / scale;
            paceMark = static_cast<float>(pace) / scale;
        }
        double onPct = run.trackTime > 0.0 ? 100.0 * run.onTime / run.trackTime : 0.0;
        double nearPct = run.trackTime > 0.0 ? 100.0 * run.nearTime / run.trackTime : 0.0;
        if (runMode == kFlick)
        {
            if (hasTarget)
            {
                add("Aim error   " + Fmt("%.1f deg", aimErrorDeg));
                if (s.settle) add("Turning     " + Fmt("%.0f deg/s", aimSpeedDeg), aimSpeedDeg <= s.settleRate ? kColGood : kColWarn);
            }
            else if (lastFlash == Flash::Hit)  add("Hit!", kColGood);
            else if (lastFlash == Flash::Miss) add("Missed", kColBad);
            add("Targets " + std::to_string(run.targets) + "   Missed " + std::to_string(run.misses) + Fmt(" (-%.0f each)", kMissPenalty) + "   Streak " + std::to_string(run.streak));
            add("Last " + OrDash(run.lastTime, "%.2f s") + "   Avg " + (run.targets ? Fmt("%.2f s", run.sumTime / run.targets) : std::string("--")));
            showBar = true;
            barFrac = holdProgress;
        }
        else if (runMode == kChase)
        {
            if (chase.moving) add("CHASE IT", kColWarn);
            else add("CATCH IT", kColGood);
            add("On target   " + Fmt("%.0f %%", onPct) + "     Near   " + Fmt("%.0f %%", nearPct));
            add("Catches " + std::to_string(run.targets) + "   Last " + OrDash(run.lastTime, "%.2f s") +
                "   Avg " + (run.targets ? Fmt("%.2f s", run.sumTime / run.targets) : std::string("--")));
        }
        else if (runMode == kTrack)
        {
            add("On target   " + Fmt("%.0f %%", onPct) + "     Near   " + Fmt("%.0f %%", nearPct));
            add("Aim error   " + Fmt("%.1f deg", aimErrorDeg));
            if (s.path.dynamic) add("Ball speed  " + Fmt("%.0f deg/s", path.speed) + Fmt(" (%.0f%% of max spin)", PctOfMaxSpin(path.speed)), kColDim, 1.2f);
        }
        else
        {
            if (hybridTracking) add("TRACK IT", kColWarn);
            else if (hasTarget) add("FLICK", kColWarn);
            add("Reps " + std::to_string(run.hybReps) + "   Missed flicks " + std::to_string(run.misses));
            add("Flick avg " + (run.targets ? Fmt("%.2f s", run.sumTime / run.targets) : std::string("--")) +
                "   Track avg " + (run.hybReps ? Fmt("%.0f%%", run.hybPctSum / run.hybReps) : std::string("--")));
            showBar = true;
            if (hybridTracking)
            {
                barFrac = static_cast<float>(std::clamp((hybridTrackEnd - gameTime) / std::max(0.1f, s.hybridTrack), 0.0, 1.0));
                barCol = kColWarn;
            }
            else barFrac = holdProgress;
        }
        if (spawnFallback) add("Ball height doesn't fit the turn sizes from here", kColWarn, 1.1f);
    }
    else if (state == RunState::Results)
    {
        addResults();
    }
    else if (state == RunState::Replay)
    {
        // Watching a leaderboard run.
        std::string who;
        {
            std::lock_guard<std::mutex> lock(lbMutex);
            who = playName;
        }
        std::string label = std::string(ModeName(play.mode)) + "  |  " + PresetRangeLabel(play.mode, play.preset, play.range);
        if (play.aim == 1) label += "  |  wheels";
        if (play.flags & kRfNoRoll) label += "  |  NO AIR ROLL";
        add("REPLAY  |  " + label, kColTitle);
        add(who, kColText, 2.2f);
        const double total = play.samples.empty() ? 0.0 : play.samples.back().ms / 1000.0;
        const double at = playTick < play.samples.size() ? play.samples[playTick].ms / 1000.0 : total;
        add(FmtClock(std::max(0.0, total - at)) + " left      Score " + FmtInt(std::llround(playScore)));
        add("Final score " + FmtInt(play.score), kColGold, 1.2f);
        if (play.mode == kHybrid) add(hybridTracking ? "TRACK IT" : "FLICK", kColWarn, 1.2f);
        add(!toggleKey.empty() ? "Stop watching: press " + toggleKey
            : !menuKey.empty() ? "Stop watching: press " + menuKey + ", then Stop"
                               : std::string("Stop watching: Stop in the AirLabs window"), kColDim, 1.1f);
        showBar = true;
        barFrac = total > 0.0 ? static_cast<float>(at / total) : 0.f;
        barCol = LinearColor(90.f, 170.f, 255.f, 255.f);
    }

    const int x = 40, w = 460;
    int y = static_cast<int>(screen.Y * 0.18f);
    int h = LinesHeight(L) + (showBar ? 18 : 0) + (bestBar ? 20 : 0) + 16;
    FillRect(canvas, x - 12, y - 10, w, h, kColPanel);
    y = DrawLines(canvas, x, y, L);
    if (showBar)
    {
        Bar(canvas, x, y + 2, w - 24, barFrac, barCol);
        y += 18;
    }
    if (bestBar)
    {
        // Your score so far (blue), your best (gold line) and where your best run's pace
        // would be by now (white line): stay right of the white line to beat your best.
        const int bw = w - 24;
        Bar(canvas, x, y + 4, bw, bestFill, LinearColor(90.f, 170.f, 255.f, 255.f));
        FillRect(canvas, x + static_cast<int>(bw * std::min(1.f, bestMark)) - 1, y, 3, 16, kColGold);
        FillRect(canvas, x + static_cast<int>(bw * std::min(1.f, paceMark)) - 1, y + 2, 2, 12, LinearColor(255.f, 255.f, 255.f, 230.f));
    }

    // ---- start prompt: top centre, red ----
    if (!promptMain.empty()) DrawPrompt(canvas, screen, promptMain, promptSub);

    if (*hudGrid && state == RunState::Running && (runMode == kFlick || runMode == kHybrid))
    {
        int gy = static_cast<int>(screen.Y * 0.18f) + h + 8;
        FillRect(canvas, x - 12, gy - 6, w, 26 + 3 * 32 + 2 * 4 + 14, kColPanel);
        canvas.SetColor(kColTitle);
        canvas.SetPosition(Vector2{ x, gy });
        canvas.DrawString("Times by direction (driver's view)", 1.1f, 1.1f, true);
        DrawRegionGrid(canvas, x, gy + 24);
    }
}

// When a replay ends: whose run it was, how it finished, and the key back to the menu.
void AirLabs::DrawReplayOver(CanvasWrapper& canvas, const Vector2& screen)
{
    std::string who;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        who = playName;
    }
    std::string label = std::string(ModeName(play.mode)) + "  |  " + PresetRangeLabel(play.mode, play.preset, play.range);
    if (play.aim == 1) label += "  |  wheels";
    if (play.flags & kRfNoRoll) label += "  |  NO AIR ROLL";
    std::vector<HudLine> L;
    L.push_back({ "REPLAY OVER  |  " + label, kColTitle, 1.4f });
    L.push_back({ who, kColText, 2.2f });
    L.push_back({ "Final score " + FmtInt(play.score), kColGold, 1.2f });
    L.push_back({ "Watch another on the Leaderboard page", kColDim, 1.1f });
    const int x = 40, w = 460, y = static_cast<int>(screen.Y * 0.18f);
    FillRect(canvas, x - 12, y - 10, w, LinesHeight(L) + 16, kColPanel);
    DrawLines(canvas, x, y, L);

    if (!menuKey.empty())
        DrawPrompt(canvas, screen, "PRESS " + menuKey + " FOR THE MENU", !toggleKey.empty() ? "or " + toggleKey + " to play" : std::string());
    else
        DrawPrompt(canvas, screen, "REPLAY OVER", "Set a key for the menu in F2 > Plugins > AirLabs");
}

// ===========================================================================
// Settings page (F2 -> Plugins -> AirLabs). Runs on BakkesMod's render
// thread: anything that touches the game goes through gameWrapper->Execute.
// ===========================================================================

std::string AirLabs::GetPluginName() { return "AirLabs"; }

void AirLabs::SetImGuiContext(uintptr_t ctx)
{
    ImGui::SetCurrentContext(reinterpret_cast<ImGuiContext*>(ctx));
}

bool AirLabs::EditF(const char* label, const char* desc, const char* cvar, float shown, float lo, float hi, const char* fmt)
{
    float v = shown;
    bool changed = ImGui::SliderFloat(label, &v, lo, hi, fmt);
    if (changed) { MakeCustom(); SetCvarF(cvar, v); }
    Desc(desc);
    return changed;
}

bool AirLabs::EditI(const char* label, const char* desc, const char* cvar, int shown, int lo, int hi, const char* fmt)
{
    int v = shown;
    bool changed = ImGui::SliderInt(label, &v, lo, hi, fmt);
    if (changed) { MakeCustom(); SetCvarI(cvar, v); }
    Desc(desc);
    return changed;
}

bool AirLabs::EditCheck(const char* label, const char* desc, const char* cvar, bool shown)
{
    bool v = shown;
    bool changed = ImGui::Checkbox(label, &v);
    if (changed) { MakeCustom(); SetCvarI(cvar, v ? 1 : 0); }
    Desc(desc);
    return changed;
}

bool AirLabs::EditRadio(const char* label, const char* desc, const char* cvar, int shown, std::initializer_list<const char*> options)
{
    ImGui::Text("%s", label);
    int v = shown, i = 0;
    bool changed = false;
    for (const char* o : options)
    {
        ImGui::SameLine();
        changed |= ImGui::RadioButton(o, &v, i++);
    }
    if (changed && v != shown) { MakeCustom(); SetCvarI(cvar, v); }
    Desc(desc);
    return changed;
}

bool AirLabs::UiSliderF(const char* label, const char* desc, const char* cvar, float lo, float hi, const char* fmt)
{
    CVarWrapper cv = cvarManager->getCvar(cvar);
    if (cv.IsNull()) return false;
    float v = cv.getFloatValue();
    bool changed = ImGui::SliderFloat(label, &v, lo, hi, fmt);
    if (changed) cv.setValue(v);
    Desc(desc);
    return changed;
}

bool AirLabs::UiCheck(const char* label, const char* desc, const char* cvar)
{
    CVarWrapper cv = cvarManager->getCvar(cvar);
    if (cv.IsNull()) return false;
    bool v = cv.getBoolValue();
    bool changed = ImGui::Checkbox(label, &v);
    if (changed) cv.setValue(v ? 1 : 0);
    Desc(desc);
    return changed;
}

void AirLabs::RenderSettings()
{
    // The trainer window is the main way in; this tab just opens it, with the classic view
    // kept underneath as a fallback.
    ImGui::Spacing();
    if (Choice("Open the AirLabs window", true, ImVec2(320, 40))) ToggleWindow();
    if (haveSpaceMap)
    {
        // Right at the top, so it's the first thing players see.
        ImGui::SameLine(0, 12);
        if (Choice(spaceMap ? "Standard map" : "Space map", true, ImVec2(240, 40))) cvarManager->executeCommand(spaceMap ? "airlabs_standard" : "airlabs_space");
    }
    ImGui::Spacing();

    // Opening AirLabs with a key: one click, or the console command spelled out. Full-strength
    // text in AirLabs' bold fonts, so it stands out from the rest of the F2 page.
    EnsureFonts();
    auto pushFont = [this](int f) { if (uiFonts[f]) ImGui::PushFont(uiFonts[f]); };
    auto popFont = [this](int f) { if (uiFonts[f]) ImGui::PopFont(); };
    pushFont(kFontHeading);
    ImGui::TextUnformatted("Open AirLabs with the O key");
    popFont(kFontHeading);
    pushFont(kFontBold);
    if (ImGui::Button("Set O to open AirLabs", ImVec2(240, 34))) cvarManager->executeCommand("bind O AL_menu");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Click once, then press O anytime to open or close AirLabs.");
    ImGui::TextUnformatted("Or any key you like: press F6 to open the BakkesMod console, type this with your key in place of <any key> (O, say) and press Enter:");
    popFont(kFontBold);
    pushFont(kFontHeading);
    ImGui::TextColored(ImVec4(0.45f, 0.72f, 1.f, 1.f), "    bind <any key> AL_menu");
    popFont(kFontHeading);
    ImGui::SameLine();
    pushFont(kFontBold);
    if (ImGui::Button("Copy")) ImGui::SetClipboardText("bind O AL_menu");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies \"bind O AL_menu\": change the O to the key you want");
    popFont(kFontBold);
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Classic settings view"))
    {
        ImGui::PushItemWidth(260.f);
        if (ImGui::BeginTabBar("airlabs_tabs"))
        {
            if (ImGui::BeginTabItem("Play"))    { TabPlay();    ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Assists")) { TabAssists(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Stats"))   { TabStats();   ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Help"))    { TabHelp();    ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();
    }
}

// ----------------------------------------------------------------------------
// Play tab
// ----------------------------------------------------------------------------

void AirLabs::TabPlay()
{
    const DrillSettings s = CurrentSettings();
    static const char* modeBlurb[kModeCount] = {
        "A ball appears somewhere around you. Turn onto it as fast as you can.",
        "The ball moves around you and keeps changing direction. Keep your aim on it.",
        "A ball appears, you flick onto it, then it takes off and you track it for a few seconds.",
        "One ball. Point at it and it dashes off in a random direction, then stops. Catch it again, and again.",
    };

    // ---- run (at the top, so Start is always the first thing you see) ----
    Heading("Run");
    const bool running = state == RunState::Ready || state == RunState::Running;
    if (ImGui::Button(running ? "Stop" : "Start", ImVec2(110, 34))) cvarManager->executeCommand("airlabs_toggle");
    ImGui::SameLine();
    if (s.mode == kFlick || s.mode == kHybrid)
    {
        if (ImGui::Button("Skip target", ImVec2(110, 34))) gameWrapper->Execute([this](GameWrapper*) { SkipTarget(); });
        ImGui::SameLine();
    }
    switch (state)
    {
    case RunState::Idle:    ImGui::TextDisabled(WantSpaceMap() ? "Press Start: it takes you to the space map." : "Press Start: it takes you to Freeplay on a standard field."); break;
    case RunState::Ready:   ImGui::TextDisabled("%s", showResults ? "Run finished. Point at the new ball to go again." : "Point at the ball for 1.5 seconds to begin."); break;
    case RunState::Running: ImGui::TextDisabled("%s", runEnd < 1e17 ? (FmtClock(runEnd - gameTime) + " left").c_str() : "Endless run"); break;
    default:                ImGui::TextDisabled("Run finished. Press Start to go again."); break;
    }
    Desc("Start resets your car to the centre with a ball between you and the goal, 45 degrees to the side of your nose: turn onto it and point at it for 1.5 seconds and the run begins. Skipping a target means the run won't count toward your records. Key: bind F5 airlabs_toggle");

    RunResult r;
    {
        std::lock_guard<std::mutex> lock(recordsMutex);
        r = lastResult;
    }
    if (r.valid)
    {
        ImGui::Text("Last run: %s %s   %s", ModeName(r.mode), RunLabel(r.mode, r.preset, r.range, r.userPreset).c_str(), FmtInt(r.score).c_str());
        if (r.counted) { ImGui::SameLine(); ImGui::TextColored(kUiGood, "%s%s", r.userPreset > 0 ? "My preset" : "Counted", r.newBest ? "  (new best!)" : ""); }
        ImGui::SameLine();
        if (ImGui::Button("Copy result")) ImGui::SetClipboardText(r.share.c_str());
    }

    // ---- mode ----
    Heading("Mode");
    for (int m = 0; m < kModeCount; ++m)
    {
        if (m) ImGui::SameLine();
        if (Choice(ModeName(m), s.mode == m, ImVec2(110, 32))) SetCvarI("airlabs_mode", m);
    }
    Desc(modeBlurb[s.mode]);
    UiCheck("No air roll", "Air roll does nothing during runs: turn with pitch and yaw only. Works with every mode (not with wheels aim) and keeps its own records.", "airlabs_noroll");

    // ---- preset ----
    Heading("Preset");
    const int count = PresetCount(s.mode);
    const bool dynSelected = !s.custom && PresetInDynamicGroup(s.mode, s.preset);
    int firstDynamic = -1;
    bool first = true;
    ImGui::PushID("presets");
    for (int p = 0; p < count; ++p)
    {
        if (PresetInDynamicGroup(s.mode, p)) { if (firstDynamic < 0) firstDynamic = p; continue; }
        const std::string label = PresetName(s.mode, p);
        const float w = std::max(100.f, ImGui::CalcTextSize(label.c_str()).x + 24.f);
        if (!first) SameLineIfFits(w);
        first = false;
        ImGui::PushID(p);
        if (Choice(label.c_str(), !s.custom && s.preset == p, ImVec2(w, 28))) SetCvarI(PresetCvar(s.mode), p);
        ImGui::PopID();
    }
    if (firstDynamic >= 0)
    {
        SameLineIfFits(100.f);
        if (Choice("Dynamic", dynSelected, ImVec2(100, 28)) && !dynSelected) SetCvarI(PresetCvar(s.mode), firstDynamic);
    }
    SameLineIfFits(100.f);
    if (Choice("Custom", s.custom, ImVec2(100, 28)) && !s.custom) MakeCustom();
    if (dynSelected)
    {
        ImGui::Text("Speed range:");
        for (int p = firstDynamic; p < count; ++p)
        {
            ImGui::SameLine();
            ImGui::PushID(100 + p);
            const std::string label = PresetName(s.mode, p);
            if (Choice(label.c_str(), s.preset == p, ImVec2(std::max(80.f, ImGui::CalcTextSize(label.c_str()).x + 20.f), 24))) SetCvarI(PresetCvar(s.mode), p);
            ImGui::PopID();
        }
    }
    ImGui::PopID();

    // ---- range ----
    ImGui::Spacing();
    if (s.custom || PresetUsesRange(s.mode, s.preset))
    {
        int rangeNow = s.custom ? -1 : s.range;
        if (s.custom)
            for (int r = 0; r < kRangeCount; ++r)
                if (std::fabs(s.distance - RangeDistance(r)) < 0.5f) rangeNow = r;
        int chosen = rangeNow;
        if (RangeButtons(chosen, "play"))
        {
            SetCvarI("airlabs_range", chosen);
            if (s.custom) { MakeCustom(); SetCvarF("airlabs_distance", RangeDistance(chosen)); }
        }
        Desc(s.custom ? "Sets Custom's ball distance (you can also drag the distance slider below). Save it as a preset to keep records for it."
                      : "How far away the ball is. Each range has its own records: a closer ball is a bigger target, so Short is the easiest and Long the hardest.");
    }
    else
    {
        ImGui::TextDisabled("Range: Flip Reset always puts the ball %.0f uu away, so it has one set of records.", s.distance);
    }

    ImGui::Spacing();
    ImGui::TextColored(kUiSub, "%s", CurrentPresetLabel(s).c_str());
    Desc(PresetBlurb(s.mode, s.custom ? count : s.preset));
    if (!s.custom)
    {
        std::string detail;
        if (s.mode == kFlick)
        {
            detail = Fmt("Turns %.0f", s.minTurn) + Fmt("-%.0f deg  |  one-minute run", s.maxTurn);
            if (s.aim == 1) detail += "  |  wheels aim, ball above you";
        }
        else if (s.mode == kChase)
        {
            detail = (s.path.dynamic ? Fmt("Each dash %.0f", s.path.vMin) + Fmt("-%.0f deg/s", s.path.vMax) : Fmt("Dashes at %.0f deg/s", s.path.speed)) +
                     Fmt("  |  %.1f", s.chaseMin) + Fmt("-%.1f s each", s.chaseMax);
        }
        else if (s.path.dynamic)
        {
            detail = Fmt("Speed %.0f", s.path.vMin) + Fmt("-%.0f deg/s", s.path.vMax) + Fmt("  |  turns up to %.0f deg", s.path.maxTurn);
        }
        else
        {
            detail = Fmt("%.0f deg/s", s.path.speed) + Fmt(" (about %.0f%% stick)", 100.f * StickForSpin(s.path.speed, false)) +
                     Fmt("  |  new direction every %.1f s", s.path.changeEvery) + Fmt("  |  turns up to %.0f deg", s.path.maxTurn);
        }
        if (s.mode == kHybrid) detail = Fmt("Flicks 30-180 deg, then %.0f s tracking  |  ", s.hybridTrack) + detail;
        detail += Fmt("  |  ball at %.0f uu", s.distance) + Fmt(" (target %.1f deg)", s.target);
        ImGui::TextDisabled("%s", detail.c_str());
        ImGui::TextColored(kUiGood, "Counts toward your records: the same settings for every player, new random targets every run.");
    }
    else
    {
        ImGui::TextColored(kUiWarn, "Custom: practice only until you save it as a preset in the trainer window.");
        ImGui::SameLine();
        if (ImGui::Button("Back to preset"))
        {
            int base = BaseIndex(s.mode);
            SetCvarI(PresetCvar(s.mode), std::clamp(base, 0, count - 1));
        }
    }


    // ---- settings for this mode ----
    PlaySettingsBody(s, true);
}

void AirLabs::PlaySettingsBody(const DrillSettings& s, bool withHeading)
{
    if (withHeading)
    {
        Heading((std::string("Settings for ") + ModeName(s.mode)).c_str());
        Desc("These show what the selected preset uses. Change anything and the run becomes Custom, which you can save as your own preset; Back to preset undoes it.");
    }
    if (s.mode == kFlick)
    {
        PlayFlickSettings(s, false);
        PlayDirections(s);
        PlayHeight(s);
    }
    else if (s.mode == kTrack)
    {
        PlayTrackSettings(s, false);
        PlayDirections(s);
        PlayHeight(s);
    }
    else if (s.mode == kChase)
    {
        PlayChaseSettings(s);
        PlayHeight(s);
    }
    else
    {
        SubHeading("Flick part");
        PlayFlickSettings(s, true);
        SubHeading("Track part");
        PlayTrackSettings(s, true);
        PlayDirections(s);
        PlayHeight(s);
    }
    SubHeading(hideRoundSettings ? "Distance" : "Run length and distance");
    if (!hideRoundSettings)
        EditF("Run length", "How long a run lasts. 0 means it keeps going until you stop it.",
              "airlabs_run_length", s.runLength, 0.f, 600.f, "%.0f s");
    EditF("Ball distance", "How far the ball is from your car. Presets use the Range buttons above (Short 400, Medium 600, Long 900); dragging this makes the run Custom. The target is always exactly the ball, so closer is easier.",
          "airlabs_distance", s.distance, 300.f, kMaxBallDistance, "%.0f uu");
    ImGui::TextDisabled("Target size: %.1f deg, exactly the ball at %.0f uu.", s.target, s.distance);
    Desc("There's no separate target size setting: you're on target when your aim is on the ball itself, at any distance.");
}

void AirLabs::PlayChaseSettings(const DrillSettings& s)
{
    EditRadio("Dash speed", "Steady: every dash goes at the same speed. Random: each dash picks a new speed, so you have to read it and commit.",
              "airlabs_dynamic", s.path.dynamic ? 1 : 0, { "Steady##chase", "Random##chase" });
    if (!s.path.dynamic)
        EditF("Speed", "How fast the ball dashes across the sky.", "airlabs_track_speed", s.path.speed, 5.f, kMaxBallSpeed, "%.0f deg/s");
    else
    {
        EditF("Slowest dash", "The slowest a dash can be.", "airlabs_dyn_min", s.path.vMin, 5.f, kMaxBallSpeed, "%.0f deg/s");
        EditF("Fastest dash", "The fastest a dash can be.", "airlabs_dyn_max", s.path.vMax, 5.f, kMaxBallSpeed, "%.0f deg/s");
    }
    EditF("Shortest dash", "How long the ball moves before it stops, at least.", "airlabs_chase_min", s.chaseMin, 0.2f, 5.f, "%.1f s");
    EditF("Longest dash", "How long the ball moves before it stops, at most. Each dash picks a time in between.", "airlabs_chase_max", s.chaseMax, 0.2f, 5.f, "%.1f s");
    if (!hideRoundSettings)
        EditF("Time limit", "How long a stopped ball waits for you. Run out and it counts as a miss and dashes off anyway. 0 means it waits forever.",
              "airlabs_timeout", s.timeout, 0.f, 30.f, "%.1f s");
}

void AirLabs::PlayFlickSettings(const DrillSettings& s, bool hybridPart)
{
    EditF("Smallest turn", "The least you'll have to turn to reach a new target.", "airlabs_min_angle", s.minTurn, 0.f, 180.f, "%.0f deg");
    EditF("Biggest turn", "The most you'll have to turn. Small numbers mean quick corrections; 180 means targets right behind you.",
          "airlabs_max_angle", s.maxTurn, 0.f, 180.f, "%.0f deg");
    EditI("Hold to confirm", "How long you have to stay on a target before it counts, so you can't just swipe past it.",
          "airlabs_hold_ms", static_cast<int>(std::lround(s.holdSec * 1000.f)), 0, 2000, "%d ms");
    if (!hideRoundSettings)
        EditF("Time limit", "How long you get to reach each target. Run out and it's a miss. 0 means no limit.",
              "airlabs_timeout", s.timeout, 0.f, 30.f, "%.1f s");
    EditI("Delay between targets", "The pause before the next target appears.",
          "airlabs_next_delay", static_cast<int>(std::lround(s.delaySec * 1000.f)), 0, 3000, "%d ms");
    if (!hybridPart)
        EditRadio("Aim with", "Nose: point the front of your car at the ball. Wheels: point the bottom of your car at it, the setup for a flip reset.",
                  "airlabs_aim", s.aim, { "Nose##aim", "Wheels##aim" });
    EditCheck("Must stop on target", "A target only counts once your car has actually stopped turning on it, not while it's swinging through. Trains clean braking.",
              "airlabs_settle", s.settle);
    if (s.settle)
        EditF("Counts as stopped below", "How slowly you need to be turning to count as stopped. Lower is stricter.",
              "airlabs_settle_rate", s.settleRate, 5.f, 120.f, "%.0f deg/s");
    EditCheck("Weak-spot mode", "Gives you more targets in the directions you're slowest at (see Stats > Directions).",
              "airlabs_weak_spot", s.weakSpot);
}

void AirLabs::PlayTrackSettings(const DrillSettings& s, bool hybridPart)
{
    if (hybridPart)
        EditF("Track time after lock-on", "How long you have to follow the ball after you flick onto it.",
              "airlabs_hybrid_track", s.hybridTrack, 1.f, 15.f, "%.1f s");

    EditRadio("Ball speed", "Steady: the ball always moves at one speed. Changing: it keeps speeding up and slowing down, so you have to adjust how far you push the stick.",
              "airlabs_dynamic", s.path.dynamic ? 1 : 0, { "Steady##spd", "Changing##spd" });
    if (!s.path.dynamic)
    {
        EditF("Speed", "How fast the ball moves around you. Slower speeds train small, precise stick movements.",
              "airlabs_track_speed", s.path.speed, 5.f, kMaxBallSpeed, "%.0f deg/s");
        ImGui::TextDisabled("Needs about %.0f%% stick (%.0f%% of your car's top spin speed).", 100.f * StickForSpin(s.path.speed, false), PctOfMaxSpin(s.path.speed));
        if (!hybridPart) // Hybrid's ball holds its take-off direction
            EditF("New direction every", "How often the ball turns onto a new path.",
                  "airlabs_track_turn", s.path.changeEvery, 0.3f, 10.f, "%.1f s");
    }
    else
    {
        EditF("Slowest speed", "The slowest the ball gets.", "airlabs_dyn_min", s.path.vMin, 5.f, kMaxBallSpeed, "%.0f deg/s");
        EditF("Fastest speed", "The fastest the ball gets. It turns more often when slow and less often when fast.",
              "airlabs_dyn_max", s.path.vMax, 5.f, kMaxBallSpeed, "%.0f deg/s");
    }
    if (!hybridPart)
        EditF("Biggest direction change", "How sharply the ball can turn. Small numbers mean gentle curves; big numbers mean sharp corners.",
              "airlabs_max_turn", s.path.maxTurn, 0.f, 180.f, "%.0f deg");
    else
        ImGui::TextDisabled("The ball holds the direction it takes off in for the whole track.");
}

void AirLabs::PlayDirections(const DrillSettings& s)
{
    SubHeading("Directions");
    // Show what's actually ticked. (Nothing ticked is stored as 0 and means "everywhere";
    // CurrentSettings turns it into all directions, so drawing from s.dirMask made Clear
    // look like it did nothing.) Presets always use every direction.
    const unsigned int stored = s.custom ? (static_cast<unsigned int>(*dirMask) & kAllRegions) : kAllRegions;
    unsigned int maskV = stored;
    bool changed = false;
    // Fixed column positions, so the 3x3 grid lines up in any font.
    const float x0 = ImGui::GetCursorPosX(), colW = 150.f;
    auto box = [&](const char* label, int region, int col) {
        if (col) ImGui::SameLine(x0 + col * colW);
        changed |= ImGui::CheckboxFlags(label, &maskV, 1u << region);
    };
    ImGui::PushID("dirs");
    box("Up-left", kUpLeft, 0);     box("Up", kUp, 1);         box("Up-right", kUpRight, 2);
    box("Left", kLeft, 0);          box("Behind", kBehind, 1); box("Right", kRight, 2);
    box("Down-left", kDownLeft, 0); box("Down", kDown, 1);     box("Down-right", kDownRight, 2);
    if (ImGui::Button("All", ImVec2(80, 0)))   { maskV = kAllRegions; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(80, 0))) { maskV = 0; changed = true; }
    ImGui::PopID();
    if (changed && maskV != stored) { MakeCustom(); SetCvarI("airlabs_dirs", static_cast<int>(maskV)); }
    if (s.mode == kFlick)
        Desc("Where targets can appear, as seen from the driver's seat. Use it to practise your weak side. Nothing ticked means everywhere.");
    else if (s.mode == kTrack)
        Desc("Which way the ball tends to head, as seen from the driver's seat (Behind doesn't apply here). Nothing ticked means any way.");
    else
        Desc("Where flick targets appear and which way the ball heads afterwards, as seen from the driver's seat. Nothing ticked means anywhere.");
}

void AirLabs::PlayHeight(const DrillSettings& s)
{
    SubHeading("Ball height");
    struct HeightChoice { const char* label; Band band; };
    static const HeightChoice heights[] = {
        { "Any##h", kBandAny }, { "Above##h", kBandAbove }, { "Level##h", kBandLevel }, { "Below##h", kBandBelow },
    };
    for (size_t i = 0; i < std::size(heights); ++i)
    {
        if (i) ImGui::SameLine();
        bool sel = std::fabs(s.band.minDeg - heights[i].band.minDeg) < 0.5f && std::fabs(s.band.maxDeg - heights[i].band.maxDeg) < 0.5f;
        if (Choice(heights[i].label, sel, ImVec2(80, 0)) && !sel)
        {
            MakeCustom();
            SetCvarF("airlabs_height_min", heights[i].band.minDeg);
            SetCvarF("airlabs_height_max", heights[i].band.maxDeg);
        }
    }
    Desc("Whether the ball is above you, level with you, below you, or anywhere, relative to the ground (not your car).");
    EditF("Lowest", "The lowest the ball can be, in degrees above the horizon (negative = below you).",
          "airlabs_height_min", s.band.minDeg, -90.f, 90.f, "%.0f deg");
    EditF("Highest", "The highest the ball can be (90 = straight above you).",
          "airlabs_height_max", s.band.maxDeg, -90.f, 90.f, "%.0f deg");
}

// ----------------------------------------------------------------------------
// Assists tab
// ----------------------------------------------------------------------------

void AirLabs::TabAssists()
{
    Desc("Visual aids and options. These never change whether a run counts toward your records.");

    Heading("Laser");
    UiCheck("Show laser", "A line out of your car showing exactly where you're aiming. Best played with it off: there's no laser in a real match. Key: bind L airlabs_laser_toggle", "airlabs_laser");
    UiCheck("Stop at the ball", "The laser ends at the ball's distance, so its tip touches the ball when you're on it. Off: it reaches as far as Laser length.", "airlabs_laser_to_ball");
    if (!(laserToBall && *laserToBall)) UiSliderF("Laser length", "How far the laser reaches.", "airlabs_laser_length", 500.f, 20000.f, "%.0f uu");
    UiSliderF("Laser thickness", "How thick the laser looks.", "airlabs_laser_width", 1.f, 12.f, "%.1f px");
    {
        const int mode = laserColorMode ? *laserColorMode : 0;
        if (Choice("Changes with your aim", mode == 0, ImVec2(220, 28))) SetCvarI("airlabs_laser_colormode", 0);
        ImGui::SameLine();
        if (Choice("One colour", mode == 1, ImVec2(160, 28))) SetCvarI("airlabs_laser_colormode", 1);
        if (mode == 1)
        {
            ImGui::SameLine();
            unsigned rgb = 0x5AA0FF;
            if (laserColor && laserColor->size() >= 7 && (*laserColor)[0] == '#') rgb = static_cast<unsigned>(std::strtoul(laserColor->c_str() + 1, nullptr, 16));
            float col[3] = { ((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f };
            if (ImGui::ColorEdit3("##lasercol", col, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
            {
                char hex[8];
                std::snprintf(hex, sizeof hex, "#%02X%02X%02X", static_cast<int>(col[0] * 255.f + 0.5f), static_cast<int>(col[1] * 255.f + 0.5f), static_cast<int>(col[2] * 255.f + 0.5f));
                CVarWrapper cv = cvarManager->getCvar("airlabs_laser_color");
                if (!cv.IsNull()) cv.setValue(std::string(hex));
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("click the square to pick");
        }
        Desc(mode == 0 ? "Laser colour: red off the ball, yellow close, green when you're on it."
                       : "Laser colour: always the colour you pick, wherever you're aiming.");
    }

    Heading("Aim marker");
    UiCheck("Show aim marker", "A small white cross where you're aiming, like a crosshair.", "airlabs_show_nose");

    Heading("Stick guide");
    UiCheck("Show stick guide", "A little joystick showing which way to push to reach the ball fastest. Put your white square on the filled one; red means brake. It's coaching, so it only appears in Custom runs.", "airlabs_guide");
    if (*guideMode != 0)
    {
        UiCheck("Inverted pitch", "Tick this if you play with inverted controls, so the guide matches your stick.", "airlabs_guide_invert");
        UiSliderF("Brake cue timing", "When the guide tells you to brake. Lower means earlier.", "airlabs_guide_accel", 300.f, 1500.f, "%.0f");
    }

    Heading("Screen");
    UiCheck("Show readout", "Live numbers: how far you're pushing the stick, how fast your car is spinning, the ball's speed, and the game's tick rate. Handy for testing. Works in Freeplay even with no run going.", "airlabs_readout");
    UiCheck("Show direction grid on screen", "Your times by direction (from Stats > Directions), shown during Flick and Hybrid.", "airlabs_hud_grid");

    Heading("Controls");
    UiCheck("Block jump and boost", "Stops you dodging or boosting out of the drill in Custom runs. Preset runs always block them.", "airlabs_block_jump");
}

// ----------------------------------------------------------------------------
// Stats tab: Overview / Records / Directions / History
// ----------------------------------------------------------------------------

void AirLabs::TabStats()
{
    if (ImGui::BeginTabBar("airlabs_stats"))
    {
        if (ImGui::BeginTabItem("Overview"))   { StatsOverview();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Records"))    { StatsRecords();    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Directions")) { StatsDirections(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("History"))    { StatsHistory();    ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
}

void AirLabs::StatsOverview()
{
    std::lock_guard<std::mutex> lock(recordsMutex);

    Heading("Last run");
    if (!lastResult.valid)
    {
        ImGui::TextDisabled("No runs yet. Pick a preset on the Play tab and press Start.");
    }
    else
    {
        const RunResult& r = lastResult;
        ImGui::TextColored(kUiSub, "%s: %s", ModeName(r.mode), RunLabel(r.mode, r.preset, r.range, r.userPreset).c_str());
        ImGui::Text("Score  %s", FmtInt(r.score).c_str());
        if (r.counted)
        {
            ImGui::SameLine();
            ImGui::TextColored(kUiGood, "   %s", r.userPreset > 0 ? "My preset" : "Counted");
            if (r.newBest) ImGui::TextColored(kUiGood, "%s", r.prevBest < 0 ? "Your first score on this preset." : ("New personal best! (" + FmtSigned(r.score - r.prevBest) + ")").c_str());
            else ImGui::TextDisabled("Personal best %s (%s)", FmtInt(r.prevBest).c_str(), FmtSigned(r.score - r.prevBest).c_str());
        }
        else
        {
            ImGui::TextColored(kUiWarn, "%s", r.custom ? "Custom settings: not saved to records." : ("Didn't count: " + WhyNotCounted(r.flags)).c_str());
        }
        if (r.mode == kFlick)
            ImGui::Text("Targets %d   Missed %d   Avg time %s   Reaction %s   Overshoot %s", r.targets, r.misses,
                        OrDash(r.avgTime, "%.2f s").c_str(), OrDash(r.avgReaction, "%.2f s").c_str(), OrDash(r.avgOvershoot, "%.1f deg").c_str());
        else if (r.mode == kHybrid)
            ImGui::Text("Reps %d   Missed flicks %d   Flick avg %s   Track avg %s", r.hybReps, r.misses,
                        OrDash(r.avgTime, "%.2f s").c_str(), OrDash(r.hybAvgPct, "%.0f%%").c_str());
        else if (r.mode == kChase)
            ImGui::Text("Catches %d   Missed %d   On target %s   Near %s   Avg catch %s", r.targets, r.misses,
                        OrDash(r.onPct, "%.0f%%").c_str(), OrDash(r.nearPct, "%.0f%%").c_str(), OrDash(r.avgTime, "%.2f s").c_str());
        else
            ImGui::Text("On target %s   Near %s   Avg error %s   Reacquire %s", OrDash(r.onPct, "%.0f%%").c_str(), OrDash(r.nearPct, "%.0f%%").c_str(),
                        OrDash(r.avgErr, "%.1f deg").c_str(), OrDash(r.reacquire, "%.2f s").c_str());
        if (ImGui::Button("Copy result")) ImGui::SetClipboardText(r.share.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 640.f);
        ImGui::TextDisabled("%s", r.share.c_str());
        ImGui::PopTextWrapPos();
        Desc("Reaction: how long before you start turning. Overshoot: how far you swing past the target after first reaching it. Reacquire: how long it takes to get back on after losing the ball.");
    }

    Heading("Skill profile");
    RangeButtons(statsRange, "ov");
    Desc("Built from your last 10 counted runs on each preset at this range. Each axis shows the real number behind it; play more presets to fill the chart in. The range also applies to the tracking sections below.");
    {
        // ---- gather recent runs (last 10 per preset at this range; Flip Reset ignores range) ----
        enum Axis { kAxFlick, kAxReaction, kAxAccuracy, kAxPrecision, kAxSpeedTrack, kAxConsistency, kAxCount };
        double sum[kAxCount] = {}; int n[kAxCount] = {};
        double trendSum = 0.0; int trendN = 0;
        std::map<int, std::vector<const RunRecord*>> recent; // key -> newest first
        for (auto it = records.rbegin(); it != records.rend(); ++it)
        {
            const RunRecord& r = *it;
            if (!r.counted || r.userPreset > 0 || r.mode < 0 || r.mode >= kModeCount || r.preset < 0 || r.preset >= PresetCount(r.mode)) continue;
            if (PresetUsesRange(r.mode, r.preset) && r.range != statsRange) continue;
            auto& v = recent[RecordKey(r.mode, r.preset, r.range)];
            if (v.size() < 10) v.push_back(&r);
        }
        for (const auto& kv : recent)
        {
            const auto& v = kv.second;
            for (const RunRecord* r : v)
            {
                if (r->mode == kChase) continue; // Chase only feeds Consistency and Trend (below)
                float target = BallTargetDeg(PresetDistance(r->mode, r->preset, r->range));
                if (r->mode != kTrack && r->avgTime > 0.f)
                {
                    float par = r->mode == kFlick ? AvgPar(kFlickPresets[r->preset].minTurn, kFlickPresets[r->preset].maxTurn)
                                                  : AvgPar(kHybridFlickMin, kHybridFlickMax);
                    sum[kAxFlick] += par / r->avgTime; ++n[kAxFlick];
                }
                if (r->mode != kTrack && r->avgReaction > 0.f) { sum[kAxReaction] += r->avgReaction; ++n[kAxReaction]; }
                if (r->mode != kTrack && r->avgOvershoot >= 0.f) { sum[kAxAccuracy] += std::max(0.f, r->avgOvershoot - target); ++n[kAxAccuracy]; }
                if (r->mode == kTrack && r->onPct >= 0.f && !PresetIsDynamic(kTrack, r->preset))
                {
                    int ax = r->preset <= 1 ? kAxPrecision : kAxSpeedTrack; // Precision/Control vs Standard/Fast/Insane
                    sum[ax] += r->onPct; ++n[ax];
                }
            }
            if (v.size() >= 3)
            {
                double mean = 0.0, sq = 0.0;
                for (const RunRecord* r : v) mean += r->score;
                mean /= v.size();
                for (const RunRecord* r : v) sq += (r->score - mean) * (r->score - mean);
                if (mean > 1.0) { sum[kAxConsistency] += std::sqrt(sq / v.size()) / mean; ++n[kAxConsistency]; }
            }
            if (v.size() >= 6)
            {
                size_t half = v.size() / 2;
                double newer = 0.0, older = 0.0;
                for (size_t i = 0; i < half; ++i) newer += v[i]->score;
                for (size_t i = half; i < 2 * half; ++i) older += v[i]->score;
                if (older > 1.0) { trendSum += newer / older - 1.0; ++trendN; }
            }
        }

        // ---- turn each measurement into a 0-1 axis value, with its real number as the label ----
        static const char* names[kAxCount] = { "Flick speed", "Reaction", "Accuracy", "Precision tracking", "Speed tracking", "Consistency" };
        static const char* practise[kAxCount] = { "Flick Big", "Flick Small", "Flick Medium", "Track Precision", "Track Fast", "your most-played preset" };
        static const char* missing[kAxCount] = { "play Flick", "play Flick", "play Flick", "play Track Precision", "play Track Fast", "play any preset 3+ times" };
        float val[kAxCount];
        std::string num[kAxCount];
        for (int a = 0; a < kAxCount; ++a)
        {
            if (!n[a]) { val[a] = -1.f; num[a] = missing[a]; continue; }
            double m = sum[a] / n[a];
            switch (a)
            {
            case kAxFlick:       val[a] = static_cast<float>(std::clamp((m - 0.6) / 0.8, 0.0, 1.0)); num[a] = Fmt("%.2fx par speed", m); break;
            case kAxReaction:    val[a] = static_cast<float>(std::clamp((0.60 - m) / 0.45, 0.0, 1.0)); num[a] = Fmt("%.2f s to react", m); break;
            case kAxAccuracy:    val[a] = static_cast<float>(std::clamp(1.0 - m / 20.0, 0.0, 1.0)); num[a] = Fmt("%.1f deg past target", m); break;
            case kAxPrecision:
            case kAxSpeedTrack:  val[a] = static_cast<float>(std::clamp(m / 100.0, 0.0, 1.0)); num[a] = Fmt("%.0f%% on target", m); break;
            default:             val[a] = static_cast<float>(std::clamp(1.0 - m / 0.25, 0.0, 1.0)); num[a] = Fmt("+/-%.0f%% run to run", m * 100.0); break;
            }
        }

        // ---- the radar ----
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float boxW = 560.f, boxH = 380.f, R = 128.f;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 c(origin.x + boxW * 0.5f, origin.y + boxH * 0.5f + 4.f);
        auto at = [&](int a, float frac) {
            float ang = -kPi * 0.5f + 2.f * kPi * a / static_cast<float>(kAxCount);
            return ImVec2(c.x + std::cos(ang) * R * frac, c.y + std::sin(ang) * R * frac);
        };
        const ImU32 grid = IM_COL32(255, 255, 255, 30), spoke = IM_COL32(255, 255, 255, 40);
        for (int ring = 1; ring <= 4; ++ring)
        {
            ImVec2 pts[kAxCount];
            for (int a = 0; a < kAxCount; ++a) pts[a] = at(a, ring / 4.f);
            dl->AddPolyline(pts, kAxCount, grid, true, 1.f);
        }
        for (int a = 0; a < kAxCount; ++a) dl->AddLine(c, at(a, 1.f), spoke, 1.f);
        ImVec2 shape[kAxCount];
        bool anyData = false;
        for (int a = 0; a < kAxCount; ++a) { shape[a] = at(a, std::max(0.04f, val[a])); anyData |= val[a] >= 0.f; }
        if (anyData)
        {
            dl->AddConvexPolyFilled(shape, kAxCount, IM_COL32(59, 130, 246, 70));
            dl->AddPolyline(shape, kAxCount, IM_COL32(86, 148, 248, 255), true, 2.f);
        }
        for (int a = 0; a < kAxCount; ++a)
            if (val[a] >= 0.f) dl->AddCircleFilled(shape[a], 4.f, IM_COL32(86, 148, 248, 255), 12);
        // Labels: name, then the real number, outside each spoke.
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        for (int a = 0; a < kAxCount; ++a)
        {
            ImVec2 tip = at(a, 1.f);
            float ang = -kPi * 0.5f + 2.f * kPi * a / static_cast<float>(kAxCount);
            ImVec2 dir(std::cos(ang), std::sin(ang));
            ImVec2 s1 = ImGui::CalcTextSize(names[a]), s2 = ImGui::CalcTextSize(num[a].c_str());
            float w = std::max(s1.x, s2.x), h = s1.y + s2.y + 2.f;
            ImVec2 p(tip.x + dir.x * 16.f, tip.y + dir.y * 16.f);
            if (dir.x > 0.2f) {} else if (dir.x < -0.2f) p.x -= w; else p.x -= w * 0.5f;
            if (dir.y < -0.5f) p.y -= h; else if (dir.y < 0.5f) p.y -= h * 0.5f;
            bool have = val[a] >= 0.f;
            dl->AddText(font, fs, ImVec2(p.x + (w - s1.x) * (dir.x < -0.2f ? 1.f : dir.x > 0.2f ? 0.f : 0.5f), p.y),
                        have ? IM_COL32(230, 234, 241, 255) : IM_COL32(142, 151, 168, 255), names[a]);
            dl->AddText(font, fs, ImVec2(p.x + (w - s2.x) * (dir.x < -0.2f ? 1.f : dir.x > 0.2f ? 0.f : 0.5f), p.y + s1.y + 2.f),
                        have ? IM_COL32(120, 175, 255, 255) : IM_COL32(93, 102, 119, 255), num[a].c_str());
        }
        ImGui::Dummy(ImVec2(boxW, boxH));

        // ---- strongest / work on / trend ----
        int hi = -1, lo = -1, have = 0;
        for (int a = 0; a < kAxCount; ++a)
        {
            if (val[a] < 0.f) continue;
            ++have;
            if (hi < 0 || val[a] > val[hi]) hi = a;
            if (lo < 0 || val[a] < val[lo]) lo = a;
        }
        ImGui::SameLine(0, 16);
        ImGui::BeginGroup();
        auto card = [&](const char* title, const std::string& line1, const std::string& line2, ImU32 accent) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = 300.f, h = 96.f;
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(22, 27, 36, 255), 10.f);
            dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(37, 44, 58, 255), 10.f);
            dl->AddRectFilled(ImVec2(p.x, p.y + 14), ImVec2(p.x + 3, p.y + h - 14), accent, 2.f);
            dl->AddText(font, fs, ImVec2(p.x + 16, p.y + 12), IM_COL32(142, 151, 168, 255), title);
            dl->AddText(font, fs, ImVec2(p.x + 16, p.y + 14 + fs + 6), IM_COL32(230, 234, 241, 255), line1.c_str());
            dl->AddText(font, fs, ImVec2(p.x + 16, p.y + 16 + 2 * (fs + 6)), IM_COL32(142, 151, 168, 255), line2.c_str(), nullptr);
            ImGui::Dummy(ImVec2(w, h));
        };
        ImGui::Dummy(ImVec2(0, 24));
        if (have >= 2)
        {
            card("STRONGEST", names[hi], num[hi], IM_COL32(52, 211, 153, 255));
            card("WORK ON", names[lo], "Try " + std::string(practise[lo]), IM_COL32(245, 184, 65, 255));
        }
        else
        {
            card("STRONGEST", "Not enough data yet", "Play a few different presets", IM_COL32(93, 102, 119, 255));
            card("WORK ON", "Not enough data yet", "Play a few different presets", IM_COL32(93, 102, 119, 255));
        }
        if (trendN > 0)
        {
            double t = trendSum / trendN * 100.0;
            std::string head = t > 2.0 ? Fmt("Improving +%.0f%%", t) : t < -2.0 ? Fmt("Down %.0f%%", -t) : std::string("Steady");
            card("TREND", head, "Your last 5 runs vs the 5 before", t > 2.0 ? IM_COL32(52, 211, 153, 255) : t < -2.0 ? IM_COL32(242, 107, 91, 255) : IM_COL32(59, 130, 246, 255));
        }
        else card("TREND", "Not enough runs yet", "Play a preset 6+ times", IM_COL32(93, 102, 119, 255));
        ImGui::EndGroup();
    }

    Heading("Tracking ceiling");
    {
        int best = -1;
        for (int p = 0; p < PresetCount(kTrack); ++p)
        {
            if (kTrackPresets[p].dynamic) continue;
            auto it = bestOnPct.find(RecordKey(kTrack, p, statsRange));
            if (it != bestOnPct.end() && it->second >= 75.f) best = p;
        }
        if (best >= 0)
            ImGui::Text("%.0f deg/s  (%s, %.0f%% of max spin)", kTrackPresets[best].speed, kTrackPresets[best].name, PctOfMaxSpin(kTrackPresets[best].speed));
        else
            ImGui::TextDisabled("Not reached yet.");
        Desc("The fastest steady-speed Track preset where you've kept your aim on the ball at least 75% of the time.");
    }

    Heading("Speed curve");
    if (lastResult.valid && lastResult.hasBands)
    {
        static const char* bandNames[kSpeedBands] = { "15-30 deg/s", "30-45 deg/s", "45-60 deg/s", "60-75 deg/s", "75-90 deg/s" };
        for (int i = 0; i < kSpeedBands; ++i)
        {
            float v = lastResult.bandPct[i];
            ImGui::ProgressBar(v >= 0.f ? v / 100.f : 0.f, ImVec2(220, 0), v >= 0.f ? Fmt("%.0f%% on target", v).c_str() : "not reached");
            ImGui::SameLine();
            ImGui::Text("%s", bandNames[i]);
        }
    }
    else
    {
        ImGui::TextDisabled("Play a Track Dynamic preset to see this.");
    }
    Desc("From your last Track Dynamic run: how well you stayed on the ball at each speed. Shows where your tracking starts to fall apart.");
}

void AirLabs::StatsRecords()
{
    std::lock_guard<std::mutex> lock(recordsMutex);
    Desc("Your best score on every preset, for each range, plus your saved presets. Plain Custom runs don't count. Click a row to see its progress.");

    for (int m = 0; m < kModeCount; ++m)
    {
        if (m) ImGui::SameLine();
        if (Choice(ModeName(m), statsMode == m, ImVec2(100, 28))) { statsMode = m; statsPreset = 0; statsUserSel = 0; }
    }
    statsMode = std::clamp(statsMode, 0, kModeCount - 1);
    statsPreset = std::clamp(statsPreset, 0, PresetCount(statsMode) - 1);
    RangeButtons(statsRange, "rec");
    statsRange = std::clamp(statsRange, 0, kRangeCount - 1);
    if (Choice(statsNoRoll ? "Showing: No air roll" : "Show No air roll records", statsNoRoll, ImVec2(220, 28))) statsNoRoll = !statsNoRoll;
    const int nr = statsNoRoll ? kNoRollKey : 0; // No air roll records sit beside the normal ones

    // Saved presets for this mode (they ignore range: each has its own fixed distance).
    std::vector<std::pair<int, std::string>> mine;
    {
        std::lock_guard<std::mutex> plock(presetsMutex);
        for (const auto& sp : savedPresets)
            if (sp.mode == statsMode) mine.emplace_back(sp.id, sp.name);
    }
    if (statsUserSel > 0 && std::none_of(mine.begin(), mine.end(), [this](const auto& x) { return x.first == statsUserSel; })) statsUserSel = 0;

    // One table row: label, best, runs, last-10 average.
    auto row = [&](const std::string& label, int key, bool selected) -> bool {
        auto best = bestScore.find(key);
        auto runs = runCount.find(key);
        auto hist = history.find(key);
        bool clicked = ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns);
        ImGui::NextColumn();
        ImGui::Text("%s", best != bestScore.end() ? FmtInt(best->second).c_str() : "--");
        ImGui::NextColumn();
        ImGui::Text("%d", runs != runCount.end() ? runs->second : 0);
        ImGui::NextColumn();
        if (hist != history.end() && !hist->second.empty())
        {
            const auto& v = hist->second;
            size_t n = std::min<size_t>(v.size(), 10);
            double sum = 0.0;
            for (size_t i = v.size() - n; i < v.size(); ++i) sum += v[i];
            ImGui::Text("%s", FmtInt(static_cast<long long>(sum / n)).c_str());
        }
        else ImGui::TextDisabled("--");
        ImGui::NextColumn();
        return clicked;
    };
    auto header = [&]() {
        ImGui::Columns(4, "records", false);
        ImGui::SetColumnWidth(0, ImGui::CalcTextSize("Dynamic Medium (my preset)").x + 24.f);
        ImGui::SetColumnWidth(1, ImGui::CalcTextSize("888,888").x + 40.f);
        ImGui::TextDisabled("Preset");       ImGui::NextColumn();
        ImGui::TextDisabled("Best");         ImGui::NextColumn();
        ImGui::TextDisabled("Runs");         ImGui::NextColumn();
        ImGui::TextDisabled("Last 10 avg");  ImGui::NextColumn();
        ImGui::Separator();
    };

    ImGui::Spacing();
    header();
    for (int p = 0; p < PresetCount(statsMode); ++p)
    {
        std::string label = (PresetUsesRange(statsMode, p) ? PresetLabel(statsMode, p) : PresetRangeLabel(statsMode, p, statsRange)) +
                            "##rec" + std::to_string(p);
        if (row(label, RecordKey(statsMode, p, statsRange) + nr, statsUserSel == 0 && statsPreset == p)) { statsPreset = p; statsUserSel = 0; }
    }
    ImGui::Columns(1);

    if (!mine.empty())
    {
        SubHeading("My presets");
        header();
        for (const auto& mp : mine)
        {
            std::string label = mp.second + "##mine" + std::to_string(mp.first);
            if (row(label, UserKey(mp.first) + nr, statsUserSel == mp.first)) statsUserSel = mp.first;
        }
        ImGui::Columns(1);
    }

    // ---- detail for the selected row ----
    const bool userRow = statsUserSel > 0;
    int key = (userRow ? UserKey(statsUserSel) : RecordKey(statsMode, statsPreset, statsRange)) + nr;
    std::string title = std::string(ModeName(statsMode)) + ": " +
                        (userRow ? std::find_if(mine.begin(), mine.end(), [this](const auto& x) { return x.first == statsUserSel; })->second + " (my preset)"
                                 : PresetRangeLabel(statsMode, statsPreset, statsRange));
    if (statsNoRoll) title += "  (No air roll)";
    Heading(title.c_str());
    auto best = bestScore.find(key);
    auto hist = history.find(key);
    if (best == bestScore.end())
    {
        ImGui::TextDisabled("No scores yet on this preset.");
        return;
    }
    ImGui::Text("Best %s", FmtInt(best->second).c_str());
    if (!userRow) ImGui::TextDisabled("Where it stands among everyone's: the Leaderboard page.");
    if (hist != history.end() && !hist->second.empty())
    {
        const auto& v = hist->second;
        int countV = static_cast<int>(std::min<size_t>(v.size(), 100));
        const float* start = v.data() + (v.size() - countV);
        int firstRun = static_cast<int>(v.size()) - countV + 1;
        std::vector<GraphMark> marks;
        if ((statsMode == kTrack || statsMode == kChase) && !userRow) marks.push_back({ 60000.f, "max 60,000" });
        ImGui::Spacing();
        ImGui::TextDisabled("Score, %s", countV < static_cast<int>(v.size()) ? ("last " + std::to_string(countV) + " runs").c_str() : "every run");
        float width = std::clamp(ImGui::GetContentRegionAvail().x, 360.f, 900.f);
        ScoreGraph("##scoregraph", start, countV, firstRun, marks, ImVec2(width, 300.f));
        Desc("Each point is one run, oldest on the left. The gold dot is your best. Hover over the graph to see any run's score.");
    }
}

void AirLabs::StatsDirections()
{
    Desc("Your recent average flick time in each direction, as seen from the driver's seat. Green = faster than your average, red = a weak spot, grey = not enough reps yet. Bigger turns are scaled so every direction compares fairly. Weak-spot mode uses this to give you more targets where you're slow.");
    SettingsDirectionGrid();
    ImGui::Spacing();
    if (ImGui::Button("Reset direction grid")) gameWrapper->Execute([this](GameWrapper*) { ResetDirectionStats(); });
}

void AirLabs::SettingsDirectionGrid()
{
    static const int layout[3][3] = {
        { kUpLeft,   kUp,     kUpRight   },
        { kLeft,     kBehind, kRight     },
        { kDownLeft, kDown,   kDownRight },
    };
    static const char* names[kRegionCount] = { "Right", "Up-right", "Up", "Up-left", "Left", "Down-left", "Down", "Down-right", "Behind" };

    ImGui::Text("Show");
    ImGui::SameLine();
    ImGui::RadioButton("Nose aim##grid", &gridView, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Wheels aim##grid", &gridView, 1);

    const RegionStat* stats = regionStats[std::clamp(gridView, 0, 1)];
    double mean = 0.0;
    int n = 0;
    for (int i = 0; i < kRegionCount; ++i)
        if (stats[i].samples >= 3) { mean += stats[i].avgNorm; ++n; }
    if (n > 0) mean /= n;

    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
        {
            if (col) ImGui::SameLine();
            int r = layout[row][col];
            const RegionStat& s = stats[r];
            ImVec4 c(0.30f, 0.32f, 0.38f, 1.f);
            if (s.samples >= 3 && n > 0)
            {
                double ratio = s.avgNorm / std::max(1e-3, mean);
                if (ratio < 0.9)      c = ImVec4(0.16f, 0.55f, 0.30f, 1.f);
                else if (ratio > 1.1) c = ImVec4(0.65f, 0.20f, 0.18f, 1.f);
                else                  c = ImVec4(0.40f, 0.40f, 0.30f, 1.f);
            }
            ImGui::PushStyleColor(ImGuiCol_Button, c);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
            std::string label = std::string(names[r]) + "\n" + (s.hits > 0 ? Fmt("%.2f s", s.avgRaw) : std::string("--")) + "##cell" + std::to_string(r);
            ImGui::Button(label.c_str(), ImVec2(96, 44));
            ImGui::PopStyleColor(3);
        }
}

void AirLabs::StatsHistory()
{
    std::lock_guard<std::mutex> lock(recordsMutex);
    Desc("Your most recent runs, newest first. Every run is also saved to a file you can open in a spreadsheet.");

    if (records.empty()) ImGui::TextDisabled("No runs yet.");
    else
    {
        // Columns sized from the text, so it lines up in any font.
        ImGui::Columns(4, "history", false);
        ImGui::SetColumnWidth(0, ImGui::CalcTextSize("2026-10-24 23:59").x + 24.f);
        ImGui::SetColumnWidth(1, ImGui::CalcTextSize("Hybrid: Dynamic Medium (Medium)").x + 24.f);
        ImGui::SetColumnWidth(2, ImGui::CalcTextSize("888,888").x + 32.f);
        ImGui::TextDisabled("When");   ImGui::NextColumn();
        ImGui::TextDisabled("Run");    ImGui::NextColumn();
        ImGui::TextDisabled("Score");  ImGui::NextColumn();
        ImGui::TextDisabled("Result"); ImGui::NextColumn();
        ImGui::Separator();
        int shown = 0;
        for (int i = static_cast<int>(records.size()) - 1; i >= 0 && shown < 25; --i)
        {
            const RunRecord& rr = records[i];
            if (rr.mode < 0 || rr.mode >= kModeCount) continue;
            ++shown;
            ImGui::TextDisabled("%s", rr.when.c_str());
            ImGui::NextColumn();
            ImGui::Text("%s: %s%s", ModeName(rr.mode), RunLabel(rr.mode, rr.preset, rr.range, rr.userPreset).c_str(), rr.NoRoll() ? " (No air roll)" : "");
            ImGui::NextColumn();
            ImGui::Text("%s", FmtInt(rr.score).c_str());
            ImGui::NextColumn();
            if (rr.userPreset > 0) ImGui::TextDisabled("%s", rr.counted ? "my preset" : "didn't count");
            else if (rr.counted) ImGui::Text("counted");
            else ImGui::TextDisabled("%s", rr.preset >= PresetCount(rr.mode) ? "custom"
                                           : rr.flags.find("older rules") != std::string::npos ? "older rules" : "didn't count");
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
    }

    Heading("This session");
    ImGui::Text("%d run%s", sessionRuns, sessionRuns == 1 ? "" : "s");
    ImGui::SameLine();
    if (ImGui::Button("Reset session")) gameWrapper->Execute([this](GameWrapper*) { ResetSession(); });
    std::string where = (DataDir() / "runs.csv").string();
    ImGui::TextDisabled("Saved to %s", where.c_str());
}

// ----------------------------------------------------------------------------
// Help tab
// ----------------------------------------------------------------------------

void AirLabs::TabHelp()
{
    Heading("Tips");
    for (int i = 0; i < kTipCount; ++i)
    {
        ImGui::Bullet();
        ImGui::TextWrapped("%s %s", kTips[i].title, kTips[i].body);
    }
    if (showTips && !*showTips)
    {
        if (ImGui::Button("Show a tip on the Play page")) SetCvarI("airlabs_tips", 1);
    }

    Heading("How a run works");
    ImGui::TextWrapped("Pick a mode, a preset and a range on the Play tab, then press Start at the top. Your car resets to the centre with a ball between you and the goal, 45 degrees to the side of your nose: turn onto it and point at it for 1.5 seconds and the one-minute run begins. When the minute is up you get a score and how it compares with your personal best, and the car and ball reset straight away: point at the ball to go again. Press Stop (or your airlabs_toggle key) to finish.");

    Heading("Presets and Custom");
    ImGui::TextWrapped("Presets have fixed settings, so scores can be compared. The targets and ball paths are random every run, so nothing can be learnt by heart. Only preset runs count toward your records. Change any setting and the run becomes Custom: it doesn't count, but you can save it as your own preset (Play tab), which keeps its own personal bests. A preset run also doesn't count if you skip a target. Ball cam, the laser and the aim marker are all fine.");

    Heading("Target size");
    ImGui::TextWrapped("The target is always the ball itself: you're on target when your aim is on the ball, at any distance. A closer ball is a bigger target: Short (400 uu) is 13.6 deg, Medium (600 uu) 9.0 deg, Long (900 uu) 6.0 deg, and Flip Reset (always 350 uu) 15.6 deg. The ring around the ball shows exactly where that edge is.");

    Heading("Ranges");
    ImGui::TextWrapped("Every preset comes in Short, Medium and Long. Each range is its own competition with its own personal bests, graphs and leaderboard, because a closer ball is easier to hit. Flip Reset always uses 350 uu and has one set of records.");

    Heading("Scoring");
    ImGui::BulletText("Flick: each target is worth 1000 points at par time, up to 2000 for twice as fast. No fixed maximum.");
    ImGui::TextWrapped("    Par comes from physics (the fastest possible turn for that angle, plus a human allowance), so big turns aren't punished.");
    ImGui::BulletText("Track: 1000 points per second on target, with partial credit just outside. Max 60,000 in a one-minute run.");
    ImGui::BulletText("Hybrid: flick points plus tracking points for each rep.");
    ImGui::BulletText("Chase: like Track, 1000 points per second on the ball, for the whole minute. Max 60,000.");
    ImGui::BulletText("How good is a score? The Leaderboard page shows where it stands among everyone's.");

    Heading("Leaderboard");
    ImGui::TextWrapped("The Leaderboard page shows everyone's best run on every preset and range (No air roll has its own boards). Your preset runs post there under your Rocket League name: whenever a run beats your best on that board, it goes up with its replay. Don't want that? Press Stop posting on the Leaderboard page. Press Watch next to any run to see it in the game: your car and the ball follow the replay exactly, and Stop (or your airlabs_toggle key) ends it. Your best score on every preset from your saved runs goes up too, including runs from before replays were recorded (those show No replay). Next to each name are that player's Rocket League ranks in 1s, 2s and 3s, as their game reports them. The same boards are on the web too.");

    Heading("Updates");
    ImGui::TextWrapped("AirLabs checks for a new version when it loads and whenever you open this window (at most every 5 minutes); the sidebar says what it found, and Check again checks now. When a new version is out, an Update button appears there instead: press it and BakkesMod installs the new version and reloads the plugin.");

    Heading("Fair timing");
    ImGui::TextWrapped("Runs are timed in physics ticks (120 per second), so frame rate doesn't affect your score and pausing the game pauses the run. The readout (Assists tab) shows the measured tick rate.");

    Heading("Key binds (type in the F6 console)");
    static const char* binds[][2] = {
        { "bind O AL_menu", "open or close this window (any key works)" },
        { "bind F5 airlabs_toggle", "start / stop" },
        { "bind M airlabs_mode_toggle", "Flick -> Track -> Hybrid" },
        { "bind P airlabs_preset_next", "next preset" },
        { "bind L airlabs_laser_toggle", "laser on / off" },
        { "bind N airlabs_next", "skip target (the run then won't count)" },
    };
    float col = 0.f;
    for (const auto& kb : binds) col = std::max(col, ImGui::CalcTextSize(kb[0]).x);
    for (const auto& kb : binds)
    {
        float x = ImGui::GetCursorPosX();
        ImGui::TextUnformatted(kb[0]);
        ImGui::SameLine(x + col + 28.f);
        ImGui::TextDisabled("%s", kb[1]);
    }
}
