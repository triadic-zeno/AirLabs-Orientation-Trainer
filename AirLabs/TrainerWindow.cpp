// The AirLabs window: a standalone overlay (open/close with the airlabs_menu command)
// with its own theme, font and layout. Run logic, scoring and records live in
// AirLabs.cpp; this file only draws and calls into them.
#include "AirLabs.h"
#include "EmbeddedFonts.h"

#include <filesystem>
#include <fstream>
#include "imgui/imgui.h"
#include "bakkesmod/wrappers/GuiManagerWrapper.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

using namespace airlabs;

extern ImFont* g_otHeadingFont; // AirLabs.cpp: section headings use this when set

namespace
{
    constexpr const char* kWinVersion = "2.6.0";

    // ---- palette ----
    constexpr ImU32 Rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }
    ImVec4 Vec(int r, int g, int b, int a = 255) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a / 255.f); }

    const ImU32 kBg        = Rgb(14, 17, 23);
    const ImU32 kSidebar   = Rgb(10, 12, 17);
    const ImU32 kCard      = Rgb(22, 27, 36);
    const ImU32 kCardHi    = Rgb(29, 35, 47);
    const ImU32 kBorder    = Rgb(37, 44, 58);
    const ImU32 kText      = Rgb(230, 234, 241);
    const ImU32 kDim       = Rgb(142, 151, 168);
    const ImU32 kFaint     = Rgb(93, 102, 119);
    const ImU32 kAccent    = Rgb(59, 130, 246);
    const ImU32 kAccentHi  = Rgb(86, 148, 248);
    const ImU32 kAccentBg  = Rgb(59, 130, 246, 46);
    const ImU32 kGood      = Rgb(52, 211, 153);
    const ImU32 kWarn      = Rgb(245, 184, 65);
    const ImU32 kBad       = Rgb(242, 107, 91);
    const ImU32 kGold      = Rgb(245, 196, 81);

    std::string Fmt(const char* fmt, double v)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), fmt, v);
        return buf;
    }
    std::string FmtInt(long long v)
    {
        std::string d = std::to_string(v < 0 ? -v : v), out;
        for (size_t i = 0; i < d.size(); ++i)
        {
            if (i && (d.size() - i) % 3 == 0) out += ',';
            out += d[i];
        }
        return v < 0 ? "-" + out : out;
    }

    // Push a font for a scope (does nothing if the font isn't loaded).
    struct FontScope
    {
        bool on = false;
        explicit FontScope(ImFont* f) { if (f) { ImGui::PushFont(f); on = true; } }
        ~FontScope() { if (on) ImGui::PopFont(); }
    };

    ImFont* Or(ImFont* f) { return f ? f : ImGui::GetFont(); }
    float SizeOf(ImFont* f) { return f ? f->FontSize : ImGui::GetFontSize(); }
    ImVec2 TextSize(ImFont* f, const char* t, float wrap = 0.f) { return Or(f)->CalcTextSizeA(SizeOf(f), FLT_MAX, wrap, t); }
    void DrawText(ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 col, const char* t, float wrap = 0.f)
    {
        dl->AddText(Or(f), SizeOf(f), p, col, t, nullptr, wrap);
    }

    // Small capitals label above a section.
    void SectionLabel(ImFont* f, const char* text)
    {
        ImGui::Dummy(ImVec2(0, 6));
        FontScope fs(f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kFaint));
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    // A rounded card behind whatever is drawn between Begin and End.
    struct Card
    {
        ImDrawList* dl = nullptr;
        ImVec2 start;
        float width = 0.f, pad = 0.f;
        void Begin(float w, float p = 18.f)
        {
            dl = ImGui::GetWindowDrawList();
            width = w;
            pad = p;
            start = ImGui::GetCursorScreenPos();
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + width - 2.f * pad);
        }
        float Inner() const { return width - 2.f * pad; }
        void End(ImU32 bg = kCard, ImU32 border = kBorder)
        {
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImVec2 max(start.x + width, ImGui::GetItemRectMax().y + pad);
            dl->ChannelsSetCurrent(0);
            dl->AddRectFilled(start, max, bg, 10.f);
            dl->AddRect(start, max, border, 10.f, ImDrawCornerFlags_All, 1.f);
            dl->ChannelsMerge();
            ImGui::SetCursorScreenPos(start);
            ImGui::Dummy(ImVec2(width, max.y - start.y));
        }
    };

    // Sidebar entry with a little vector icon. icon: 0 play, 1 stats, 2 assists, 3 help, 4 leaderboard
    bool NavItem(const char* id, const char* label, int icon, bool active, float width, ImFont* font)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = 42.f;
        bool pressed = ImGui::InvisibleButton(id, ImVec2(width, h));
        bool hover = ImGui::IsItemHovered();
        if (active)      dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), kCardHi, 8.f);
        else if (hover)  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), kCard, 8.f);
        if (active) dl->AddRectFilled(ImVec2(p.x, p.y + 10), ImVec2(p.x + 3, p.y + h - 10), kAccent, 2.f);

        ImU32 col = active ? kText : (hover ? kText : kDim);
        ImU32 icol = active ? kAccentHi : col;
        ImVec2 c(p.x + 24.f, p.y + h * 0.5f);
        switch (icon)
        {
        case 0: dl->AddTriangleFilled(ImVec2(c.x - 5, c.y - 7), ImVec2(c.x - 5, c.y + 7), ImVec2(c.x + 7, c.y), icol); break;
        case 1:
            dl->AddRectFilled(ImVec2(c.x - 7, c.y + 1), ImVec2(c.x - 3, c.y + 7), icol, 1.f);
            dl->AddRectFilled(ImVec2(c.x - 2, c.y - 6), ImVec2(c.x + 2, c.y + 7), icol, 1.f);
            dl->AddRectFilled(ImVec2(c.x + 3, c.y - 2), ImVec2(c.x + 7, c.y + 7), icol, 1.f);
            break;
        case 2:
            dl->AddCircle(c, 6.5f, icol, 20, 1.8f);
            dl->AddLine(ImVec2(c.x - 10, c.y), ImVec2(c.x - 4, c.y), icol, 1.8f);
            dl->AddLine(ImVec2(c.x + 4, c.y), ImVec2(c.x + 10, c.y), icol, 1.8f);
            dl->AddLine(ImVec2(c.x, c.y - 10), ImVec2(c.x, c.y - 4), icol, 1.8f);
            dl->AddLine(ImVec2(c.x, c.y + 4), ImVec2(c.x, c.y + 10), icol, 1.8f);
            break;
        case 4: // a podium
            dl->AddRectFilled(ImVec2(c.x - 3, c.y - 6), ImVec2(c.x + 3, c.y + 7), icol, 1.f);
            dl->AddRectFilled(ImVec2(c.x - 9, c.y - 1), ImVec2(c.x - 4, c.y + 7), icol, 1.f);
            dl->AddRectFilled(ImVec2(c.x + 4, c.y + 2), ImVec2(c.x + 9, c.y + 7), icol, 1.f);
            break;
        default:
        {
            dl->AddCircle(c, 8.f, icol, 20, 1.8f);
            ImVec2 qs = TextSize(font, "?");
            DrawText(dl, font, ImVec2(c.x - qs.x * 0.5f, c.y - qs.y * 0.5f), icol, "?");
            break;
        }
        }
        ImVec2 ts = TextSize(font, label);
        DrawText(dl, font, ImVec2(p.x + 46.f, p.y + (h - ts.y) * 0.5f), col, label);
        return pressed;
    }

    // Big selectable tile: title and a one-line description.
    bool Tile(const char* id, const char* title, const char* desc, bool selected, ImVec2 size, ImFont* titleFont, ImFont* descFont)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, size);
        bool hover = ImGui::IsItemHovered();
        ImVec2 q(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, q, selected ? kAccentBg : (hover ? kCardHi : kCard), 10.f);
        dl->AddRect(p, q, selected ? kAccent : kBorder, 10.f, ImDrawCornerFlags_All, selected ? 2.f : 1.f);
        DrawText(dl, titleFont, ImVec2(p.x + 16, p.y + 14), selected ? kText : (hover ? kText : kDim), title);
        DrawText(dl, descFont, ImVec2(p.x + 16, p.y + 16 + SizeOf(titleFont) + 4), selected ? kDim : kFaint, desc, size.x - 32);
        return pressed;
    }

    // extra = room kept free on the right (a rank badge drawn by the caller).
    float ChipWidth(ImFont* f, const char* label, float extra = 0.f) { return TextSize(f, label).x + 34.f + (extra > 0.f ? extra + 8.f : 0.f); }

    // Rounded pill button. outline = a quieter style (used for Custom).
    bool Chip(const char* id, const char* label, bool selected, ImFont* font, float height = 34.f, bool outline = false, float extra = 0.f)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ChipWidth(font, label, extra);
        bool pressed = ImGui::InvisibleButton(id, ImVec2(w, height));
        bool hover = ImGui::IsItemHovered();
        ImVec2 q(p.x + w, p.y + height);
        if (selected)       dl->AddRectFilled(p, q, kAccent, height * 0.5f);
        else if (outline)   dl->AddRect(p, q, hover ? kDim : kBorder, height * 0.5f, ImDrawCornerFlags_All, 1.5f);
        else                dl->AddRectFilled(p, q, hover ? kCardHi : kCard, height * 0.5f);
        if (!selected && !outline) dl->AddRect(p, q, kBorder, height * 0.5f);
        ImVec2 ts = TextSize(font, label);
        const float tx = extra > 0.f ? p.x + 17.f : p.x + (w - ts.x) * 0.5f;
        DrawText(dl, font, ImVec2(tx, p.y + (height - ts.y) * 0.5f), selected ? Rgb(255, 255, 255) : (hover ? kText : kDim), label);
        return pressed;
    }

    // Rank colours: Bronze, Silver, Gold, Platinum, Diamond, Champion, Grand Champion, Supersonic Legend, Pro.
    ImU32 RankColor(int rank)
    {
        static const ImU32 c[9] = { Rgb(176, 122, 74), Rgb(169, 180, 194), Rgb(232, 185, 49), Rgb(63, 193, 201),
                                    Rgb(79, 141, 245), Rgb(155, 89, 245), Rgb(229, 72, 77), Rgb(240, 232, 205), Rgb(255, 170, 40) };
        return c[std::clamp(rank, 0, 8)];
    }

    // Keep the next chip on this line if it fits.
    void SameLineIfRoom(float nextWidth)
    {
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + nextWidth < right) ImGui::SameLine();
    }

    // Small coloured info pill (not clickable).
    void Pill(const char* text, ImFont* font, ImU32 fg, ImU32 bg)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 ts = TextSize(font, text);
        ImVec2 size(ts.x + 20.f, ts.y + 10.f);
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, size.y * 0.5f);
        DrawText(dl, font, ImVec2(p.x + 10.f, p.y + 5.f), fg, text);
        ImGui::Dummy(size);
    }

    // Segmented control: N options in one rounded bar, each with a title and a subtitle.
    int Segmented(const char* id, const char* const* titles, const char* const* subs, int count, int selected,
                  float width, float height, ImFont* titleFont, ImFont* subFont)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), kCard, 10.f);
        dl->AddRect(p, ImVec2(p.x + width, p.y + height), kBorder, 10.f);
        int clicked = -1;
        float segW = width / count;
        for (int i = 0; i < count; ++i)
        {
            ImVec2 a(p.x + segW * i, p.y), b(p.x + segW * (i + 1), p.y + height);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(id);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("seg", ImVec2(segW, height))) clicked = i;
            bool hover = ImGui::IsItemHovered();
            ImGui::PopID();
            ImGui::PopID();
            if (i == selected) dl->AddRectFilled(ImVec2(a.x + 4, a.y + 4), ImVec2(b.x - 4, b.y - 4), kAccent, 7.f);
            else if (hover)    dl->AddRectFilled(ImVec2(a.x + 4, a.y + 4), ImVec2(b.x - 4, b.y - 4), kCardHi, 7.f);
            float th = SizeOf(titleFont), sh = subs ? SizeOf(subFont) : 0.f;
            float top = a.y + (height - th - (subs ? sh + 2.f : 0.f)) * 0.5f;
            ImVec2 ts = TextSize(titleFont, titles[i]);
            DrawText(dl, titleFont, ImVec2(a.x + (segW - ts.x) * 0.5f, top), i == selected ? Rgb(255, 255, 255) : (hover ? kText : kDim), titles[i]);
            if (subs)
            {
                ImVec2 ss = TextSize(subFont, subs[i]);
                DrawText(dl, subFont, ImVec2(a.x + (segW - ss.x) * 0.5f, top + th + 2.f), i == selected ? Rgb(220, 232, 255) : kFaint, subs[i]);
            }
        }
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(width, height));
        return clicked;
    }

    // Large call-to-action button with a play or stop glyph.
    bool BigButton(const char* id, const char* label, bool stop, ImVec2 size, ImFont* font)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton(id, size);
        bool hover = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
        ImU32 base = stop ? kBad : kAccent;
        ImU32 col = held ? (stop ? Rgb(214, 86, 72) : Rgb(47, 111, 216)) : (hover ? (stop ? Rgb(248, 128, 112) : kAccentHi) : base);
        ImVec2 q(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, q, col, 12.f);
        ImVec2 ts = TextSize(font, label);
        float glyph = 16.f, gap = 12.f;
        float x = p.x + (size.x - (glyph + gap + ts.x)) * 0.5f, cy = p.y + size.y * 0.5f;
        if (stop) dl->AddRectFilled(ImVec2(x, cy - 7), ImVec2(x + 14, cy + 7), Rgb(255, 255, 255), 2.f);
        else      dl->AddTriangleFilled(ImVec2(x, cy - 9), ImVec2(x, cy + 9), ImVec2(x + glyph, cy), Rgb(255, 255, 255));
        DrawText(dl, font, ImVec2(x + glyph + gap, cy - ts.y * 0.5f), Rgb(255, 255, 255), label);
        return pressed;
    }

    // Text-only button (for small secondary actions).
    bool LinkButton(const char* id, const char* label, ImFont* font, ImU32 col = kDim)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 ts = TextSize(font, label);
        bool pressed = ImGui::InvisibleButton(id, ImVec2(ts.x + 4, ts.y + 4));
        bool hover = ImGui::IsItemHovered();
        DrawText(dl, font, ImVec2(p.x + 2, p.y + 2), hover ? kText : col, label);
        if (hover) dl->AddLine(ImVec2(p.x + 2, p.y + ts.y + 3), ImVec2(p.x + 2 + ts.x, p.y + ts.y + 3), kText);
        return pressed;
    }

    void Dot(ImU32 col)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetTextLineHeight();
        dl->AddCircleFilled(ImVec2(p.x + 5, p.y + h * 0.5f), 4.5f, col, 16);
        ImGui::Dummy(ImVec2(14, h));
        ImGui::SameLine(0, 4);
    }

    // ---- theme ----
    int PushTheme()
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kBg));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Vec(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertU32ToFloat4(kCard));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kBorder));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kText));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(kDim));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Vec(31, 38, 50));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Vec(40, 49, 66));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Vec(46, 57, 77));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImGui::ColorConvertU32ToFloat4(kAccent));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImGui::ColorConvertU32ToFloat4(kAccentHi));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(kAccentHi));
        ImGui::PushStyleColor(ImGuiCol_Button, Vec(31, 38, 50));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Vec(40, 49, 66));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(kAccent));
        ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(kCard));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::ColorConvertU32ToFloat4(kCardHi));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::ColorConvertU32ToFloat4(kAccentBg));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImGui::ColorConvertU32ToFloat4(kBorder));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, Vec(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, Vec(44, 52, 68));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, Vec(60, 70, 90));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, Vec(70, 82, 104));
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ImGui::ColorConvertU32ToFloat4(kAccent));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertU32ToFloat4(kAccent));
        ImGui::PushStyleColor(ImGuiCol_Tab, ImGui::ColorConvertU32ToFloat4(kCard));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImGui::ColorConvertU32ToFloat4(kCardHi));
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImGui::ColorConvertU32ToFloat4(kAccentBg));
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip, Vec(0, 0, 0, 0));
        return 29;
    }
    int PushThemeVars()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 7));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 9));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 7.f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 14.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 10.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.f);
        return 12;
    }

    const char* kModeTileDesc[kModeCount] = {
        "Turn onto each ball as fast as you can.",
        "Keep your aim on a ball that moves around you.",
        "Flick onto the ball, then track it as it takes off.",
        "Reach the ball and it dashes off, then stops. Catch it again.",
    };
}

