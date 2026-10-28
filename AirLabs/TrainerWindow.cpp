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
    constexpr const char* kWinVersion = "2.8.2";

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
        case 5: // two chevrons: speed
            dl->AddLine(ImVec2(c.x - 8, c.y - 7), ImVec2(c.x - 1, c.y), icol, 2.2f);
            dl->AddLine(ImVec2(c.x - 1, c.y), ImVec2(c.x - 8, c.y + 7), icol, 2.2f);
            dl->AddLine(ImVec2(c.x, c.y - 7), ImVec2(c.x + 7, c.y), icol, 2.2f);
            dl->AddLine(ImVec2(c.x + 7, c.y), ImVec2(c.x, c.y + 7), icol, 2.2f);
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

    // Big selectable tile: title and a one-line description. badge = a small green label on the
    // right of the title (badgeShort when the full one doesn't fit).
    bool Tile(const char* id, const char* title, const char* desc, bool selected, ImVec2 size, ImFont* titleFont, ImFont* descFont,
              const char* badge = nullptr, const char* badgeShort = nullptr)
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
        const float titleRight = p.x + 16.f + TextSize(titleFont, title).x;
        for (const char* b : { badge, badgeShort })
        {
            if (!b) continue;
            const ImVec2 bs = TextSize(descFont, b);
            const float bw = bs.x + 14.f, bh = bs.y + 4.f;
            const ImVec2 b0(q.x - 12.f - bw, p.y + 14.f + (SizeOf(titleFont) - bh) * 0.5f);
            if (b0.x < titleRight + 8.f) continue; // doesn't fit: try the short one
            dl->AddRectFilled(b0, ImVec2(b0.x + bw, b0.y + bh), Rgb(52, 211, 153, 40), bh * 0.5f);
            DrawText(dl, descFont, ImVec2(b0.x + 7.f, b0.y + 2.f), kGood, b);
            break;
        }
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
        "The ball dashes off and stops. Catch it each time.",
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
    case 5:  WinLab();     break;
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

