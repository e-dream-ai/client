#pragma once

// Shared keyboard feed for the Linux ImGui overlays (first-run wizard, Settings).
// Backend-neutral: the Wayland and X11 paths in DisplayVulkan each work out the
// keysym, modifiers and typed text, then call the overlay's FeedKey, which calls
// this. X11 and xkb keysyms are the same values, so <X11/keysym.h> serves both
// and the overlays need no xkbcommon (a build without Wayland lacks it).

#if !defined(WIN32) && !defined(MAC)

#include <cstdint>

#include <X11/keysym.h>
#include <imgui.h>

/// `keysym` is the layout-aware keysym (Shift applied, so Ctrl+Shift+A gives XK_A).
/// `utf8` is the text the key types, NUL-terminated, or null/empty for none.
inline void ImGuiFeedLinuxKey(uint32_t keysym, bool pressed, bool ctrl, bool shift,
                              const char* utf8)
{
    ImGuiIO& io = ImGui::GetIO();

    ImGuiKey imkey = ImGuiKey_None;
    switch (keysym)
    {
    case XK_BackSpace: imkey = ImGuiKey_Backspace;  break;
    case XK_Delete:
    case XK_KP_Delete: imkey = ImGuiKey_Delete;     break;
    case XK_Return:
    case XK_KP_Enter:  imkey = ImGuiKey_Enter;      break;
    case XK_Tab:
    case XK_ISO_Left_Tab: imkey = ImGuiKey_Tab;     break;
    case XK_Left:
    case XK_KP_Left:   imkey = ImGuiKey_LeftArrow;  break;
    case XK_Right:
    case XK_KP_Right:  imkey = ImGuiKey_RightArrow; break;
    case XK_Home:
    case XK_KP_Home:   imkey = ImGuiKey_Home;       break;
    case XK_End:
    case XK_KP_End:    imkey = ImGuiKey_End;        break;
    case XK_Insert:
    case XK_KP_Insert: imkey = ImGuiKey_Insert;     break;
    case XK_Escape:    imkey = ImGuiKey_Escape;     break;
    default: break;
    }

    if (ctrl)
    {
        switch (keysym)
        {
        case XK_a: case XK_A: imkey = ImGuiKey_A; break;
        case XK_c: case XK_C: imkey = ImGuiKey_C; break;
        case XK_v: case XK_V: imkey = ImGuiKey_V; break;
        case XK_x: case XK_X: imkey = ImGuiKey_X; break;
        case XK_z: case XK_Z: imkey = ImGuiKey_Z; break;
        default: break;
        }
    }

    // Shift+Insert is the classic alternate paste shortcut. Route it through
    // ImGui's Ctrl+V paste path so GetClipboardTextFn is called.
    if (shift && imkey == ImGuiKey_Insert)
    {
        io.AddKeyEvent(ImGuiMod_Shift, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_V, pressed);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        return;
    }

    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
    io.AddKeyEvent(ImGuiMod_Shift, shift);
    if (imkey != ImGuiKey_None)
        io.AddKeyEvent(imkey, pressed);

    // Typed text. Skip control characters (< 0x20) and DEL; Backspace, Enter
    // and Tab arrive as keys above.
    if (pressed && !ctrl && utf8 &&
        static_cast<unsigned char>(utf8[0]) >= 0x20 &&
        static_cast<unsigned char>(utf8[0]) != 0x7f)
    {
        io.AddInputCharactersUTF8(utf8);
    }
}

#endif // !WIN32 && !MAC