// ===========================================================================
// Window plumbing
// ===========================================================================

void AirLabs::LoadFonts()
{
    // The font files are compiled into the DLL. BakkesMod loads fonts from bakkesmod/data/fonts,
    // so write them there first if they're missing or out of date. If anything fails the
    // window still works, in BakkesMod's default font.
    try
    {
        std::filesystem::path dir = std::filesystem::path(gameWrapper->GetDataFolderW()) / "fonts";
        std::filesystem::create_directories(dir);
        for (const auto& f : fonts::kAll)
        {
            std::filesystem::path p = dir / f.file;
            std::error_code ec;
            if (std::filesystem::exists(p, ec) && std::filesystem::file_size(p, ec) == f.size) continue;
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(f.data), static_cast<std::streamsize>(f.size));
        }
    }
    catch (...) { cvarManager->log("[AirLabs] couldn't write the window font to bakkesmod/data/fonts."); }
    static const char* names[kFontCount] = { "airlabs_small", "airlabs_body", "airlabs_bold", "airlabs_heading", "airlabs_title", "airlabs_huge" };
    static const char* files[kFontCount] = {
        "AirLabs-Inter-Regular.ttf", "AirLabs-Inter-Regular.ttf", "AirLabs-Inter-SemiBold.ttf",
        "AirLabs-Inter-SemiBold.ttf", "AirLabs-Inter-Bold.ttf", "AirLabs-Inter-Bold.ttf",
    };
    static const int sizes[kFontCount] = { 15, 17, 17, 21, 28, 40 };
    GuiManagerWrapper gui = gameWrapper->GetGUIManager();
    for (int i = 0; i < kFontCount; ++i)
    {
        auto [status, font] = gui.LoadFont(names[i], files[i], sizes[i]);
        if (status == 2 && font) uiFonts[i] = font;
        else if (status == 0 && i == 0)
            cvarManager->log("[AirLabs] couldn't load the window font (bakkesmod/data/fonts/AirLabs-Inter-*.ttf); using the default font.");
    }
}