float AirLabs::RealRankBadge(int rank, float h, ImFont* font, ImDrawList* dl, ImVec2 p)
{
    const float icon = h + 8.f, iconY = p.y + (h - icon) * 0.5f;
    EnsureRankIcons();
    if (rank < 0) // not known: a quiet dash
    {
        if (dl) DrawText(dl, font, ImVec2(p.x + icon * 0.5f - 4.f, p.y + (h - SizeOf(font)) * 0.5f), kFaint, "-");
        return icon;
    }
    const int tier = rank / 4, div = rank % 4;
    std::shared_ptr<ImageWrapper> img = tier == 0 ? unrankedIcon : rankIcon[std::clamp(tier - 1, 0, kTierCount - 1)];
    if (img && img->IsLoadedForImGui())
    {
        if (dl)
            if (void* tex = img->GetImGuiTex()) dl->AddImage(tex, ImVec2(p.x, iconY), ImVec2(p.x + icon, iconY + icon));
        (void)div;
        return icon;
    }
    if (img) img->GetImGuiTex(); // starts loading; drawn from the next frame
    const char* txt = tier == 0 ? "UNR" : TierShort(tier - 1);
    const float w = std::max(icon, TextSize(font, txt).x + 10.f);
    if (dl)
    {
        const ImU32 col = tier == 0 ? Rgb(48, 55, 70) : RankColor(TierRank(tier - 1));
        const ImVec2 ts = TextSize(font, txt);
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), col, 5.f);
        DrawText(dl, font, ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (h - SizeOf(font)) * 0.5f),
                 tier == 0 ? kDim : (TierRank(tier - 1) >= 7 || TierRank(tier - 1) <= 3 ? Rgb(20, 22, 28) : Rgb(255, 255, 255)), txt);
    }
    return w;
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
    static const int order[6] = { 0, 5, 1, 4, 2, 3 };
    static const char* labels[6] = { "Play", "Stats", "Assists", "Help", "Leaderboard", "Boost Lab" };
    for (int k = 0; k < 6; ++k)
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

    // Updates, just above the status box: checking, no update, couldn't check, or the Update button.
    std::string latest;
    bool updating;
    UpdateCheck check;
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        latest = lbLatest;
        updating = lbUpdating;
        check = lbUpdateCheck;
    }
    if (check == UpdateCheck::Available && !latest.empty())
    {
        const float bh = 62.f;
        const ImVec2 b0(top.x, top.y - bh - 10.f), b1(p.x + w - 12.f, top.y - 10.f);
        ImGui::SetCursorScreenPos(b0);
        const bool pressed = ImGui::InvisibleButton("##otupdate", ImVec2(b1.x - b0.x, bh));
        const bool hover = ImGui::IsItemHovered();
        dl->AddRectFilled(b0, b1, updating ? kCardHi : (hover ? kAccentHi : kAccent), 10.f);
        // a down arrow into a tray
        const ImVec2 ic(b0.x + 24.f, b0.y + bh * 0.5f);
        const ImU32 white = Rgb(255, 255, 255);
        dl->AddLine(ImVec2(ic.x, ic.y - 9.f), ImVec2(ic.x, ic.y + 3.f), white, 2.f);
        dl->AddTriangleFilled(ImVec2(ic.x - 5.f, ic.y), ImVec2(ic.x + 5.f, ic.y), ImVec2(ic.x, ic.y + 6.f), white);
        dl->AddLine(ImVec2(ic.x - 8.f, ic.y + 9.f), ImVec2(ic.x + 8.f, ic.y + 9.f), white, 2.f);
        DrawText(dl, uiFonts[kFontBold], ImVec2(b0.x + 44.f, b0.y + 11.f), white, updating ? "Updating..." : "Update");
        DrawText(dl, uiFonts[kFontSmall], ImVec2(b0.x + 44.f, b0.y + 33.f), Rgb(220, 232, 255),
                 (updating ? "BakkesMod is installing " + latest : "AirLabs " + latest + " is out").c_str());
        if (hover && !updating) ImGui::SetTooltip("Install AirLabs %s from bakkesplugins. BakkesMod reloads it when it's done.", latest.c_str());
        if (pressed && !updating) StartUpdate();
    }
    else
    {
        // One line of status, and a link to check (again).
        ImFont* sm = uiFonts[kFontSmall];
        const float lineH = SizeOf(sm);
        const ImVec2 r0(top.x + 4.f, top.y - 2.f * lineH - 22.f);
        const char* what = check == UpdateCheck::Checking ? "Checking for updates..."
                         : check == UpdateCheck::UpToDate ? "No update available"
                         : check == UpdateCheck::Failed   ? "Couldn't check for updates"
                                                          : "";
        const ImU32 dot = check == UpdateCheck::UpToDate ? kGood : check == UpdateCheck::Failed ? kWarn : kFaint;
        if (*what)
        {
            dl->AddCircleFilled(ImVec2(r0.x + 5.f, r0.y + lineH * 0.5f + 1.f), 3.5f, dot, 12);
            DrawText(dl, sm, ImVec2(r0.x + 16.f, r0.y), check == UpdateCheck::Failed ? kWarn : kDim, what);
        }
        if (check != UpdateCheck::Checking)
        {
            ImGui::SetCursorScreenPos(ImVec2(r0.x + 14.f, r0.y + lineH + 4.f));
            const char* link = check == UpdateCheck::Failed ? "Try again" : check == UpdateCheck::UpToDate ? "Check again" : "Check for updates";
            if (LinkButton("##otcheckupd", link, sm, kAccentHi)) CheckForUpdate();
        }
    }

    ImVec2 bot(p.x + w - 12.f, p.y + height - 12.f);
    dl->AddRectFilled(top, bot, kCard, 10.f);
    dl->AddRect(top, bot, kBorder, 10.f);
    ImU32 dotCol = kFaint;
    std::string status = "Not running";
    if (state == RunState::Ready)   { dotCol = kWarn; status = showResults ? "Run finished" : "Point at the ball"; }
    if (state == RunState::Running) { dotCol = kGood; status = runEnd < 1e17 ? "Running  " + std::string(Fmt("%.0f s left", std::max(0.0, runEnd - gameTime))) : "Running"; }
    if (state == RunState::Replay)  { dotCol = kAccentHi; status = "Watching a replay"; }
    if (labOn)                      { dotCol = kGood; status = "Boost Lab"; }
    dl->AddCircleFilled(ImVec2(top.x + 18.f, top.y + 22.f), 5.f, dotCol, 16);
    DrawText(dl, uiFonts[kFontBold], ImVec2(top.x + 30.f, top.y + 12.f), kText, status.c_str());
    DrawText(dl, uiFonts[kFontSmall], ImVec2(top.x + 14.f, top.y + 44.f), kDim, "Open or close this window:");
    DrawText(dl, uiFonts[kFontBold], ImVec2(top.x + 14.f, top.y + 62.f), kText, "bind O AL_menu");
    if (state == RunState::Idle && !labOn) // the running status is wider, so the version only shows when idle
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
            if ((s.mode == kTrack || s.mode == kChase) && s.runLength > 0.f)
                pills.push_back("Max " + FmtInt(std::llround(1000.0 * s.runLength))); // 1000 points a second on the ball
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
            if (best >= 0) { FontScope f(bold); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGold), "Best %s", FmtInt(best).c_str()); }
            else ImGui::TextDisabled("No best yet");
            // Where you stand on this preset's leaderboard (refreshed every minute while the page is open).
            const std::string board = BoardKey(s.mode, s.preset, s.range, s.noRoll);
            FetchBoard(board);
            bool loaded;
            int pos, total, top;
            {
                std::lock_guard<std::mutex> lock(lbMutex);
                const LbBoard& b = lbBoards[board];
                loaded = b.loaded; pos = b.mePos; total = b.total; top = b.rows.empty() ? 0 : b.rows[0].score;
            }
            ImGui::AlignTextToFramePadding();
            if (loaded && pos > 0)
            {
                FontScope f(bold);
                ImGui::TextUnformatted("Leaderboard");
                ImGui::SameLine(0, 8);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pos == 1 ? kGold : kText), "#%d of %d", pos, total);
                if (pos > 1 && top > 0)
                {
                    ImGui::SameLine(0, 10);
                    FontScope fs(small);
                    ImGui::TextDisabled("top score %s", FmtInt(top).c_str());
                }
            }
            else if (loaded && total > 0) ImGui::TextDisabled("Leaderboard: %d %s, top score %s", total, total == 1 ? "player" : "players", FmtInt(top).c_str());
            else if (loaded) ImGui::TextDisabled("Nobody's on this leaderboard yet: the top spot is open");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        float leftH = ImGui::GetItemRectSize().y;

        // The button, vertically centred on the right.
        ImVec2 btnPos(cardTop.x + card.Inner() - btnW, cardTop.y + std::max(0.f, (leftH - btnH) * 0.5f) - (state == RunState::Running && s.mode != kTrack && s.mode != kChase ? 12.f : 0.f));
        ImGui::SetCursorScreenPos(btnPos);
        const bool running = state == RunState::Ready || state == RunState::Running || state == RunState::Replay || labOn;
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

    // ---- a training tip (Tips.h): a new one each time the window opens ----
    if (showTips && *showTips)
    {
        const Tip& tip = kTips[((tipIndex % kTipCount) + kTipCount) % kTipCount];
        ImGui::Dummy(ImVec2(0, 4));
        Card c;
        c.Begin(width, 16.f);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        {
            FontScope f(small);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kAccentHi), "TIP");
        }
        {
            const float nextW = TextSize(small, "Next tip").x + 4.f, hideW = TextSize(small, "Hide tips").x + 4.f, gap = 18.f;
            ImGui::SetCursorScreenPos(ImVec2(top.x + c.Inner() - nextW - gap - hideW, top.y - 2.f));
            if (LinkButton("##tipnext", "Next tip", small, kAccentHi)) ++tipIndex;
            ImGui::SameLine(0, gap);
            if (LinkButton("##tiphide", "Hide tips", small)) SetCvarI("airlabs_tips", 0);
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + SizeOf(small) + 6.f));
        {
            FontScope f(bold);
            ImGui::TextUnformatted(tip.title);
        }
        {
            FontScope f(small);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
            ImGui::TextWrapped("%s", tip.body);
            ImGui::PopStyleColor();
        }
        c.End(kCard, kBorder);
    }


    // ---- mode ----
    SectionLabel(small, "MODE");
    {
        const float gap = 12.f, tileW = (width - gap * static_cast<float>(kModeCount - 1)) / static_cast<float>(kModeCount);
        for (int m = 0; m < kModeCount; ++m)
        {
            if (m) ImGui::SameLine(0, gap);
            ImGui::PushID(m);
            if (Tile("##mode", ModeName(m), kModeTileDesc[m], s.mode == m, ImVec2(tileW, 82.f), uiFonts[kFontHeading], small,
                     m == kTrack ? "Best" : nullptr)) SetCvarI("airlabs_mode", m);
            ImGui::PopID();
        }
        // No air roll (a variant of every mode, with its own records) and the laser (an assist,
        // here so it's easy to find), with their explanations lined up.
        ImGui::Dummy(ImVec2(0, 2));
        const float chipsX = ImGui::GetCursorPosX();
        const float textX = chipsX + std::max(ChipWidth(bold, "No air roll: OFF"), ChipWidth(bold, "Laser: OFF")) + 14.f;
        const bool noRollOn = noAirRoll && *noAirRoll;
        if (Chip("##noroll", noRollOn ? "No air roll: ON" : "No air roll: OFF", noRollOn, bold))
            SetCvarI("airlabs_noroll", noRollOn ? 0 : 1);
        ImGui::SameLine(textX);
        {
            FontScope f(small);
            ImGui::AlignTextToFramePadding();
            if (noRollOn && s.aim == 1)
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "Not used with wheels aim: aiming with your wheels needs air roll.");
            else
                ImGui::TextDisabled("Air roll does nothing during runs: turn with pitch and yaw only. Scores go to separate No air roll records.");
        }
        ImGui::SetCursorPosX(chipsX);
        const bool laserOn = laserEnabled && *laserEnabled;
        if (Chip("##laser", laserOn ? "Laser: ON" : "Laser: OFF", laserOn, bold)) SetCvarI("airlabs_laser", laserOn ? 0 : 1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Red off the ball, yellow close, green on it. More laser settings in Assists.");
        ImGui::SameLine(textX);
        {
            FontScope f(small);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("A line out of your car's nose that shows exactly where you're aiming (also in Assists). Best played with it off.");
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
            if (!first) SameLineIfRoom(ChipWidth(bold, PresetName(s.mode, p)));
            first = false;
            ImGui::PushID(p);
            if (Chip("##c", PresetName(s.mode, p), !s.custom && s.preset == p, bold, 34.f)) SetCvarI(PresetCvar(s.mode), p);
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
            else MakeCustom();
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
                if (Chip("##d", PresetName(s.mode, p), s.preset == p, small, 28.f)) SetCvarI(PresetCvar(s.mode), p);
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
                ImGui::TextDisabled("Your saved preset. Its runs count toward its own personal bests and progress (no leaderboard). It's locked: change a setting and it becomes Custom, which you can save as a new preset.");
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

    // ---- every setting, on the page: round first, then where the ball spawns, then the mode's own ----
    WinSettings(s, width);

    // ---- save as preset: lights up once you've changed a setting ----
    ImGui::Dummy(ImVec2(0, 6));
    {
        const bool canSave = s.custom && s.userPreset == 0;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float btnW = 190.f, h = 40.f;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, (h - ImGui::GetFontSize()) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(canSave ? kCard : Rgb(16, 19, 26)));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(canSave ? kText : kFaint));
        ImGui::PushItemWidth(width - btnW - 10.f);
        bool enterPressed = ImGui::InputTextWithHint("##presetname", canSave ? "Name your preset (e.g. Slow reversals)" : "Change a setting above, then name and save it as your own preset",
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
        FontScope f(small);
        ImGui::TextDisabled("Presets have fixed settings so scores compare. Changing any setting makes the run Custom; save it here to keep its own records.");
    }
}

