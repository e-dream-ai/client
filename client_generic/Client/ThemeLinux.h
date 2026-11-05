#ifndef THEMELINUX_H
#define THEMELINUX_H

#include <cmath>

#include <imgui.h>

// The system light/dark choice, from the XDG Desktop Portal (org.freedesktop.appearance), which
// GNOME, KDE and other desktops implement. Dark when the desktop doesn't say.
namespace ThemeLinux {

// Starts the background thread that reads the choice and follows changes to it.
void Start();

// Cheap enough to call every frame from the render thread.
bool IsDark();

// The swapchain is sRGB and ImGui writes its colors unconverted, so the GPU gamma-encodes them: a
// 0.15 gray reaches the screen as 0.42. That barely shows on light colors but washes out dark ones,
// so dark colors go through here to reach the screen at the values written.
inline float DisplayToLinear(float c)
{
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline ImVec4 DarkColor(float r, float g, float b, float a)
{
    return ImVec4(DisplayToLinear(r), DisplayToLinear(g), DisplayToLinear(b), a);
}

inline void ApplyDarkBaseColors(ImGuiStyle& style)
{
    ImGui::StyleColorsDark(&style);
    for (ImVec4& c : style.Colors)
        c = DarkColor(c.x, c.y, c.z, c.w);
}

} // namespace ThemeLinux

#endif // THEMELINUX_H