void AirLabs::EnsureFonts()
{
    // Fonts queued at load become available a frame or two later.
    if (fontsReady) return;
    static const char* names[kFontCount] = { "airlabs_small", "airlabs_body", "airlabs_bold", "airlabs_heading", "airlabs_title", "airlabs_huge" };
    GuiManagerWrapper gui = gameWrapper->GetGUIManager();
    bool all = true;
    for (int i = 0; i < kFontCount; ++i)
    {
        if (!uiFonts[i]) uiFonts[i] = gui.GetFont(names[i]);
        if (!uiFonts[i]) all = false;
    }
    fontsReady = all || ++fontTries > 600; // stop asking after ~10 s if they never arrive
}

void AirLabs::ToggleWindow()
{
    cvarManager->executeCommand("togglemenu " + GetMenuName(), false);
}

bool AirLabs::ShouldBlockInput()
{
    return ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantCaptureKeyboard;
}

void AirLabs::Render()
{
    EnsureFonts();
    // Default size fits a 1080p screen comfortably. (The window's ID changed in 1.8.0 so this
    // larger default applies even if the old window size was remembered.)
    ImGui::SetNextWindowSize(ImVec2(1340, 940), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(860, 560), ImVec2(4000, 4000));
    int colors = PushTheme();
    int vars = PushThemeVars();
    if (ImGui::Begin("AirLabs##otwindow2", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        DrawTrainerWindow();
    ImGui::End();
    ImGui::PopStyleVar(vars);
    ImGui::PopStyleColor(colors);
    if (closeRequested)
    {
        closeRequested = false;
        ToggleWindow();
    }
}

void AirLabs::DrawTrainerWindow()
{
    FontScope body(uiFonts[kFontBody]);
    g_otHeadingFont = uiFonts[kFontHeading];
    const float sidebarW = 214.f;
    const float height = ImGui::GetContentRegionAvail().y;

    // ---- sidebar ----
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + sidebarW, p.y + height), kSidebar, 12.f, ImDrawCornerFlags_Left);
        dl->AddLine(ImVec2(p.x + sidebarW, p.y), ImVec2(p.x + sidebarW, p.y + height), kBorder);
        ImGui::BeginChild("##otsidebar", ImVec2(sidebarW, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        WinSidebar(height);
        ImGui::EndChild();
    }
    ImGui::SameLine(0, 0);

    // ---- page ----
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30, 24));
    ImGui::BeginChild("##otpage", ImVec2(0, height), false, ImGuiWindowFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    {
        // Close button, top right.
        ImVec2 keep = ImGui::GetCursorPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        float wx = ImGui::GetWindowWidth();
        ImVec2 c(wp.x + wx - 30.f, wp.y + 26.f - ImGui::GetScrollY() * 0.f);
        ImGui::SetCursorScreenPos(ImVec2(c.x - 14, c.y - 14));
        if (ImGui::InvisibleButton("##otclose", ImVec2(28, 28))) closeRequested = true;
        bool hover = ImGui::IsItemHovered();
        if (hover) dl->AddCircleFilled(c, 14.f, kCardHi, 24);
        ImU32 xc = hover ? kText : kDim;
        dl->AddLine(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), xc, 2.f);
        dl->AddLine(ImVec2(c.x + 5, c.y - 5), ImVec2(c.x - 5, c.y + 5), xc, 2.f);
        ImGui::SetCursorPos(keep);
    }
    switch (uiPage)
    {
    case 1:  WinStats();   break;
    case 2:  WinAssists(); break;
    case 3:  WinHelp();    break;
    case 4:  WinLeaderboard(); break;
    default: WinPlay();    break;
    }
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::EndChild();
    g_otHeadingFont = nullptr;
}

void AirLabs::EnsureRankIcons()
{
    if (rankIconsTried) return;
    rankIconsTried = true;
    static const char* group[9] = { "bronze", "silver", "gold", "platinum", "diamond", "champion", "grand_champion", "supersonic_legend", "pro" };
    std::shared_ptr<ImageWrapper> shared[9];
    const std::filesystem::path dir = DataDir() / "ranks";
    std::error_code uec;
    if (std::filesystem::exists(dir / "unranked.png", uec)) unrankedIcon = std::make_shared<ImageWrapper>(dir / "unranked.png", false, true);
    for (int t = 0; t < kTierCount; ++t)
    {
        const int rk = TierRank(t);
        const bool single = rk >= 7; // SSL and Pro have no divisions
        std::error_code ec;
        std::filesystem::path own = dir / (single ? std::string(group[rk]) + ".png" : std::string(group[rk]) + "_" + std::to_string(t % 3 + 1) + ".png");
        if (std::filesystem::exists(own, ec)) { rankIcon[t] = std::make_shared<ImageWrapper>(own, false, true); continue; }
        std::filesystem::path one = dir / (std::string(group[rk]) + ".png"); // one image for the whole rank
        if (std::filesystem::exists(one, ec))
        {
            if (!shared[rk]) shared[rk] = std::make_shared<ImageWrapper>(one, false, true);
            rankIcon[t] = shared[rk];
            rankIconShared[t] = !single;
        }
    }
}