// ===========================================================================
// Settings (on the Play page)
// ===========================================================================

namespace
{
    // One setting: its name and a line on what it does on the left, the control on the right.
    // Begin returns the control's width and leaves the cursor where the control goes.
    struct SettingRow
    {
        ImVec2 start;
        float width = 0.f, textH = 0.f;
        float Begin(const char* title, const char* desc, float w, ImFont* titleFont, ImFont* descFont)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            start = ImGui::GetCursorScreenPos();
            width = w;
            const float textW = w * 0.5f, ctrlW = w * 0.44f;
            DrawText(dl, titleFont, start, kText, title);
            const float th = SizeOf(titleFont) + 3.f;
            float dh = 0.f;
            if (desc && *desc)
            {
                DrawText(dl, descFont, ImVec2(start.x, start.y + th), kDim, desc, textW);
                dh = TextSize(descFont, desc, textW).y;
            }
            textH = th + dh;
            ImGui::SetCursorScreenPos(ImVec2(start.x + w - ctrlW, start.y));
            ImGui::BeginGroup();
            return ctrlW;
        }
        void End(bool divider = true)
        {
            ImGui::EndGroup();
            const float bottom = std::max(start.y + textH, ImGui::GetItemRectMax().y);
            const float y = bottom + (divider ? 13.f : 2.f);
            if (divider) ImGui::GetWindowDrawList()->AddLine(ImVec2(start.x, y), ImVec2(start.x + width, y), Rgb(37, 44, 58, 150));
            ImGui::SetCursorScreenPos(ImVec2(start.x, y + (divider ? 13.f : 0.f)));
            ImGui::Dummy(ImVec2(0, 0));
        }
    };

    // An on/off switch.
    bool OnOff(const char* id, bool on, ImFont* font)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = 50.f, h = 28.f;
        const bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
        const bool hover = ImGui::IsItemHovered();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), on ? (hover ? kAccentHi : kAccent) : (hover ? kCardHi : Rgb(31, 38, 50)), h * 0.5f);
        if (!on) dl->AddRect(p, ImVec2(p.x + w, p.y + h), kBorder, h * 0.5f);
        const float r = h * 0.5f - 4.f;
        dl->AddCircleFilled(ImVec2(on ? p.x + w - h * 0.5f : p.x + h * 0.5f, p.y + h * 0.5f), r, Rgb(255, 255, 255), 24);
        ImGui::SameLine(0, 10);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y + (h - SizeOf(font)) * 0.5f));
        FontScope f(font);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(on ? kText : kDim), on ? "On" : "Off");
        return pressed;
    }

    // A slider: a thin track filled up to the value, a round knob, and the value in a pill on the
    // right. Click or drag anywhere on the track.
    bool TrackSlider(const char* id, float* v, float lo, float hi, const std::string& label, float w, ImFont* font)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pillW = std::max(78.f, TextSize(font, label.c_str()).x + 22.f), gap = 12.f, h = 30.f;
        const float trackW = std::max(40.f, w - pillW - gap);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(trackW, h));
        const bool active = ImGui::IsItemActive(), hover = ImGui::IsItemHovered();
        bool changed = false;
        if (active)
        {
            const float t = std::clamp((ImGui::GetIO().MousePos.x - p.x) / trackW, 0.f, 1.f);
            const float nv = lo + t * (hi - lo);
            if (nv != *v) { *v = nv; changed = true; }
        }
        const float t = hi > lo ? std::clamp((*v - lo) / (hi - lo), 0.f, 1.f) : 0.f;
        const float cy = p.y + h * 0.5f, x = p.x + t * trackW;
        dl->AddRectFilled(ImVec2(p.x, cy - 3.f), ImVec2(p.x + trackW, cy + 3.f), Rgb(44, 52, 68), 3.f);
        dl->AddRectFilled(ImVec2(p.x, cy - 3.f), ImVec2(x, cy + 3.f), kAccent, 3.f);
        dl->AddCircleFilled(ImVec2(x, cy), active ? 10.f : 9.f, (hover || active) ? Rgb(255, 255, 255) : Rgb(230, 234, 241), 24);
        dl->AddCircle(ImVec2(x, cy), active ? 10.f : 9.f, kAccent, 24, 2.f);
        const ImVec2 a(p.x + trackW + gap, p.y + 1.f), b(a.x + pillW, p.y + h - 1.f);
        dl->AddRectFilled(a, b, Rgb(31, 38, 50), 7.f);
        const ImVec2 ts = TextSize(font, label.c_str());
        DrawText(dl, font, ImVec2(a.x + (pillW - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f), kText, label.c_str());
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h));
        return changed;
    }
    std::string Label(const char* fmt, float v) { char b[64]; std::snprintf(b, sizeof b, fmt, v); return b; }
    bool SliderF(const char* id, float* v, float lo, float hi, const char* fmt, float w, ImFont* font)
    {
        float x = *v;
        const bool changed = TrackSlider(id, &x, lo, hi, Label(fmt, x), w, font);
        if (changed) *v = x;
        return changed;
    }
    bool SliderI(const char* id, int* v, int lo, int hi, const char* fmt, float w, ImFont* font)
    {
        float x = static_cast<float>(*v);
        char b[64];
        std::snprintf(b, sizeof b, fmt, *v);
        const bool changed = TrackSlider(id, &x, static_cast<float>(lo), static_cast<float>(hi), b, w, font);
        const int nv = static_cast<int>(std::lround(x));
        if (changed && nv != *v) { *v = nv; return true; }
        return false;
    }
    // Two knobs on one track (a low and a high end): drag whichever is nearer.
    bool RangeSlider(const char* id, float* lo, float* hi, float min, float max, const std::string& label, float w, ImFont* font, int& dragging)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float pillW = std::max(78.f, TextSize(font, label.c_str()).x + 22.f), gap = 12.f, h = 30.f;
        const float trackW = std::max(40.f, w - pillW - gap);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(trackW, h));
        const bool active = ImGui::IsItemActive(), hover = ImGui::IsItemHovered();
        auto tOf = [&](float v) { return std::clamp((v - min) / (max - min), 0.f, 1.f); };
        bool changed = false;
        if (active)
        {
            const float t = std::clamp((ImGui::GetIO().MousePos.x - p.x) / trackW, 0.f, 1.f);
            const float nv = min + t * (max - min);
            if (ImGui::IsItemActivated()) dragging = std::fabs(t - tOf(*lo)) <= std::fabs(t - tOf(*hi)) ? 0 : 1;
            float& target = dragging == 0 ? *lo : *hi;
            const float bound = dragging == 0 ? std::min(nv, *hi) : std::max(nv, *lo);
            if (bound != target) { target = bound; changed = true; }
        }
        const float cy = p.y + h * 0.5f, x0 = p.x + tOf(*lo) * trackW, x1 = p.x + tOf(*hi) * trackW;
        dl->AddRectFilled(ImVec2(p.x, cy - 3.f), ImVec2(p.x + trackW, cy + 3.f), Rgb(44, 52, 68), 3.f);
        dl->AddRectFilled(ImVec2(x0, cy - 3.f), ImVec2(x1, cy + 3.f), kAccent, 3.f);
        for (float x : { x0, x1 })
        {
            dl->AddCircleFilled(ImVec2(x, cy), 9.f, (hover || active) ? Rgb(255, 255, 255) : Rgb(230, 234, 241), 24);
            dl->AddCircle(ImVec2(x, cy), 9.f, kAccent, 24, 2.f);
        }
        const ImVec2 a(p.x + trackW + gap, p.y + 1.f), b(a.x + pillW, p.y + h - 1.f);
        dl->AddRectFilled(a, b, Rgb(31, 38, 50), 7.f);
        const ImVec2 ts = TextSize(font, label.c_str());
        DrawText(dl, font, ImVec2(a.x + (pillW - ts.x) * 0.5f, a.y + (b.y - a.y - ts.y) * 0.5f), kText, label.c_str());
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(w, h));
        return changed;
    }
}

void AirLabs::WinSettings(const DrillSettings& s, float width)
{
    ImFont* small = uiFonts[kFontSmall];
    ImFont* bold = uiFonts[kFontBold];
    // Every change goes through here: the run becomes Custom (presets are fixed so scores compare).
    auto setF = [this](const char* cvar, float v) { MakeCustom(); SetCvarF(cvar, v); };
    auto setI = [this](const char* cvar, int v) { MakeCustom(); SetCvarI(cvar, v); };
    SettingRow row;

    auto sliderRow = [&](const char* id, const char* title, const char* desc, const char* cvar, float value, float lo, float hi, const char* fmt, bool divider = true) {
        const float w = row.Begin(title, desc, width - 36.f, bold, small);
        float v = value;
        if (SliderF(id, &v, lo, hi, fmt, w, bold)) setF(cvar, v);
        row.End(divider);
    };
    auto msRow = [&](const char* id, const char* title, const char* desc, const char* cvar, float seconds, int lo, int hi, bool divider = true) {
        const float w = row.Begin(title, desc, width - 36.f, bold, small);
        int v = static_cast<int>(std::lround(seconds * 1000.f));
        if (SliderI(id, &v, lo, hi, "%d ms", w, bold)) setI(cvar, v);
        row.End(divider);
    };
    auto toggleRow = [&](const char* id, const char* title, const char* desc, const char* cvar, bool on, bool divider = true) {
        row.Begin(title, desc, width - 36.f, bold, small);
        if (OnOff(id, on, small)) setI(cvar, on ? 0 : 1);
        row.End(divider);
    };
    auto choiceRow = [&](const char* id, const char* title, const char* desc, const char* cvar, int value, const char* const* options, int count, bool divider = true) {
        const float w = row.Begin(title, desc, width - 36.f, bold, small);
        const int c = Segmented(id, options, nullptr, count, value, w, 36.f, bold, nullptr);
        if (c >= 0 && c != value) setI(cvar, c);
        row.End(divider);
    };

    // ---- ROUND: how long the round is, and how long each ball waits ----
    SectionLabel(small, "ROUND");
    {
        Card c;
        c.Begin(width, 18.f);
        const bool flicks = s.mode == kFlick || s.mode == kHybrid;
        sliderRow("##runlen", "Round length", "How long a round lasts. Presets: 60 s. 0 = endless (until you stop).",
                  "airlabs_run_length", s.runLength, 0.f, 600.f, s.runLength > 0.f ? "%.0f s" : "Endless", s.mode != kTrack);
        if (s.mode == kChase)
            sliderRow("##timeout", "Time to catch the ball", "How long a stopped ball waits for you before it counts as a miss and dashes off. Presets: 7.5 s. 0 = no limit.",
                      "airlabs_timeout", s.timeout, 0.f, 30.f, s.timeout > 0.f ? "%.1f s" : "No limit", false);
        else if (flicks)
        {
            sliderRow("##timeout", "Time per target", "How long you get to reach each ball. Run out and it's a miss (-500 points). Presets: 5 s. 0 = no limit.",
                      "airlabs_timeout", s.timeout, 0.f, 30.f, s.timeout > 0.f ? "%.1f s" : "No limit");
            msRow("##delay", "Delay between targets", "The pause before the next ball spawns. Presets: 20 ms.",
                  "airlabs_next_delay", s.delaySec, 0, 3000, false);
        }
        c.End();
    }

    // ---- BALL SPAWN: height, direction from your car, distance ----
    SectionLabel(small, "BALL SPAWN");
    {
        Card c;
        c.Begin(width, 18.f);
        // Height
        {
            const float w = row.Begin("Ball spawn height", "Where the ball can be, measured from the horizon (the ground, not your car): above you, level, below, or anywhere.",
                                      width - 36.f, bold, small);
            struct HeightChoice { const char* label; Band band; };
            static const HeightChoice heights[] = { { "Any", kBandAny }, { "Above", kBandAbove }, { "Level", kBandLevel }, { "Below", kBandBelow } };
            static const char* labels[] = { "Any", "Above", "Level", "Below" };
            int sel = -1;
            for (int i = 0; i < 4; ++i)
                if (std::fabs(s.band.minDeg - heights[i].band.minDeg) < 0.5f && std::fabs(s.band.maxDeg - heights[i].band.maxDeg) < 0.5f) sel = i;
            const int clicked = Segmented("##height", labels, nullptr, 4, sel, w, 36.f, bold, nullptr);
            if (clicked >= 0 && clicked != sel)
            {
                MakeCustom();
                SetCvarF("airlabs_height_min", heights[clicked].band.minDeg);
                SetCvarF("airlabs_height_max", heights[clicked].band.maxDeg);
            }
            ImGui::Dummy(ImVec2(0, 2));
            float lo = std::round(s.band.minDeg), hi = std::round(s.band.maxDeg);
            static int which = 0;
            const std::string label = Label("%.0f\xC2\xB0", lo) + Label(" to %.0f\xC2\xB0", hi);
            if (RangeSlider("##height_range", &lo, &hi, -90.f, 90.f, label, w, bold, which))
            {
                MakeCustom();
                SetCvarF("airlabs_height_min", std::round(lo));
                SetCvarF("airlabs_height_max", std::round(hi));
            }
            row.End();
        }
        // Direction (Flick, Track, Hybrid: Chase picks a random direction for every dash)
        if (s.mode != kChase)
        {
            const char* desc = s.mode == kFlick ? "Where balls can spawn, as seen from the driver's seat. Train your weak side. Nothing picked = anywhere."
                             : s.mode == kTrack ? "Which way the ball tends to head, as seen from the driver's seat. Nothing picked = any way."
                                                : "Where flick balls spawn and which way they head after, as seen from the driver's seat. Nothing picked = anywhere.";
            const float w = row.Begin("Ball spawn direction", desc, width - 36.f, bold, small);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const unsigned int stored = s.custom ? (static_cast<unsigned int>(*dirMask) & kAllRegions) : kAllRegions;
            unsigned int mask = stored;
            static const int grid[3][3] = { { kUpLeft, kUp, kUpRight }, { kLeft, kBehind, kRight }, { kDownLeft, kDown, kDownRight } };
            static const char* names[kRegionCount] = { "Right", "Up-right", "Up", "Up-left", "Left", "Down-left", "Down", "Down-right", "Behind" };
            const float gap = 6.f, cw = (w - 2.f * gap) / 3.f, ch = 34.f;
            const ImVec2 o = ImGui::GetCursorScreenPos();
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k)
                {
                    const int region = grid[r][k];
                    const bool usable = !(s.mode == kTrack && region == kBehind); // the moving ball can't head "behind"
                    const bool on = (mask >> region) & 1u;
                    const ImVec2 a(o.x + k * (cw + gap), o.y + r * (ch + gap)), b(a.x + cw, a.y + ch);
                    ImGui::SetCursorScreenPos(a);
                    ImGui::PushID(region);
                    const bool pressed = ImGui::InvisibleButton("##dir", ImVec2(cw, ch)) && usable;
                    const bool hover = usable && ImGui::IsItemHovered();
                    ImGui::PopID();
                    dl->AddRectFilled(a, b, !usable ? Rgb(16, 19, 26) : on ? (hover ? kAccentHi : kAccent) : (hover ? kCardHi : Rgb(31, 38, 50)), 7.f);
                    if (!on || !usable) dl->AddRect(a, b, kBorder, 7.f);
                    const ImVec2 ts = TextSize(small, names[region]);
                    DrawText(dl, small, ImVec2(a.x + (cw - ts.x) * 0.5f, a.y + (ch - ts.y) * 0.5f), !usable ? kFaint : on ? Rgb(255, 255, 255) : kDim, names[region]);
                    if (pressed) mask ^= 1u << region;
                }
            ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + 3.f * ch + 2.f * gap + 6.f));
            if (LinkButton("##dirall", "All", small, kAccentHi)) mask = kAllRegions;
            ImGui::SameLine(0, 16);
            if (LinkButton("##dirnone", "Clear", small, kAccentHi)) mask = 0;
            if (mask != stored) setI("airlabs_dirs", static_cast<int>(mask));
            row.End();
        }
        const std::string distDesc = "How far the ball is from your car. The Range buttons above set it for presets (400, 600, 900 uu). The target is always the ball itself: here it's " +
                                     Label("a %.1f\xC2\xB0 target.", s.target);
        sliderRow("##dist", "Ball distance", distDesc.c_str(), "airlabs_distance", s.distance, 300.f, kMaxBallDistance, "%.0f uu", false);
        c.End();
    }

    // ---- the mode's own settings ----
    static const char* sectionNames[kModeCount] = { "FLICK", "TRACK", "HYBRID", "CHASE" };
    SectionLabel(small, sectionNames[s.mode]);
    Card c;
    c.Begin(width, 18.f);
    auto flickRows = [&](bool hybridPart) {
        sliderRow("##minturn", "Smallest turn", "The least you'll have to turn to reach a new ball.", "airlabs_min_angle", s.minTurn, 0.f, 180.f, "%.0f\xC2\xB0");
        sliderRow("##maxturn", "Biggest turn", "The most you'll have to turn. 180 means balls right behind you.", "airlabs_max_angle", s.maxTurn, 0.f, 180.f, "%.0f\xC2\xB0");
        msRow("##hold", "Hold to confirm", "How long you stay on a ball before it counts, so you can't just swipe past it. Presets: 200 ms.",
              "airlabs_hold_ms", s.holdSec, 0, 2000);
        if (!hybridPart)
        {
            static const char* aims[] = { "Nose", "Wheels" };
            choiceRow("##aim", "Aim with", "Nose: point the front of your car at the ball. Wheels: point the bottom of your car at it, the setup for a flip reset.",
                      "airlabs_aim", s.aim, aims, 2);
        }
        toggleRow("##settle", "Must stop on target", "A ball only counts once your car has stopped turning on it, not while swinging through. Trains clean braking.",
                  "airlabs_settle", s.settle);
        if (s.settle)
            sliderRow("##settlerate", "Counts as stopped below", "How slowly you need to be turning to count as stopped. Lower is stricter.",
                      "airlabs_settle_rate", s.settleRate, 5.f, 120.f, "%.0f\xC2\xB0/s");
        toggleRow("##weak", "Weak-spot mode", "More balls in the directions you're slowest at (see Stats > Directions).",
                  "airlabs_weak_spot", s.weakSpot, hybridPart); // last row in Flick: no divider
    };
    auto speedRows = [&](bool hybridPart) {
        static const char* modes[] = { "Steady", "Changing" };
        choiceRow("##speedmode", "Ball speed", "Steady: one speed all the time. Changing: it keeps speeding up and slowing down, so you adjust how far you push the stick.",
                  "airlabs_dynamic", s.path.dynamic ? 1 : 0, modes, 2);
        if (!s.path.dynamic)
        {
            std::string d = Fmt("How fast the ball moves around you. This needs about %.0f%% stick", 100.f * StickForSpin(s.path.speed, false)) +
                            Fmt(" (%.0f%% of your car's top spin speed).", PctOfMaxSpin(s.path.speed));
            sliderRow("##speed", "Speed", d.c_str(), "airlabs_track_speed", s.path.speed, 5.f, kMaxBallSpeed, "%.0f\xC2\xB0/s");
            if (!hybridPart)
                sliderRow("##turnevery", "New direction every", "How often the ball turns onto a new path.", "airlabs_track_turn", s.path.changeEvery, 0.3f, 10.f, "%.1f s");
        }
        else
        {
            sliderRow("##vmin", "Slowest speed", "The slowest the ball gets.", "airlabs_dyn_min", s.path.vMin, 5.f, kMaxBallSpeed, "%.0f\xC2\xB0/s");
            sliderRow("##vmax", "Fastest speed", "The fastest it gets. It turns more often when slow, less often when fast.", "airlabs_dyn_max", s.path.vMax, 5.f, kMaxBallSpeed, "%.0f\xC2\xB0/s");
        }
        if (!hybridPart)
            sliderRow("##maxturnchg", "Biggest direction change", "How sharply the ball can turn: small means gentle curves, big means sharp corners.",
                      "airlabs_max_turn", s.path.maxTurn, 0.f, 180.f, "%.0f\xC2\xB0", false);
    };
    if (s.mode == kFlick) flickRows(false);
    else if (s.mode == kTrack) speedRows(false);
    else if (s.mode == kHybrid)
    {
        {
            FontScope f(small);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kFaint), "THE FLICK");
        }
        ImGui::Dummy(ImVec2(0, 2));
        flickRows(true);
        {
            FontScope f(small);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kFaint), "THE TRACK");
        }
        ImGui::Dummy(ImVec2(0, 2));
        sliderRow("##hybtrack", "Track time", "How long you follow the ball after you flick onto it. Presets: 4 s.", "airlabs_hybrid_track", s.hybridTrack, 1.f, 15.f, "%.1f s");
        speedRows(true);
        FontScope f(small);
        ImGui::TextDisabled("The ball holds the direction it takes off in for the whole track.");
    }
    else
    {
        static const char* modes[] = { "Steady", "Random" };
        choiceRow("##dashmode", "Dash speed", "Steady: every dash at the same speed. Random: each dash picks a new speed, so you read it and commit.",
                  "airlabs_dynamic", s.path.dynamic ? 1 : 0, modes, 2);
        if (!s.path.dynamic)
            sliderRow("##dashspeed", "Speed", "How fast the ball dashes across the sky.", "airlabs_track_speed", s.path.speed, 5.f, kMaxBallSpeed, "%.0f\xC2\xB0/s");
        else
        {
            sliderRow("##dashmin", "Slowest dash", "The slowest a dash can be.", "airlabs_dyn_min", s.path.vMin, 5.f, kMaxBallSpeed, "%.0f\xC2\xB0/s");
            sliderRow("##dashmax", "Fastest dash", "The fastest a dash can be.", "airlabs_dyn_max", s.path.vMax, 5.f, kMaxBallSpeed, "%.0f\xC2\xB0/s");
        }
        sliderRow("##dashshort", "Shortest dash", "How long the ball moves before it stops, at least.", "airlabs_chase_min", s.chaseMin, 0.2f, 5.f, "%.1f s");
        sliderRow("##dashlong", "Longest dash", "How long it moves, at most. Each dash picks a time in between.", "airlabs_chase_max", s.chaseMax, 0.2f, 5.f, "%.1f s", false);
    }
    c.End();
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