// The badge for a preset button or a label: your rank's icon (or a coloured "GC1" badge), the
// unranked icon and "3/10" while placing, nothing before your first run. h is the text badge's
// height; icons are drawn a little bigger (centred on the same line). Returns its width; draws at p.
float AirLabs::RankBadge(const RankState& rs, float h, ImFont* font, ImDrawList* dl, ImVec2 p)
{
    if (rs.runs <= 0) return 0.f;
    const bool draw = dl != nullptr;
    const float icon = h + 8.f;
    const float iconY = p.y + (h - icon) * 0.5f;
    EnsureRankIcons();
    if (RankPlacing(rs))
    {
        std::string txt = std::to_string(rs.runs) + "/" + std::to_string(kPlacementRuns);
        const bool haveIcon = unrankedIcon && unrankedIcon->IsLoadedForImGui();
        if (unrankedIcon && !haveIcon) unrankedIcon->GetImGuiTex(); // starts loading
        const float iw = haveIcon ? icon + 3.f : 0.f;
        const float w = iw + TextSize(font, txt.c_str()).x + 12.f;
        if (draw)
        {
            if (haveIcon)
                if (void* tex = unrankedIcon->GetImGuiTex()) dl->AddImage(tex, ImVec2(p.x, iconY), ImVec2(p.x + icon, iconY + icon));
            dl->AddRectFilled(ImVec2(p.x + iw, p.y), ImVec2(p.x + w, p.y + h), Rgb(48, 55, 70), h * 0.5f);
            DrawText(dl, font, ImVec2(p.x + iw + 6.f, p.y + (h - SizeOf(font)) * 0.5f), kDim, txt.c_str());
        }
        return w;
    }
    const int t = TierFromValue(rs.value);
    if (rankIcon[t] && rankIcon[t]->IsLoadedForImGui())
    {
        if (draw)
        {
            if (void* tex = rankIcon[t]->GetImGuiTex()) dl->AddImage(tex, ImVec2(p.x, iconY), ImVec2(p.x + icon, iconY + icon));
            if (rankIconShared[t]) // the division as pips under the icon
                for (int d = 0; d <= t % 3; ++d)
                    dl->AddCircleFilled(ImVec2(p.x + icon * 0.5f + (d - (t % 3) * 0.5f) * 5.f, iconY + icon - 1.5f), 1.8f, Rgb(255, 255, 255), 8);
        }
        return icon;
    }
    if (rankIcon[t]) rankIcon[t]->GetImGuiTex(); // starts loading; drawn from the next frame
    const char* txt = TierShort(t);
    const float w = TextSize(font, txt).x + 12.f;
    if (draw)
    {
        const ImU32 col = RankColor(TierRank(t));
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col, 5.f);
        dl->AddRect(ImVec2(p.x - 1.f, p.y - 1.f), ImVec2(p.x + w + 1.f, p.y + h + 1.f), Rgb(10, 12, 17, 220), 6.f, ImDrawCornerFlags_All, 2.f);
        DrawText(dl, font, ImVec2(p.x + 6.f, p.y + (h - SizeOf(font)) * 0.5f), TierRank(t) >= 7 || TierRank(t) <= 3 ? Rgb(20, 22, 28) : Rgb(255, 255, 255), txt);
    }
    return w;
}

// A preset button with your rank on it: "Control  [GC1]".
float AirLabs::RankChipWidth(const char* label, ImFont* font, ImFont* badgeFont, const RankState& rs, float height)
{
    return ChipWidth(font, label, RankBadge(rs, height - 12.f, badgeFont, nullptr, ImVec2()));
}

bool AirLabs::RankChip(const char* id, const char* label, bool selected, ImFont* font, ImFont* badgeFont, const RankState& rs, float height)
{
    const float bh = height - 12.f;
    const float bw = RankBadge(rs, bh, badgeFont, nullptr, ImVec2());
    const bool pressed = Chip(id, label, selected, font, height, false, bw);
    if (bw > 0.f)
    {
        const ImVec2 q = ImGui::GetItemRectMax();
        RankBadge(rs, bh, badgeFont, ImGui::GetWindowDrawList(), ImVec2(q.x - 10.f - bw, q.y - height + 6.f));
    }
    return pressed;
}

// Space | Standard: where START takes you. A pill with a sliding knob behind the chosen side.
// Choosing only sets the destination (airlabs_map); START does the travelling.
void AirLabs::MapSwitch(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool standard = mapPref && *mapPref == 1;
    // knob position eases towards the chosen side (0 = Space, 1 = Standard)
    const float target = standard ? 1.f : 0.f;
    if (mapKnob < 0.f) mapKnob = target; // first time shown: start where the setting is
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, 0.1f);
    mapKnob += (target - mapKnob) * std::min(1.f, dt * 14.f);
    if (std::fabs(target - mapKnob) < 0.002f) mapKnob = target;

    const bool pressed = ImGui::InvisibleButton("##mapswitch_toggle", ImVec2(w, h));
    const bool hover = ImGui::IsItemHovered();
    const float r = h * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hover ? kCardHi : kBg, r);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), kBorder, r, ImDrawCornerFlags_All, 1.5f);
    const float half = w * 0.5f, inset = 4.f;
    const float kx = p.x + inset + mapKnob * (half - inset);
    dl->AddRectFilled(ImVec2(kx, p.y + inset), ImVec2(kx + half - inset, p.y + h - inset), kAccent, r - inset);
    ImFont* f = uiFonts[kFontBold];
    const char* labels[2] = { "Space", "Standard" };
    for (int i = 0; i < 2; ++i)
    {
        const ImVec2 ts = TextSize(f, labels[i]);
        const bool on = (i == 1) == standard;
        DrawText(dl, f, ImVec2(p.x + half * i + (half - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), on ? Rgb(255, 255, 255) : kDim, labels[i]);
    }
    if (hover) ImGui::SetTooltip("Where START takes you: the space map, or Freeplay on the standard field");
    if (pressed)
    {
        // click a side to pick it; clicking the side that's already on flips it
        const float mx = ImGui::GetIO().MousePos.x;
        int pick = mx < p.x + half ? 0 : 1;
        if (pick == (standard ? 1 : 0)) pick = 1 - pick;
        SetCvarI("airlabs_map", pick);
    }
}

void AirLabs::WinSidebar(float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetWindowWidth();

    // Logo: a target ring, and the name.
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + 34.f, p.y + 40.f);
    dl->AddCircle(c, 13.f, kAccent, 32, 2.5f);
    dl->AddCircle(c, 6.f, kAccentHi, 24, 2.f);
    dl->AddCircleFilled(c, 2.2f, Rgb(255, 255, 255), 12);
    DrawText(dl, uiFonts[kFontBold], ImVec2(p.x + 58.f, c.y - SizeOf(uiFonts[kFontBold]) * 0.5f - 1.f), kText, "AirLabs");
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 92.f));

    // Pages in sidebar order (the numbers are uiPage values).
    static const int order[5] = { 0, 1, 4, 2, 3 };
    static const char* labels[5] = { "Play", "Stats", "Assists", "Help", "Leaderboard" };
    for (int k = 0; k < 5; ++k)
    {
        const int i = order[k];
        ImGui::SetCursorPosX(12.f);
        ImGui::PushID(i);
        if (NavItem("##nav", labels[i], i, uiPage == i, w - 24.f, uiFonts[kFontBold])) uiPage = i;
        ImGui::PopID();
    }

    // Status and key hint at the bottom.
    const float boxH = 104.f;
    ImVec2 top(p.x + 12.f, p.y + height - boxH - 12.f);
    ImVec2 bot(p.x + w - 12.f, p.y + height - 12.f);
    dl->AddRectFilled(top, bot, kCard, 10.f);
    dl->AddRect(top, bot, kBorder, 10.f);
    ImU32 dotCol = kFaint;
    std::string status = "Not running";
    if (state == RunState::Ready)   { dotCol = kWarn; status = showResults ? "Run finished" : "Point at the ball"; }
    if (state == RunState::Running) { dotCol = kGood; status = runEnd < 1e17 ? "Running  " + std::string(Fmt("%.0f s left", std::max(0.0, runEnd - gameTime))) : "Running"; }
    if (state == RunState::Replay)  { dotCol = kAccentHi; status = "Watching a replay"; }
    dl->AddCircleFilled(ImVec2(top.x + 18.f, top.y + 22.f), 5.f, dotCol, 16);
    DrawText(dl, uiFonts[kFontBold], ImVec2(top.x + 30.f, top.y + 12.f), kText, status.c_str());
    DrawText(dl, uiFonts[kFontSmall], ImVec2(top.x + 14.f, top.y + 44.f), kDim, "Open or close this window:");
    DrawText(dl, uiFonts[kFontBold], ImVec2(top.x + 14.f, top.y + 62.f), kText, "bind O airlabs_menu");
    if (state == RunState::Idle) // the running status is wider, so the version only shows when idle
        DrawText(dl, uiFonts[kFontSmall], ImVec2(bot.x - TextSize(uiFonts[kFontSmall], kWinVersion).x - 12.f, top.y + 14.f), kFaint, kWinVersion);
}

// ===========================================================================
// Play
// ===========================================================================