// ===========================================================================
// Boost Lab (test)
// ===========================================================================

void AirLabs::WinLab()
{
    const float width = ImGui::GetContentRegionAvail().x - 34.f; // keep clear of the close button
    ImFont* small = uiFonts[kFontSmall];
    ImFont* bold = uiFonts[kFontBold];
    const LabParams p = LabParamsNow();

    {
        FontScope f(uiFonts[kFontTitle]);
        ImGui::TextUnformatted("Boost Lab");
    }
    ImGui::SameLine(0, 12);
    Pill("TEST", small, Rgb(20, 22, 28), kWarn);

    // ---- what it is, and the button ----
    {
        const float btnW = 190.f, btnH = 64.f;
        Card card;
        card.Begin(width, 22.f);
        const ImVec2 top = ImGui::GetCursorScreenPos();
        const float textW = card.Inner() - btnW - 24.f;
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + textW);
        {
            FontScope f(bold);
            ImGui::TextUnformatted("Fly at the ball with real momentum.");
        }
        {
            FontScope f(small);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
            ImGui::TextWrapped("Hold boost to move towards where your nose points. Let go and you keep drifting, like in a match: "
                               "only boost changes where you're going, so to slow down or turn, point somewhere else and boost. "
                               "Your car stays put on screen and the ball moves instead. Touches work like Rocket League's: "
                               "meet the ball gently and it stays close, hit it hard and it flies.");
            ImGui::TextWrapped("The green circle with three ticks is where you're heading relative to the ball: put it on the ball "
                               "and you'll hit it. The circle with an X is the opposite way: boost there to slow down before you arrive.");
            ImGui::PopStyleColor();
        }
        if (labOn)
        {
            ImGui::Dummy(ImVec2(0, 2));
            FontScope f(bold);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGood), "Touches %d", lab.touches);
            if (p.motion == kLabRespawn)
            {
                ImGui::SameLine(0, 18);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kDim), "Got away %d", lab.misses);
            }
        }
        else
        {
            FontScope f(small);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kWarn), "Test version: no scores or leaderboard yet.");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const float leftH = ImGui::GetItemRectSize().y;
        ImGui::SetCursorScreenPos(ImVec2(top.x + card.Inner() - btnW, top.y + std::max(0.f, (leftH - btnH) * 0.5f)));
        if (BigButton("##labstart", labOn ? "STOP" : "START", labOn, ImVec2(btnW, btnH), uiFonts[kFontHeading]))
        {
            if (!labOn) closeRequested = true; // straight into it
            LabToggle();
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + std::max(leftH, btnH)));
        ImGui::Dummy(ImVec2(0, 0));
        card.End(kCard, kBorder);
        FontScope f(small);
        ImGui::TextDisabled("Changes apply straight away, even while you play. Console: airlabs_lab starts and stops it.");
    }

    // ---- on screen ----
    {
        ImGui::Dummy(ImVec2(0, 2));
        const bool markers = *labMarkers, navball = *labNavball, dust = *labDust, noRoll = noAirRoll && *noAirRoll;
        if (Chip("##labmark", markers ? "Markers: ON" : "Markers: OFF", markers, bold)) SetCvarI("airlabs_lab_markers", markers ? 0 : 1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where you're heading relative to the ball, and the opposite way");
        ImGui::SameLine(0, 8);
        if (Chip("##labnav", navball ? "Navball: ON" : "Navball: OFF", navball, bold)) SetCvarI("airlabs_lab_navball", navball ? 0 : 1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every direction around you on one disc: the middle is your nose, the rim is behind you.\nShows the ball, the markers and your drift (blue dot), even when they're off screen.");
        ImGui::SameLine(0, 8);
        if (Chip("##labdust", dust ? "Dust: ON" : "Dust: OFF", dust, bold)) SetCvarI("airlabs_lab_dust", dust ? 0 : 1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Specks that streak past as you move");
        ImGui::SameLine(0, 8);
        if (Chip("##labnoroll", noRoll ? "No air roll: ON" : "No air roll: OFF", noRoll, bold)) SetCvarI("airlabs_noroll", noRoll ? 0 : 1);
    }

    // ---- how the ball moves ----
    SectionLabel(small, "BALL MOTION");
    {
        const float gap = 12.f, tileW = (width - gap) * 0.5f;
        if (Tile("##labrespawn", "Respawn", "Each ball waits with an arrow showing where it'll go, then launches. Touch it and a new one appears.",
                 p.motion == kLabRespawn, ImVec2(tileW, 82.f), uiFonts[kFontHeading], small)) SetCvarI("airlabs_lab_motion", kLabRespawn);
        ImGui::SameLine(0, gap);
        if (Tile("##labconstant", "Constant", "The ball never stops: at the edge of the bubble it bounces back in a new direction.",
                 p.motion == kLabConstant, ImVec2(tileW, 82.f), uiFonts[kFontHeading], small)) SetCvarI("airlabs_lab_motion", kLabConstant);
    }

    ImGui::PushItemWidth(300.f);
    // ---- ball speed: presets like Track's, or Dynamic ----
    SectionLabel(small, "BALL SPEED");
    {
        static const struct { const char* name; float v; } speeds[3] = { { "Slow", 400.f }, { "Medium", 800.f }, { "Fast", 1400.f } };
        for (int i = 0; i < 3; ++i)
        {
            if (i) ImGui::SameLine(0, 8);
            ImGui::PushID(i);
            if (Chip("##labspd", speeds[i].name, !p.dynamic && std::fabs(p.speed - speeds[i].v) < 0.5f, bold, 34.f))
            {
                SetCvarF("airlabs_lab_speed", speeds[i].v);
                SetCvarI("airlabs_lab_dynamic", 0);
            }
            ImGui::PopID();
        }
        ImGui::SameLine(0, 8);
        if (Chip("##labdyn", "Dynamic", p.dynamic, bold, 34.f)) SetCvarI("airlabs_lab_dynamic", p.dynamic ? 0 : 1);
        if (p.dynamic)
        {
            UiSliderF("Slowest##lab", "Each launch or bounce picks a random speed between these.", "airlabs_lab_speed_min", 100.f, 2300.f, "%.0f uu/s");
            UiSliderF("Fastest##lab", "Your car's top speed is 2300.", "airlabs_lab_speed_max", 100.f, 2300.f, "%.0f uu/s");
        }
        else
        {
            UiSliderF("Ball speed##lab", "How fast the ball moves after it launches or bounces. Your car's top speed is 2300.",
                      "airlabs_lab_speed", 100.f, 2300.f, "%.0f uu/s");
        }
        UiSliderF("Curve##lab", "How fast the ball turns while it flies on its own (it picks a new way to curve every second). 0 = straight lines.",
                  "airlabs_lab_curve", 0.f, 120.f, "%.0f deg/s");
    }

    // ---- where balls appear ----
    SectionLabel(small, "RANGE AND DIRECTIONS");
    {
        static const struct { const char* name; float v; } ranges[3] = { { "Short", 500.f }, { "Medium", 800.f }, { "Long", 1200.f } };
        for (int i = 0; i < 3; ++i)
        {
            if (i) ImGui::SameLine(0, 8);
            ImGui::PushID(10 + i);
            if (Chip("##labrng", ranges[i].name, std::fabs(p.bubble - ranges[i].v) < 0.5f, bold, 34.f)) SetCvarF("airlabs_lab_bubble", ranges[i].v);
            ImGui::PopID();
        }
        UiSliderF("Bubble radius##lab", "The ball always stays within this distance of your car. On the standard field it also stops at the floor, walls and roof (about 850 uu up or down); the space map has none.",
                  "airlabs_lab_bubble", 300.f, 2000.f, "%.0f uu");
        UiSliderF("Around your nose##lab", "New balls appear within this angle of where your nose points. 180 = anywhere, 90 = never behind you.",
                  "airlabs_lab_cone", 10.f, 180.f, "%.0f deg");
        UiSliderF("Lowest##lab", "How far below (negative) or above the horizon a new ball can appear.", "airlabs_lab_height_min", -90.f, 90.f, "%.0f deg");
        UiSliderF("Highest##lab", "Set both to 0 for balls level with the horizon only.", "airlabs_lab_height_max", -90.f, 90.f, "%.0f deg");
    }

    // ---- run ----
    SectionLabel(small, "RUN");
    UiSliderF("Run length##lab", "How long a run lasts, then it shows how you did. 0 = endless.", "airlabs_lab_run", 0.f, 300.f, "%.0f s");
    UiSliderF("Pass distance##lab", "A launched or bounced ball never heads straight at you: its path passes at least this far from your car's centre, so you always have to move to reach it. Bigger is harder.",
              "airlabs_lab_pass", 0.f, 600.f, "%.0f uu");
    if (p.motion == kLabRespawn)
        UiSliderF("Wait before launch##lab", "How long a new ball waits (showing its arrow) before it takes off. Your speed resets with each new ball.",
                  "airlabs_lab_hang", 0.f, 3.f, "%.2f s");
    ImGui::PopItemWidth();
    ImGui::Dummy(ImVec2(0, 6));
    if (LinkButton("##labdefaults", "Back to the defaults", small, kAccentHi))
    {
        SetCvarF("airlabs_lab_bubble", 800.f);
        SetCvarF("airlabs_lab_pass", 250.f);
        SetCvarF("airlabs_lab_speed", 800.f);
        SetCvarI("airlabs_lab_dynamic", 0);
        SetCvarF("airlabs_lab_speed_min", 400.f);
        SetCvarF("airlabs_lab_speed_max", 1400.f);
        SetCvarF("airlabs_lab_curve", 0.f);
        SetCvarF("airlabs_lab_cone", 180.f);
        SetCvarF("airlabs_lab_height_min", -90.f);
        SetCvarF("airlabs_lab_height_max", 90.f);
        SetCvarF("airlabs_lab_run", 0.f);
        SetCvarF("airlabs_lab_hang", 0.75f);
    }
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

    // Open on Flick Mixed, Medium range: the busiest board (the website opens there too).
    if (lbMode < 0)
    {
        lbMode = kFlick;
        lbPreset = 0;
        for (int i = 0; i < PresetCount(kFlick); ++i)
            if (std::string_view(kFlickPresets[i].name) == "Mixed") lbPreset = i;
        lbRange = kMedium;
        lbNoRoll = false;
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
        name = lbName; // the game's name; a picked one (airlabs_lb_name) is shown instead below
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
        ImGui::TextDisabled("Everyone's best one-minute run on every preset, with their Rocket League ranks. Watch any run that has a replay.");
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
    std::string picked = lbCustomName ? *lbCustomName : std::string();
    while (!picked.empty() && picked.back() == ' ') picked.pop_back();
    const std::string gameName = name;
    if (!picked.empty()) name = picked;
    if (!posting)
    {
        Card c;
        c.Begin(width, 20.f);
        {
            FontScope f(uiFonts[kFontHeading]);
            ImGui::TextUnformatted("Put your runs on the leaderboard");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kDim));
        ImGui::TextWrapped("Your best run on each preset goes on this public leaderboard under your name%s, with its replay so anyone can watch it. "
                           "Only preset runs count. While this is off, your best run on each board is kept here, and it goes up when you turn it back on.",
                           name.empty() ? "" : (" (" + name + ")").c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 2));
        if (Chip("##lbjoin", "Post my runs", true, bold, 36.f))
        {
            SetCvarI("airlabs_lb_post", 1);
            gameWrapper->Execute([this](GameWrapper*) { RetryPending(); }); // send what's waiting straight away
        }
        c.End();
    }
    else
    {
        Dot(kGood);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kGood), "%s", name.empty() ? "Posting your runs" : ("Posting your runs as " + name).c_str());
        ImGui::SameLine(0, 14);
        if (LinkButton("##lbname", "Change name", small)) { lbEditingName = !lbEditingName; std::snprintf(lbNameEdit, sizeof lbNameEdit, "%s", picked.c_str()); }
        ImGui::SameLine(0, 14);
        if (LinkButton("##lbstop", "Stop posting", small)) SetCvarI("airlabs_lb_post", 0);
    }
    if (lbEditingName)
    {
        // The name on the boards: one you pick, or blank for your Rocket League name.
        FontScope f(small);
        auto setName = [this](const std::string& v) {
            CVarWrapper cv = cvarManager->getCvar("airlabs_lb_name");
            if (!cv.IsNull()) cv.setValue(v);
            lbEditingName = false;
            gameWrapper->Execute([this](GameWrapper*) { SendName(); });
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name on the leaderboard");
        ImGui::SameLine(0, 10);
        ImGui::SetNextItemWidth(240.f);
        const bool enter = ImGui::InputText("##lbnameinput", lbNameEdit, sizeof lbNameEdit, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(0, 10);
        if (Chip("##lbnamesave", "Save", true, small, 30.f) || enter)
        {
            std::string v = lbNameEdit;
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            setName(v);
        }
        ImGui::SameLine(0, 12);
        ImGui::AlignTextToFramePadding();
        if (!picked.empty() && LinkButton("##lbnamegame", "Use my Rocket League name", small, kAccentHi)) setName("");
        if (!picked.empty()) ImGui::SameLine(0, 12);
        if (LinkButton("##lbnamecancel", "Cancel", small)) lbEditingName = false;
        ImGui::TextDisabled("%s", ("Leave it blank to use your Rocket League name" + (gameName.empty() ? std::string() : " (" + gameName + ")") +
                                   ". A name another player already uses can't be taken.").c_str());
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
            ImGui::TextDisabled("(they go up one at a time, by themselves)");
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
        if (board.loaded && board.max > 0) count += (count.empty() ? "" : "  \xC2\xB7  ") + std::string("max ") + FmtInt(board.max);
        const float tw = TextSize(uiFonts[kFontHeading], title.c_str()).x;
        DrawText(dl, small, ImVec2(p.x + 18.f + tw + 12.f, p.y + 14.f + SizeOf(uiFonts[kFontHeading]) - SizeOf(small) - 2.f), kFaint, count.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + inner - 90.f, p.y + 14.f));
        if (LinkButton("##lbrefresh", board.loading ? "Loading..." : "Refresh", small) && !board.loading) FetchBoard(key, true);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 52.f));
        dl->AddLine(ImVec2(p.x, p.y + 52.f), ImVec2(p.x + inner, p.y + 52.f), kBorder);
    }
    const float rowH = 42.f;
    // Columns, right to left: Watch, date, score, input, the three Rocket League ranks.
    const float rankW = 38.f, rankGap = 6.f, inputX = inner - 230.f - 170.f, ranksRight = inputX - 16.f;
    bool anyRank = false, anyInput = false;
    for (const LbRow& r : board.rows) { for (int i = 0; i < 3; ++i) anyRank |= r.rank[i] >= 0; anyInput |= r.input >= 0; }
    {
        std::lock_guard<std::mutex> lock(lbMutex);
        if (board.mePos > 0) for (int i = 0; i < 3; ++i) anyRank |= lbRealRank[i] >= 0;
    }
    if ((anyRank || anyInput) && !board.rows.empty())
    {
        // labels for the Rocket League rank column: 1s 2s 3s
        const ImVec2 q = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(inner, 22.f));
        static const char* pl[3] = { "1s", "2s", "3s" };
        if (anyRank)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float cx = q.x + ranksRight - (3 - i) * (rankW + rankGap) + rankW * 0.5f;
                DrawText(dl, small, ImVec2(cx - TextSize(small, pl[i]).x * 0.5f, q.y + 4.f), kFaint, pl[i]);
            }
            DrawText(dl, small, ImVec2(q.x + ranksRight - 3 * (rankW + rankGap) - TextSize(small, "Rocket League").x - 10.f, q.y + 4.f), kFaint, "Rocket League");
        }
        if (anyInput) DrawText(dl, small, ImVec2(q.x + inputX, q.y + 4.f), kFaint, "Input");
    }
    auto drawRow = [&](const LbRow& r, bool mine, bool alt) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::PushID(r.run);
        ImGui::Dummy(ImVec2(inner, rowH));
        if (mine) dl->AddRectFilled(p, ImVec2(p.x + inner, p.y + rowH), kAccentBg);
        else if (alt) dl->AddRectFilled(p, ImVec2(p.x + inner, p.y + rowH), Rgb(255, 255, 255, 6));
        const float cy = p.y + rowH * 0.5f;
        const std::string pos = "#" + std::to_string(r.pos);
        DrawText(dl, bold, ImVec2(p.x + 18.f, cy - SizeOf(bold) * 0.5f), r.pos == 1 ? kGold : r.pos <= 3 ? kText : kDim, pos.c_str());
        const float nx = p.x + 76.f;
        DrawText(dl, bold, ImVec2(nx, cy - SizeOf(bold) * 0.5f), kText, r.name.c_str());
        if (anyRank)
            for (int i = 0; i < 3; ++i)
                RealRankBadge(r.rank[i], 22.f, small, dl, ImVec2(p.x + ranksRight - (3 - i) * (rankW + rankGap), cy - 11.f));
        if (r.input >= 0) // a little controller or keyboard, and the word
        {
            const ImVec2 ic(p.x + inputX, cy - 6.f);
            if (r.input == 0)
            {
                dl->AddRectFilled(ImVec2(ic.x, ic.y + 1.f), ImVec2(ic.x + 18.f, ic.y + 10.f), kDim, 4.5f);
                dl->AddRectFilled(ImVec2(ic.x + 1.f, ic.y + 6.f), ImVec2(ic.x + 6.f, ic.y + 12.f), kDim, 2.5f);
                dl->AddRectFilled(ImVec2(ic.x + 12.f, ic.y + 6.f), ImVec2(ic.x + 17.f, ic.y + 12.f), kDim, 2.5f);
                dl->AddCircleFilled(ImVec2(ic.x + 13.5f, ic.y + 4.5f), 1.2f, kCard, 8);
                dl->AddLine(ImVec2(ic.x + 3.f, ic.y + 4.5f), ImVec2(ic.x + 7.f, ic.y + 4.5f), kCard, 1.4f);
                dl->AddLine(ImVec2(ic.x + 5.f, ic.y + 2.5f), ImVec2(ic.x + 5.f, ic.y + 6.5f), kCard, 1.4f);
            }
            else
            {
                dl->AddRect(ImVec2(ic.x, ic.y), ImVec2(ic.x + 18.f, ic.y + 12.f), kDim, 2.5f, ImDrawCornerFlags_All, 1.4f);
                for (int k = 0; k < 4; ++k) dl->AddRectFilled(ImVec2(ic.x + 3.f + k * 3.5f, ic.y + 3.f), ImVec2(ic.x + 4.6f + k * 3.5f, ic.y + 4.6f), kDim);
                dl->AddLine(ImVec2(ic.x + 5.f, ic.y + 8.5f), ImVec2(ic.x + 13.f, ic.y + 8.5f), kDim, 1.4f);
            }
            DrawText(dl, small, ImVec2(ic.x + 24.f, cy - SizeOf(small) * 0.5f), kDim, r.input == 1 ? "Keyboard" : "Controller");
        }
        const float nw = TextSize(bold, r.name.c_str()).x;
        const std::string tag = r.platform + (mine ? std::string(r.platform.empty() ? "" : "  \xC2\xB7  ") + "you" : "") +
                                (r.note.empty() ? "" : "  \xC2\xB7  " + r.note);
        DrawText(dl, small, ImVec2(nx + nw + 10.f, cy - SizeOf(small) * 0.5f + 1.f), mine && !r.note.empty() ? kDim : kFaint, tag.c_str());
        const std::string sc = FmtInt(r.score);
        const float scoreRight = p.x + inner - 230.f;
        DrawText(dl, bold, ImVec2(scoreRight - TextSize(bold, sc.c_str()).x, cy - SizeOf(bold) * 0.5f), kText, sc.c_str());
        DrawText(dl, small, ImVec2(scoreRight + 24.f, cy - SizeOf(small) * 0.5f), kFaint, r.date.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + inner - 104.f, cy - 15.f));
        const bool thisLoading = loading == r.run;
        if (!r.replay)
        {
            ImGui::Dummy(ImVec2(80.f, 30.f));
            DrawText(dl, small, ImVec2(p.x + inner - 100.f, cy - SizeOf(small) * 0.5f), kFaint, "No replay");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("A saved best from before AirLabs recorded replays");
        }
        else if (Chip("##watch", thisLoading ? "Loading" : "Watch", false, small, 30.f) && loading == 0)
        {
            WatchReplay(r.run);
            closeRequested = true; // close the window: the replay plays in the game
        }
        if (r.replay && ImGui::IsItemHovered()) ImGui::SetTooltip("Watch this run in the game (in Freeplay; it loads the map if you're not there)");
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
        ImGui::PopID();
    };
    auto message = [&](const char* text, ImU32 col) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(inner, 70.f));
        const ImVec2 ts = TextSize(uiFonts[kFontBody], text, inner - 40.f);
        DrawText(dl, uiFonts[kFontBody], ImVec2(p.x + (inner - ts.x) * 0.5f, p.y + (70.f - ts.y) * 0.5f), col, text, inner - 40.f);
    };
    // ---- your spot, pinned above the list: a row like the others (Watch included), so you never
    //      have to scroll down to yourself ----
    if (board.loaded && board.mePos > 0)
    {
        LbRow me;
        me.pos = board.mePos; me.score = board.meScore; me.run = board.meRun; me.date = board.meDate;
        me.replay = board.meReplay; me.input = board.meInput; me.pid = pid; me.name = name.empty() ? std::string("You") : name;
        for (const LbRow& r : board.rows) if (r.pid == pid) { me.platform = r.platform; for (int i = 0; i < 3; ++i) me.rank[i] = r.rank[i]; }
        if (me.platform.empty())
        {
            std::lock_guard<std::mutex> lock(lbMutex);
            for (int i = 0; i < 3; ++i) me.rank[i] = lbRealRank[i];
        }
        if (board.total > 1) me.note = "top " + std::to_string(std::max(1, static_cast<int>(std::ceil(100.0 * board.mePos / board.total)))) + "%";
        if (board.mePos > 1 && board.mePos - 2 < static_cast<int>(board.rows.size()))
            me.note += "  \xC2\xB7  " + FmtInt(std::max(1, board.rows[board.mePos - 2].score - board.meScore + 1)) + " to #" + std::to_string(board.mePos - 1);
        drawRow(me, true, false);
        const ImVec2 q = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(inner, 6.f));
        dl->AddRectFilled(ImVec2(q.x, q.y + 2.f), ImVec2(q.x + inner, q.y + 4.f), kBorder);
    }
    else if (board.loaded)
    {
        const ImVec2 q = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(inner, 42.f));
        dl->AddRectFilled(q, ImVec2(q.x + inner, q.y + 42.f), kAccentBg);
        DrawText(dl, bold, ImVec2(q.x + 18.f, q.y + 21.f - SizeOf(bold) * 0.5f), kDim,
                 lbPost && *lbPost ? "You're not on this board yet. Play it, and your best goes up."
                                   : "You're not on this board. Turn posting back on to put your runs up.");
    }
    if (!board.error.empty() && board.rows.empty()) message(board.error.c_str(), kWarn);
    else if (!board.loaded) message("Loading...", kDim);
    else if (board.rows.empty()) message("No runs on this board yet. Play it and the top spot is yours.", kDim);
    else
    {
        bool meShown = false;
        for (std::size_t i = 0; i < board.rows.size(); ++i)
        {
            const bool mine = !pid.empty() && board.rows[i].pid == pid;
            meShown |= mine;
            drawRow(board.rows[i], mine, i % 2 == 1);
        }
        (void)meShown; // your place is pinned at the top of the board
    }
    ImGui::Dummy(ImVec2(0, 4));
    c.End();
    {
        FontScope f(small);
        ImGui::TextDisabled("Also on the web: %s", LbUrl("").c_str());
    }
}