void AirLabs::WinPlay()
{
    const DrillSettings s = CurrentSettings();
    const float width = ImGui::GetContentRegionAvail().x - 34.f; // keep clear of the close button
    ImFont* small = uiFonts[kFontSmall];
    ImFont* bold = uiFonts[kFontBold];

    // ---- up next: what Start will play, and the button itself ----
    {
        int best = -1;
        const bool mine = s.custom && s.userPreset > 0; // a saved preset: has its own records
        if (!s.custom || mine)
        {
            std::lock_guard<std::mutex> lock(recordsMutex);
            auto it = bestScore.find(RunKey(s.mode, s.preset, s.range, mine ? s.userPreset : 0, s.noRoll));
            if (it != bestScore.end()) best = it->second;
        }
        const float btnW = 190.f, btnH = 64.f;
        const float mapW = 208.f, mapH = 40.f, mapGap = 14.f; // the Space | Standard switch left of START
        const bool showMapSwitch = haveSpaceMap;
        Card card;
        card.Begin(width, 22.f);
        ImVec2 cardTop = ImGui::GetCursorScreenPos();
        float textW = card.Inner() - btnW - 24.f - (showMapSwitch ? mapW + mapGap : 0.f);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textW);
        {
            FontScope f(small);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kFaint));
            ImGui::TextUnformatted(state == RunState::Running ? "PLAYING NOW" : "UP NEXT");
            ImGui::PopStyleColor();
        }
        {
            FontScope f(uiFonts[kFontTitle]);
            std::string title = std::string(ModeName(s.mode)) + "  \xC2\xB7  " +
                                (mine ? SavedPresetName(s.userPreset) : s.custom ? std::string("Custom") : PresetLabel(s.mode, s.preset));
            ImGui::TextUnformatted(title.c_str());
        }
        // Key facts as pills.
        {
            std::string where = s.custom ? Fmt("%.0f uu", s.distance)
                                         : (PresetUsesRange(s.mode, s.preset) ? std::string(RangeName(s.range)) + Fmt(" range \xC2\xB7 %.0f uu", s.distance)
                                                                              : Fmt("Fixed \xC2\xB7 %.0f uu", s.distance));
            std::string what;
            if (s.mode == kFlick) what = Fmt("Turns %.0f", s.minTurn) + Fmt("-%.0f\xC2\xB0", s.maxTurn);
            else if (s.path.dynamic) what = Fmt("%.0f", s.path.vMin) + Fmt("-%.0f\xC2\xB0/s", s.path.vMax) + (s.mode == kChase ? " per dash" : "");
            else what = Fmt("%.0f\xC2\xB0/s", s.path.speed);
            std::vector<std::string> pills = { where, Fmt("Target %.1f\xC2\xB0", s.target), what,
                                               s.runLength > 0.f ? Fmt("%.0f s run", s.runLength) : std::string("Endless") };
            if (s.aim == 1) pills.push_back("Wheels aim");
            if (s.noRoll) pills.push_back("No air roll");
            const int n = static_cast<int>(pills.size());
            const float limit = ImGui::GetCursorScreenPos().x + textW; // wrap before reaching the Start button
            for (int i = 0; i < n; ++i)
            {
                if (i && ImGui::GetItemRectMax().x + 6.f + TextSize(small, pills[i].c_str()).x + 20.f <= limit) ImGui::SameLine(0, 6);
                Pill(pills[i].c_str(), small, kText, kCardHi);
            }
        }
        ImGui::Dummy(ImVec2(0, 2));
        if (mine)
        {
            Dot(kGood);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGood), "My preset: counts toward its own records");
            ImGui::SameLine(0, 18);
            if (best >= 0) { FontScope f(bold); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGold), "Best %s", FmtInt(best).c_str()); }
            else ImGui::TextDisabled("No best yet");
        }
        else if (s.custom) { Dot(kWarn); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "Custom: practice only until you save it"); }
        else
        {
            Dot(kGood);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGood), "Counts toward your records");
            ImGui::SameLine(0, 18);
            if (best >= 0)
            {
                FontScope f(bold);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGold), "Best %s  \xC2\xB7  %s", FmtInt(best).c_str(),
                                   TierName(TierIndex(static_cast<float>(best), s.mode, s.preset, s.range)));
            }
            else ImGui::TextDisabled("No best yet");
            // Your rank on this preset (weighted average of your runs).
            const RankState rs = RankFor(s.mode, s.preset, s.range, 0, s.noRoll);
            ImGui::AlignTextToFramePadding();
            if (rs.runs == 0) ImGui::TextDisabled("No rank yet: your first %d runs are placements", kPlacementRuns);
            else
            {
                FontScope f(bold);
                ImGui::TextUnformatted(RankPlacing(rs) ? "Placing" : "Your rank");
                ImGui::SameLine(0, 8);
                const ImVec2 at = ImGui::GetCursorScreenPos();
                const float bw = RankBadge(rs, 22.f, small, ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + 1.f));
                ImGui::Dummy(ImVec2(bw, 22.f));
                ImGui::SameLine(0, 8);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(RankColor(TierRank(TierFromValue(rs.value)))), "%s%s",
                                   RankPlacing(rs) ? "so far " : "", TierName(TierFromValue(rs.value)));
            }
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        float leftH = ImGui::GetItemRectSize().y;

        // The button, vertically centred on the right.
        ImVec2 btnPos(cardTop.x + card.Inner() - btnW, cardTop.y + std::max(0.f, (leftH - btnH) * 0.5f) - (state == RunState::Running && s.mode != kTrack && s.mode != kChase ? 12.f : 0.f));
        ImGui::SetCursorScreenPos(btnPos);
        const bool running = state == RunState::Ready || state == RunState::Running || state == RunState::Replay;
        if (BigButton("##otstart", running ? "STOP" : "START", running, ImVec2(btnW, btnH), uiFonts[kFontHeading]))
        {
            cvarManager->executeCommand("airlabs_toggle");
            if (!running) closeRequested = true; // straight into the run (or to the map it loads)
        }
        if (showMapSwitch)
        {
            ImGui::SetCursorScreenPos(ImVec2(btnPos.x - mapGap - mapW, btnPos.y + (btnH - mapH) * 0.5f));
            MapSwitch(mapW, mapH);
        }
        if (state == RunState::Running && s.mode != kTrack && s.mode != kChase)
        {
            ImGui::SetCursorScreenPos(ImVec2(btnPos.x + btnW * 0.5f - 40.f, btnPos.y + btnH + 6.f));
            if (LinkButton("##otskip", "Skip target", small)) gameWrapper->Execute([this](GameWrapper*) { SkipTarget(); });
        }
        ImGui::SetCursorScreenPos(ImVec2(cardTop.x, cardTop.y + std::max(leftH, btnH + (state == RunState::Running ? 26.f : 0.f))));
        ImGui::Dummy(ImVec2(0, 0));
        card.End(kCard, kBorder);
        if (state == RunState::Idle)
        {
            FontScope f(small);
            ImGui::TextDisabled(WantSpaceMap() ? "Start takes you to the space map. Point at the ball for 1.5 s to begin."
                                               : "Start takes you to Freeplay on a standard field. Point at the ball for 1.5 s to begin.");
        }
    }


    // ---- mode ----
    SectionLabel(small, "MODE");
    {
        const float gap = 12.f, tileW = (width - gap * static_cast<float>(kModeCount - 1)) / static_cast<float>(kModeCount);
        for (int m = 0; m < kModeCount; ++m)
        {
            if (m) ImGui::SameLine(0, gap);
            ImGui::PushID(m);
            if (Tile("##mode", ModeName(m), kModeTileDesc[m], s.mode == m, ImVec2(tileW, 82.f), uiFonts[kFontHeading], small)) SetCvarI("airlabs_mode", m);
            ImGui::PopID();
        }
        // No air roll: a variant of every mode, with its own records.
        ImGui::Dummy(ImVec2(0, 2));
        const bool noRollOn = noAirRoll && *noAirRoll;
        if (Chip("##noroll", noRollOn ? "No air roll: ON" : "No air roll: OFF", noRollOn, bold))
            SetCvarI("airlabs_noroll", noRollOn ? 0 : 1);
        ImGui::SameLine(0, 12);
        {
            FontScope f(small);
            ImGui::AlignTextToFramePadding();
            if (noRollOn && s.aim == 1)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "Not used with wheels aim: aiming with your wheels needs air roll.");
            else
                ImGui::TextDisabled("Air roll does nothing during runs: turn with pitch and yaw only. Scores go to separate No air roll records.");
        }
    }

    // ---- preset ----
    SectionLabel(small, "PRESET");
    {
        const int count = PresetCount(s.mode);
        const bool dynSelected = !s.custom && PresetInDynamicGroup(s.mode, s.preset);
        int firstDynamic = -1;
        bool first = true;
        ImGui::PushID("chips");
        for (int p = 0; p < count; ++p)
        {
            if (PresetInDynamicGroup(s.mode, p)) { if (firstDynamic < 0) firstDynamic = p; continue; }
            const RankState rs = RankFor(s.mode, p, s.range, 0, s.noRoll);
            if (!first) SameLineIfRoom(RankChipWidth(PresetName(s.mode, p), bold, small, rs, 34.f));
            first = false;
            ImGui::PushID(p);
            if (RankChip("##c", PresetName(s.mode, p), !s.custom && s.preset == p, bold, small, rs, 34.f)) SetCvarI(PresetCvar(s.mode), p);
            ImGui::PopID();
        }
        if (firstDynamic >= 0)
        {
            SameLineIfRoom(ChipWidth(bold, "Dynamic"));
            if (Chip("##dyn", "Dynamic", dynSelected, bold) && !dynSelected) SetCvarI(PresetCvar(s.mode), firstDynamic);
        }
        SameLineIfRoom(ChipWidth(bold, "Custom"));
        if (Chip("##custom", "Custom", s.custom && s.userPreset == 0, bold, 34.f, true) && !(s.custom && s.userPreset == 0))
        {
            if (s.custom) MakeCustom(); // leaving a saved preset: keep its settings as plain Custom
            else { MakeCustom(); showCustomise = true; }
        }
        if (dynSelected)
        {
            ImGui::Dummy(ImVec2(0, 0));
            {
                FontScope f(small);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Speed range");
            }
            for (int p = firstDynamic; p < count; ++p)
            {
                ImGui::SameLine(0, 8);
                ImGui::PushID(200 + p);
                const RankState rs = RankFor(s.mode, p, s.range, 0, s.noRoll);
                if (RankChip("##d", PresetName(s.mode, p), s.preset == p, small, small, rs, 28.f)) SetCvarI(PresetCvar(s.mode), p);
                ImGui::PopID();
            }
        }
        ImGui::PopID();

        // ---- my presets: saved ones for this mode, in their own row ----
        std::vector<std::pair<int, std::string>> mineList;
        {
            std::lock_guard<std::mutex> lock(presetsMutex);
            for (const auto& sp : savedPresets)
                if (sp.mode == s.mode) mineList.emplace_back(sp.id, sp.name);
        }
        if (!mineList.empty())
        {
            ImGui::Dummy(ImVec2(0, 0));
            {
                FontScope f(small);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("My presets");
            }
            ImGui::PushID("mine");
            for (size_t i = 0; i < mineList.size(); ++i)
            {
                const auto& mp = mineList[i];
                if (i) SameLineIfRoom(ChipWidth(bold, mp.second.c_str()));
                else ImGui::SameLine(0, 10);
                ImGui::PushID(mp.first);
                if (Chip("##m", mp.second.c_str(), s.custom && s.userPreset == mp.first, bold, 32.f)) SelectSavedPreset(s.mode, mp.first);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        {
            FontScope f(small);
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + width);
            if (s.custom && s.userPreset > 0)
                ImGui::TextDisabled("Your saved preset. Its runs count toward its own personal bests and progress (no tiers). It's locked: change a setting and it becomes Custom, which you can save as a new preset.");
            else
                ImGui::TextDisabled("%s", PresetBlurb(s.mode, s.custom ? count : s.preset));
            ImGui::PopTextWrapPos();
        }
        if (s.custom && s.userPreset > 0)
        {
            // Delete needs a second click, so a stray click can't remove a preset.
            bool confirming = confirmDeleteId == s.userPreset;
            if (LinkButton("##del", confirming ? "Click again to delete this preset (its runs stay in History)" : "Delete this preset", small, confirming ? kBad : kDim))
            {
                if (confirming) { DeleteSavedPreset(s.userPreset); confirmDeleteId = 0; }
                else confirmDeleteId = s.userPreset;
            }
        }
        else if (s.custom)
        {
            int base = std::clamp(BaseIndex(s.mode), 0, count - 1);
            std::string back = "Back to " + PresetLabel(s.mode, base);
            if (LinkButton("##back", back.c_str(), small, kAccentHi)) SetCvarI(PresetCvar(s.mode), base);
        }
    }

    // ---- range ----
    SectionLabel(small, "RANGE");
    if (s.custom || PresetUsesRange(s.mode, s.preset))
    {
        static const char* titles[kRangeCount] = { "Short", "Medium", "Long" };
        std::string subsS[kRangeCount];
        const char* subs[kRangeCount];
        for (int r = 0; r < kRangeCount; ++r)
        {
            subsS[r] = Fmt("%.0f uu", RangeDistance(r)) + Fmt("  \xC2\xB7  %.1f\xC2\xB0 target", BallTargetDeg(RangeDistance(r)));
            subs[r] = subsS[r].c_str();
        }
        int sel = s.custom ? -1 : s.range;
        if (s.custom)
            for (int r = 0; r < kRangeCount; ++r)
                if (std::fabs(s.distance - RangeDistance(r)) < 0.5f) sel = r;
        int clicked = Segmented("##range", titles, subs, kRangeCount, sel, width, 60.f, bold, small);
        if (clicked >= 0)
        {
            SetCvarI("airlabs_range", clicked);
            if (s.custom) { MakeCustom(); SetCvarF("airlabs_distance", RangeDistance(clicked)); } // edits a saved preset into Custom
        }
        FontScope f(small);
        ImGui::TextDisabled(s.userPreset > 0 ? "Sets the ball distance. Changing it turns this preset into Custom based on it."
                          : s.custom ? "Sets Custom's ball distance. Save it as a preset below to keep records for it."
                                     : "Each range has its own records: a closer ball is a bigger target.");
    }
    else
    {
        Card card;
        card.Begin(width, 14.f);
        ImGui::TextDisabled("Flip Reset always puts the ball %.0f uu away (target %.1f\xC2\xB0), so it has one set of records.", s.distance, s.target);
        card.End();
    }

    // ---- round: length and time per target, right on the Play page ----
    SectionLabel(small, "ROUND");
    {
        const bool hasTimeout = s.mode != kTrack; // Track has no targets to time out
        const float gap = 16.f, colW = hasTimeout ? (width - gap) * 0.5f : width;
        auto slider = [&](const char* id, const char* title, const char* cvar, float value, float lo, float hi, const char* fmt, float w) {
            ImGui::BeginGroup();
            {
                FontScope f(bold);
                ImGui::TextUnformatted(title);
            }
            ImGui::PushItemWidth(w);
            float v = value;
            if (ImGui::SliderFloat(id, &v, lo, hi, fmt))
            {
                MakeCustom(); // a preset's round is fixed so scores compare: changing it makes the run Custom
                SetCvarF(cvar, v);
            }
            ImGui::PopItemWidth();
            ImGui::EndGroup();
        };
        slider("##roundlen", "Round length", "airlabs_run_length", s.runLength, 0.f, 600.f, s.runLength > 0.f ? "%.0f s" : "Endless", colW);
        if (hasTimeout)
        {
            ImGui::SameLine(0, gap);
            slider("##timeout", s.mode == kChase ? "Time to catch the ball" : "Time per target", "airlabs_timeout", s.timeout, 0.f, 30.f,
                   s.timeout > 0.f ? "%.1f s" : "No limit", colW);
        }
        FontScope f(small);
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + width);
        ImGui::TextDisabled(hasTimeout ? "Presets: 60 s rounds, %s. A missed target costs %.0f points. Changing either makes the run Custom (save it as a preset to keep records). 0 = endless / no limit."
                                       : "Presets: 60 s rounds. Changing it makes the run Custom (save it as a preset to keep records). 0 = endless.",
                            s.mode == kChase ? "7.5 s to catch the ball" : "5 s per target", kMissPenalty);
        ImGui::PopTextWrapPos();
    }

    // ---- save as preset: lights up once you've changed a setting ----
    ImGui::Dummy(ImVec2(0, 4));
    {
        const bool canSave = s.custom && s.userPreset == 0;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float btnW = 190.f, h = 40.f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, (h - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(canSave ? kCard : Rgb(16, 19, 26)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(canSave ? kText : kFaint));
        ImGui::PushItemWidth(width - btnW - 10.f);
        bool enterPressed = ImGui::InputTextWithHint("##presetname", canSave ? "Name your preset (e.g. Slow reversals)" : "Change a setting below, then name and save it as your own preset",
                                                     saveName, sizeof(saveName), canSave ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_ReadOnly) && canSave;
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 10);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool pressed = ImGui::InvisibleButton("##savepreset", ImVec2(btnW, h));
        bool hover = canSave && ImGui::IsItemHovered();
        ImU32 bg = canSave ? (hover ? kAccentHi : kAccent) : Rgb(16, 19, 26);
        dl->AddRectFilled(p, ImVec2(p.x + btnW, p.y + h), bg, 8.f);
        if (!canSave) dl->AddRect(p, ImVec2(p.x + btnW, p.y + h), kBorder, 8.f);
        ImVec2 ts = TextSize(bold, "Save as preset");
        DrawText(dl, bold, ImVec2(p.x + (btnW - ts.x) * 0.5f, p.y + (h - ts.y) * 0.5f), canSave ? Rgb(255, 255, 255) : kFaint, "Save as preset");
        if (canSave && (pressed || enterPressed))
        {
            SaveCurrentAsPreset(saveName);
            saveName[0] = '\0';
        }
    }

    // ---- customise drawer ----
    ImGui::Dummy(ImVec2(0, 2));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = 50.f;
        bool pressed = ImGui::InvisibleButton("##customise", ImVec2(width, h));
        bool hover = ImGui::IsItemHovered();
        if (pressed) showCustomise = !showCustomise;
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), hover ? kCardHi : kCard, 10.f);
        dl->AddRect(p, ImVec2(p.x + width, p.y + h), kBorder, 10.f);
        ImVec2 a(p.x + 22.f, p.y + h * 0.5f);
        if (showCustomise) dl->AddTriangleFilled(ImVec2(a.x - 6, a.y - 3), ImVec2(a.x + 6, a.y - 3), ImVec2(a.x, a.y + 4), kDim);
        else               dl->AddTriangleFilled(ImVec2(a.x - 3, a.y - 6), ImVec2(a.x - 3, a.y + 6), ImVec2(a.x + 4, a.y), kDim);
        DrawText(dl, bold, ImVec2(p.x + 40.f, p.y + (h - SizeOf(bold)) * 0.5f), kText, "Customise");
        const char* hint = "Fine-tune every setting. Changing anything makes the run Custom; save it above to keep records.";
        DrawText(dl, small, ImVec2(p.x + 40.f + TextSize(bold, "Customise").x + 14.f, p.y + (h - SizeOf(small)) * 0.5f), kFaint, hint);
    }
    if (showCustomise)
    {
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::Indent(8.f);
        ImGui::PushItemWidth(std::min(340.f, width * 0.45f));
        hideRoundSettings = true; // round length and time per target are in ROUND above
        PlaySettingsBody(s, false);
        hideRoundSettings = false;
        ImGui::PopItemWidth();
        ImGui::Unindent(8.f);
    }
}

// ===========================================================================
// Stats, Assists, Help
// ===========================================================================

void AirLabs::WinStats()
{
    const float width = ImGui::GetContentRegionAvail().x - 40.f; // keep clear of the close button
    {
        FontScope f(uiFonts[kFontTitle]);
        ImGui::TextUnformatted("Stats");
    }
    static const char* titles[4] = { "Overview", "Records", "Directions", "History" };
    int clicked = Segmented("##statspage", titles, nullptr, 4, uiStatsPage, std::min(width, 560.f), 40.f, uiFonts[kFontBold], nullptr);
    if (clicked >= 0) uiStatsPage = clicked;
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushItemWidth(300.f);
    switch (uiStatsPage)
    {
    case 1:  StatsRecords();    break;
    case 2:  StatsDirections(); break;
    case 3:  StatsHistory();    break;
    default: StatsOverview();   break;
    }
    ImGui::PopItemWidth();
}

void AirLabs::WinAssists()
{
    {
        FontScope f(uiFonts[kFontTitle]);
        ImGui::TextUnformatted("Assists");
    }
    ImGui::PushItemWidth(300.f);
    TabAssists();
    ImGui::PopItemWidth();
}

void AirLabs::WinHelp()
{
    {
        FontScope f(uiFonts[kFontTitle]);
        ImGui::TextUnformatted("Help");
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + std::min(760.f, ImGui::GetContentRegionAvail().x));
    TabHelp();
    ImGui::PopTextWrapPos();
}

// ===========================================================================
// Leaderboard
// ===========================================================================

void AirLabs::WinLeaderboard()
{
    const float width = ImGui::GetContentRegionAvail().x - 34.f; // keep clear of the close button
    ImFont* small = uiFonts[kFontSmall];
    ImFont* bold = uiFonts[kFontBold];
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

    // Start on the preset you're playing.
    if (lbMode < 0)
    {
        const DrillSettings s = CurrentSettings();
        lbMode = s.mode;
        lbPreset = s.custom ? 0 : s.preset;
        lbRange = s.range;
        lbNoRoll = noAirRoll && *noAirRoll;
    }
    lbMode = std::clamp(lbMode, 0, kModeCount - 1);
    lbPreset = std::clamp(lbPreset, 0, PresetCount(lbMode) - 1);
    const bool usesRange = PresetUsesRange(lbMode, lbPreset);
    const bool rollOk = !(lbMode == kFlick && kFlickPresets[lbPreset].aim == 1); // wheels aim needs roll
    const std::string key = BoardKey(lbMode, lbPreset, lbRange, lbNoRoll && rollOk);
    FetchBoard(key);
    if (lbName.empty() && wall - lbNameAsked > 5.0)
    {
        lbNameAsked = wall;
        gameWrapper->Execute([this](GameWrapper* gw) {
            std::string n = Utf8FromWide(gw->GetPlayerName().ToWideString());
            std::lock_guard<std::mutex> lock(lbMutex);
            lbName = n;
        });
    }

    // Copy what the page needs out of the shared state.
    LbBoard board;
    std::string status, pid, name, watchName, playErr;
    bool statusGood = true, uploading = false;
    int loading = 0, pending = 0;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        board = lbBoards[key];
        status = wall - lbStatusWall < 600.0 ? lbStatus : std::string();
        statusGood = lbStatusGood;
        uploading = lbUploading;
        pending = lbPendingCount;
        pid = lbPid;
        name = lbName;
        loading = loadingRun;
        watchName = playName;
        playErr = playError;
    }

    {
        FontScope f(uiFonts[kFontTitle]);
        ImGui::TextUnformatted("Leaderboard");
    }
    {
        FontScope f(small);
        ImGui::TextDisabled("Everyone's best one-minute run on every preset. Every run is checked by replaying it, and you can watch any of them.");
    }
    ImGui::Dummy(ImVec2(0, 2));

    // ---- watching now ----
    if (state == RunState::Replay)
    {
        Card c;
        c.Begin(width, 16.f);
        Dot(kAccentHi);
        {
            FontScope f(bold);
            ImGui::TextUnformatted(("Watching " + watchName + "'s run: " + FmtInt(play.score)).c_str());
        }
        ImGui::SameLine(0, 16);
        if (Chip("##stopwatch", "Stop watching", false, small, 28.f)) cvarManager->executeCommand("airlabs_stop");
        c.End(kAccentBg, kAccent);
        ImGui::Dummy(ImVec2(0, 2));
    }

    // ---- posting: opt in, and how the last post went ----
    const bool posting = lbPost && *lbPost;
    if (!posting)
    {
        Card c;
        c.Begin(width, 20.f);
        {
            FontScope f(uiFonts[kFontHeading]);
            ImGui::TextUnformatted("Put your runs on the leaderboard");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
        ImGui::TextWrapped("Your best run on each preset goes on this public leaderboard under your Rocket League name%s, with its replay so anyone can watch it. "
                           "Only preset runs count, and the server checks every run by playing it back before it's accepted.",
                           name.empty() ? "" : (" (" + name + ")").c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 2));
        if (Chip("##lbjoin", "Post my runs", true, bold, 36.f)) SetCvarI("airlabs_lb_post", 1);
        c.End();
    }
    else
    {
        Dot(kGood);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGood), "%s", name.empty() ? "Posting your runs" : ("Posting your runs as " + name).c_str());
        ImGui::SameLine(0, 14);
        if (LinkButton("##lbstop", "Stop posting", small)) SetCvarI("airlabs_lb_post", 0);
    }
    if (!status.empty() || uploading)
    {
        FontScope f(small);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(statusGood ? kDim : kWarn), "%s", uploading ? "Posting your run..." : status.c_str());
    }
    if (pending > 0)
    {
        // Runs that couldn't be posted (no internet): they retry by themselves every minute.
        FontScope f(small);
        Dot(kWarn);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "%s", (pending == 1 ? std::string("1 run is waiting to post")
                                                                                       : std::to_string(pending) + " runs are waiting to post").c_str());
        ImGui::SameLine(0, 10);
        if (posting)
        {
            ImGui::TextDisabled("(tries again every minute)");
            ImGui::SameLine(0, 10);
            if (LinkButton("##lbretry", uploading ? "Posting..." : "Retry now", small, kAccentHi) && !uploading)
                gameWrapper->Execute([this](GameWrapper*) { RetryPending(); });
        }
        else ImGui::TextDisabled("Turn on Post my runs to send %s.", pending == 1 ? "it" : "them");
    }
    if (!playErr.empty())
    {
        FontScope f(small);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "Replay: %s", playErr.c_str());
    }

    // ---- which board ----
    SectionLabel(small, "BOARD");
    {
        static const char* modes[kModeCount] = { "Flick", "Track", "Hybrid", "Chase" };
        int m = Segmented("##lbmode", modes, nullptr, kModeCount, lbMode, std::min(width, 520.f), 40.f, bold, nullptr);
        if (m >= 0 && m != lbMode) { lbMode = m; lbPreset = m == kFlick ? 4 : 1; }
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::PushID("lbpresets");
        for (int p = 0; p < PresetCount(lbMode); ++p)
        {
            const std::string label = PresetLabel(lbMode, p);
            if (p) SameLineIfRoom(ChipWidth(bold, label.c_str()));
            ImGui::PushID(p);
            if (Chip("##p", label.c_str(), p == lbPreset, bold)) lbPreset = p;
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 2));
        if (usesRange)
        {
            static const char* ranges[kRangeCount] = { "Short", "Medium", "Long" };
            int r = Segmented("##lbrange", ranges, nullptr, kRangeCount, lbRange, 330.f, 36.f, bold, nullptr);
            if (r >= 0) lbRange = r;
            ImGui::SameLine(0, 14);
        }
        if (rollOk)
        {
            if (Chip("##lbnoroll", lbNoRoll ? "No air roll: ON" : "No air roll: OFF", lbNoRoll, bold, 36.f)) lbNoRoll = !lbNoRoll;
        }
        else
        {
            FontScope f(small);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Flip Reset has one board: the ball is always 350 uu away, and wheels aim needs air roll.");
        }
    }

    // ---- the board ----
    ImGui::Dummy(ImVec2(0, 6));
    Card c;
    c.Begin(width, 0.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float inner = width;
    {
        // header: title, players, refresh
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string title = board.title.empty() ? std::string(ModeName(lbMode)) + " " + PresetLabel(lbMode, lbPreset) : board.title;
        DrawText(dl, uiFonts[kFontHeading], ImVec2(p.x + 18.f, p.y + 14.f), kText, title.c_str());
        std::string count = board.loaded && board.total > 0 ? (board.total == 1 ? "1 player" : FmtInt(board.total) + " players") : std::string();
        const float tw = TextSize(uiFonts[kFontHeading], title.c_str()).x;
        DrawText(dl, small, ImVec2(p.x + 18.f + tw + 12.f, p.y + 14.f + SizeOf(uiFonts[kFontHeading]) - SizeOf(small) - 2.f), kFaint, count.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + inner - 90.f, p.y + 14.f));
        if (LinkButton("##lbrefresh", board.loading ? "Loading..." : "Refresh", small) && !board.loading) FetchBoard(key, true);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 52.f));
        dl->AddLine(ImVec2(p.x, p.y + 52.f), ImVec2(p.x + inner, p.y + 52.f), kBorder);
    }
    const float rowH = 42.f;
    auto drawRow = [&](const LbRow& r, bool mine, bool alt) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(r.run);
        ImGui::Dummy(ImVec2(inner, rowH));
        if (mine) dl->AddRectFilled(p, ImVec2(p.x + inner, p.y + rowH), kAccentBg);
        else if (alt) dl->AddRectFilled(p, ImVec2(p.x + inner, p.y + rowH), Rgb(255, 255, 255, 6));
        const float cy = p.y + rowH * 0.5f;
        const std::string pos = "#" + std::to_string(r.pos);
        DrawText(dl, bold, ImVec2(p.x + 18.f, cy - SizeOf(bold) * 0.5f), r.pos == 1 ? kGold : r.pos <= 3 ? kText : kDim, pos.c_str());
        RankState rs;
        rs.value = TierIndex(static_cast<float>(r.score), lbMode, lbPreset, lbRange) / 3.f + 0.001f;
        rs.runs = kPlacementRuns;
        const float bw = RankBadge(rs, 22.f, small, dl, ImVec2(p.x + 76.f, cy - 11.f));
        const float nx = p.x + 76.f + std::max(bw, 30.f) + 14.f;
        DrawText(dl, bold, ImVec2(nx, cy - SizeOf(bold) * 0.5f), kText, r.name.c_str());
        const float nw = TextSize(bold, r.name.c_str()).x;
        DrawText(dl, small, ImVec2(nx + nw + 10.f, cy - SizeOf(small) * 0.5f + 1.f), kFaint, (r.platform + (mine ? "  \xC2\xB7  you" : "")).c_str());
        const std::string sc = FmtInt(r.score);
        const float scoreRight = p.x + inner - 230.f;
        DrawText(dl, bold, ImVec2(scoreRight - TextSize(bold, sc.c_str()).x, cy - SizeOf(bold) * 0.5f), kText, sc.c_str());
        DrawText(dl, small, ImVec2(scoreRight + 24.f, cy - SizeOf(small) * 0.5f), kFaint, r.date.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + inner - 104.f, cy - 15.f));
        const bool thisLoading = loading == r.run;
        if (Chip("##watch", thisLoading ? "Loading" : "Watch", false, small, 30.f) && loading == 0)
        {
            WatchReplay(r.run);
            closeRequested = true; // close the window: the replay plays in the game
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Watch this run in the game (in Freeplay; it loads the map if you're not there)");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
        ImGui::PopID();
    };
    auto message = [&](const char* text, ImU32 col) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(inner, 70.f));
        const ImVec2 ts = TextSize(uiFonts[kFontBody], text, inner - 40.f);
        DrawText(dl, uiFonts[kFontBody], ImVec2(p.x + (inner - ts.x) * 0.5f, p.y + (70.f - ts.y) * 0.5f), col, text, inner - 40.f);
    };
    if (!board.error.empty() && board.rows.empty()) message(board.error.c_str(), kWarn);
    else if (!board.loaded) message("Loading...", kDim);
    else if (board.rows.empty()) message("No runs on this board yet. Play it with Post my runs on, and the top spot is yours.", kDim);
    else
    {
        bool meShown = false;
        for (std::size_t i = 0; i < board.rows.size(); ++i)
        {
            const bool mine = !pid.empty() && board.rows[i].pid == pid;
            meShown |= mine;
            drawRow(board.rows[i], mine, i % 2 == 1);
        }
        if (!meShown && board.mePos > 0)
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + inner, p.y), kBorder);
            LbRow me;
            me.pos = board.mePos; me.score = board.meScore; me.run = board.meRun; me.pid = pid;
            me.name = name.empty() ? std::string("You") : name;
            drawRow(me, true, false);
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    c.End();
    {
        FontScope f(small);
        ImGui::TextDisabled("Also on the web: %s", LbUrl("").c_str());
    }
}
