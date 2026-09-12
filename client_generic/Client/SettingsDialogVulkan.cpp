#if !defined(WIN32) && !defined(MAC)

#include "SettingsDialogVulkan.h"

#include "CacheManager.h"
#include "ContentDownloader.h"
#include "EDreamClient.h"
#include "PlatformUtils.h"
#include "PlatformUtils_Internal.h"
#include "Player.h"
#include "ServerConfig.h"
#include "Settings.h"
#include "storage.h"
#include "client.h"
#include "Log.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <imgui.h>

#ifdef HAVE_WAYLAND
#include <xkbcommon/xkbcommon.h>
#endif

namespace {

#ifdef STAGE
constexpr const char* kUrlCreateAccount = "https://stage.infinidream.ai/account";
constexpr const char* kUrlHelp          = "https://stage.infinidream.ai/help";
constexpr const char* kUrlWebRemote     = "https://stage.infinidream.ai/rc";
constexpr const char* kUrlPlaylists     = "https://stage.infinidream.ai/playlists";
#else
constexpr const char* kUrlCreateAccount = "https://alpha.infinidream.ai/account";
constexpr const char* kUrlHelp          = "https://alpha.infinidream.ai/help";
constexpr const char* kUrlWebRemote     = "https://alpha.infinidream.ai/rc";
constexpr const char* kUrlPlaylists     = "https://alpha.infinidream.ai/playlists";
#endif

static float g_uiScale = 1.0f; // refreshed from PlatformUtils_GetUIScale() on each show
static inline float S(float v) { return v * g_uiScale; }

std::atomic<bool> g_showRequested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_wasPausedBeforeDialog{false};
std::atomic<bool> g_wasUserPausedBeforeDialog{false};
std::atomic<bool> g_pausedBySettingsDialog{false};
std::atomic<bool> g_fontsLoaded{false};

ImFont* g_regularUiFont = nullptr;
ImFont* g_boldUiFont    = nullptr;

char g_nicknameBuf[256]          = {};
char g_codeBuf[32]               = {};
char g_previousLoginEmailBuf[256] = {};
bool g_hasPreviousLoginEmail     = false;
bool g_sentCode                  = false;
char g_statusBuf[256]            = {};
char g_errorPopupMessage[512]    = {};
std::string g_versionText;

double g_playerFps      = 23.0;
double g_displayFps     = 60.0;
bool   g_vsync          = false;
bool   g_preserveAR     = false;
bool   g_quietMode      = true;
bool   g_showAttribution = false;

char g_contentDirBuf[512] = {};
bool g_unlimitedCache     = false;
int  g_cacheSizeGb        = 10;

bool g_useProxy              = false;
char g_proxyHostBuf[512]     = {};
char g_proxyLoginBuf[256]    = {};
char g_proxyPasswordBuf[256] = {};
bool g_debugLog              = false;
char g_serverBuf[512]        = {};

// ---------------------------------------------------------------------------
// Font loading
// ---------------------------------------------------------------------------

static std::string FindSystemFont(const char* name)
{
    static const char* const kDirs[] = {
        "/usr/share/fonts/TTF/",
        "/usr/share/fonts/truetype/",
        "/usr/share/fonts/noto/",
        "/usr/share/fonts/truetype/noto/",
        "/usr/share/fonts/truetype/liberation/",
        "/usr/share/fonts/truetype/dejavu/",
        "/usr/share/fonts/",
        nullptr,
    };
    for (int i = 0; kDirs[i]; ++i)
    {
        std::string path = std::string(kDirs[i]) + name;
        if (FILE* f = fopen(path.c_str(), "rb"))
        {
            fclose(f);
            return path;
        }
    }
    return {};
}

static void LoadDialogFonts()
{
    if (g_fontsLoaded.load(std::memory_order_acquire)) return;

    ImGuiIO& io = ImGui::GetIO();

    static const char* const kRegular[] = {
        "NotoSans-Regular.ttf", "NotoSans[wdth,wght].ttf",
        "LiberationSans-Regular.ttf", "DejaVuSans.ttf", nullptr,
    };
    static const char* const kBold[] = {
        "NotoSans-Bold.ttf", "NotoSans[wdth,wght].ttf",
        "LiberationSans-Bold.ttf", "DejaVuSans-Bold.ttf", nullptr,
    };

    std::string regular, bold;
    for (int i = 0; kRegular[i] && regular.empty(); ++i)
        regular = FindSystemFont(kRegular[i]);
    for (int i = 0; kBold[i] && bold.empty(); ++i)
        bold = FindSystemFont(kBold[i]);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;

    constexpr float kUiTextSize = 20.f; // design-space baseline; always used as S(kUiTextSize)

    if (!regular.empty())
    {
        g_regularUiFont = io.Fonts->AddFontFromFileTTF(regular.c_str(), S(kUiTextSize), &cfg);
        g_Log->Info("SettingsDialog: loaded regular font '%s' at %.0fpx (uiScale=%.2f)",
                    regular.c_str(), S(kUiTextSize), g_uiScale);
    }
    else
    {
        g_Log->Warning("SettingsDialog: no regular font found; using embedded fallback");
    }

    if (!bold.empty())
    {
        g_boldUiFont = io.Fonts->AddFontFromFileTTF(bold.c_str(), S(kUiTextSize), &cfg);
        g_Log->Info("SettingsDialog: loaded bold font '%s' at %.0fpx", bold.c_str(), S(kUiTextSize));
    }

    g_fontsLoaded.store(true, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

static const char* GetClipboardText(void*)
{
    static std::string s_buf;
    s_buf.clear();
    static const char* const kCmds[] = {
        "wl-paste --no-newline 2>/dev/null",
        "xclip -selection clipboard -o 2>/dev/null",
        "xsel --clipboard --output 2>/dev/null",
        nullptr,
    };
    for (int i = 0; kCmds[i]; ++i)
    {
        FILE* fp = popen(kCmds[i], "r");
        if (!fp) continue;
        char buf[256];
        while (fgets(buf, sizeof buf, fp))
            s_buf += buf;
        pclose(fp);
        if (!s_buf.empty()) return s_buf.c_str();
    }
    return s_buf.c_str();
}

// ---------------------------------------------------------------------------
// Inlined helpers and state
// ---------------------------------------------------------------------------

#include "SettingsDialogVulkan/SettingsDialogVulkan.CommonTextUi.inl"
#include "SettingsDialogVulkan/SettingsDialogVulkan.CommonSettingsState.inl"
#include "SettingsDialogVulkan/SettingsDialogVulkan.DialogLifecycle.inl"

// ---------------------------------------------------------------------------
// Tab draw functions
// ---------------------------------------------------------------------------

static void DrawAccountTab()
{
    #include "SettingsDialogVulkan/SettingsDialogVulkan.TabAccount.inl"
}

static void DrawControlsTab()
{
    #include "SettingsDialogVulkan/SettingsDialogVulkan.TabControls.inl"
}

static void DrawDiskTab()
{
    #include "SettingsDialogVulkan/SettingsDialogVulkan.TabDisk.inl"
}

static void DrawDisplayTab()
{
    #include "SettingsDialogVulkan/SettingsDialogVulkan.TabDisplay.inl"
}

static void DrawAdvancedTab()
{
    #include "SettingsDialogVulkan/SettingsDialogVulkan.TabAdvanced.inl"
}

// ---------------------------------------------------------------------------
// Dialog layout
// ---------------------------------------------------------------------------

static void DrawSettingsDialog(float viewportW, float viewportH)
{
    const float targetWidth  = S(541.f);
    const float targetHeight = S(450.f);
    const float windowWidth  = (viewportW > (targetWidth + S(64.f))) ? targetWidth : (viewportW - S(32.f));
    const float windowHeight = (viewportH > (targetHeight + S(64.f))) ? targetHeight : (viewportH - S(32.f));
    const ImVec2 windowSize((windowWidth < S(460.f)) ? S(460.f) : windowWidth,
                            (windowHeight < S(340.f)) ? S(340.f) : windowHeight);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((viewportW - windowSize.x) * 0.5f, (viewportH - windowSize.y) * 0.5f),
                            ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18.f), S(16.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10.f), S(10.f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.98f, 0.98f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled,  ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Tab,           ImVec4(0.92f, 0.92f, 0.92f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabActive,     ImVec4(0.98f, 0.98f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered,    ImVec4(0.95f, 0.95f, 0.95f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.93f, 0.93f, 0.93f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.88f, 0.88f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.84f, 0.84f, 0.84f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(0.97f, 0.97f, 0.97f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.95f, 0.95f, 0.95f, 1.00f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##SettingsDialog", nullptr, flags))
    {
        const float footerHeight    = S(44.f);
        const float separatorReserve = ImGui::GetStyle().ItemSpacing.y + S(2.f);
        float contentHeight = ImGui::GetContentRegionAvail().y - footerHeight - separatorReserve;
        if (contentHeight < S(120.f)) contentHeight = S(120.f);

        ImGui::BeginChild("settings_content_region", ImVec2(0.f, contentHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Flat tab strip with hairline + accent underline on active tab.
        ImGui::PushStyleColor(ImGuiCol_Tab,              ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_TabHovered,       ImVec4(0.f, 0.f, 0.f, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_TabActive,        ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocused,     ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_TabBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.f), S(8.f)));

        bool activeTabFound = false;
        ImVec2 activeTabMin(0.f, 0.f);
        ImVec2 activeTabMax(0.f, 0.f);
        const auto drawTabItem = [&](const char* label, void (*body)()) {
            const bool open = ImGui::BeginTabItem(label);
            if (open && !activeTabFound)
            {
                activeTabMin = ImGui::GetItemRectMin();
                activeTabMax = ImGui::GetItemRectMax();
                activeTabFound = true;
            }
            if (open)
            {
                ImGui::PopStyleVar(); // FramePadding
                body();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.f), S(8.f)));
                ImGui::EndTabItem();
            }
        };

        if (ImGui::BeginTabBar("settings_tabs"))
        {
            drawTabItem("Account",  DrawAccountTab);
            drawTabItem("Controls", DrawControlsTab);
            drawTabItem("Disk",     DrawDiskTab);
            drawTabItem("Display",  DrawDisplayTab);
            drawTabItem("Advanced", DrawAdvancedTab);
            ImGui::EndTabBar();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        if (activeTabFound)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
            const ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.478f, 1.f, 1.f));
            const ImVec2 wPos = ImGui::GetWindowPos();
            const float wW = ImGui::GetWindowWidth();
            const float lineY = activeTabMax.y;
            dl->AddLine(ImVec2(wPos.x, lineY), ImVec2(wPos.x + wW, lineY), borderCol, 1.f);
            dl->AddLine(ImVec2(activeTabMin.x, lineY), ImVec2(activeTabMax.x, lineY), accentCol, S(2.f));
        }

        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("settings_footer_region", ImVec2(0.f, 0.f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const float helpButtonSize    = S(24.f);
            const float closeButtonWidth  = S(90.f);
            const float closeButtonHeight = S(32.f);
            const float originX       = ImGui::GetCursorPosX();
            const float contentWidth  = ImGui::GetContentRegionAvail().x;
            const float contentHeight = ImGui::GetContentRegionAvail().y;
            const float rowCenterY    = ImGui::GetCursorPosY() + (contentHeight * 0.5f);
            const float buttonY       = rowCenterY - (closeButtonHeight * 0.5f);
            const float textY         = rowCenterY - (ImGui::GetTextLineHeight() * 0.5f);
            const float closeX        = originX + contentWidth - closeButtonWidth;

            ImGui::SetCursorPos(ImVec2(closeX, buttonY));
            if (ImGui::Button("Close", ImVec2(closeButtonWidth, closeButtonHeight)))
                CloseDialog(true);

            // Help button: InvisibleButton + manual draw for pixel-perfect "?" centering.
            ImGui::SetCursorPos(ImVec2(originX, rowCenterY - (helpButtonSize * 0.5f)));
            const bool helpClicked = ImGui::InvisibleButton("##help_btn", ImVec2(helpButtonSize, helpButtonSize));
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 btnMin = ImGui::GetItemRectMin();
                const ImVec2 center(btnMin.x + helpButtonSize * 0.5f, btnMin.y + helpButtonSize * 0.5f);
                const ImU32 bgCol = ImGui::IsItemActive()  ? ImGui::GetColorU32(ImGuiCol_ButtonActive)  :
                                    ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_ButtonHovered) :
                                                             ImGui::GetColorU32(ImGuiCol_Button);
                dl->AddCircleFilled(center, helpButtonSize * 0.5f, bgCol, 24);
                dl->AddCircle(center, helpButtonSize * 0.5f, ImGui::GetColorU32(ImGuiCol_Border), 24, 1.f);
                const char* q = "?";
                const ImVec2 qs = ImGui::CalcTextSize(q);
                dl->AddText(ImVec2(center.x - qs.x * 0.5f, center.y - qs.y * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_Text), q);
            }
            if (helpClicked)
                PlatformUtils::OpenURLExternally(kUrlHelp);

            ImGui::SameLine();
            ImGui::SetCursorPosY(textY);
            ImGui::TextDisabled("%s", g_versionText.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::End();
    ImGui::PopStyleColor(13);
    ImGui::PopStyleVar(4);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        CloseDialog(true);
}

// ---------------------------------------------------------------------------
// Callback registered with ESSetShowPreferencesCallback
// ---------------------------------------------------------------------------

static void OnShowPreferencesRequested()
{
    g_showRequested.store(true, std::memory_order_release);
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void SettingsDialogVulkan_Register()
{
    ESSetShowPreferencesCallback(OnShowPreferencesRequested);
}

bool SettingsDialogVulkan_IsVisible()
{
    return g_visible.load(std::memory_order_acquire);
}

void SettingsDialogVulkan_Toggle()
{
    if (g_visible.load(std::memory_order_acquire))
        CloseDialog(true);
    else
        OnShowPreferencesRequested();
}

void SettingsDialogVulkan_DrawIfNeeded()
{
    if (g_showRequested.load(std::memory_order_acquire))
    {
        g_showRequested.store(false, std::memory_order_release);

        g_uiScale = PlatformUtils_GetUIScale();
        LoadDialogFonts();

        // Re-apply io.FontDefault on every open: the wizard may have overwritten it
        // with its own S(15) body font between dialog opens.
        if (g_regularUiFont)
            ImGui::GetIO().FontDefault = g_regularUiFont;

        ImGui::GetStyle().ScaleAllSizes(g_uiScale);
        ImGui::StyleColorsLight();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::GetIO().GetClipboardTextFn = GetClipboardText;

        ResetFormForShow();
        ApplyDialogPauseState(true);
        // Clear any stale key state (e.g. Escape stuck pressed from closing the
        // wizard) so the dialog doesn't immediately close on its first frame.
        ImGui::GetIO().ClearInputKeys();
        g_visible.store(true, std::memory_order_release);
    }

    if (!g_visible.load(std::memory_order_acquire)) return;

    const float vpW = ImGui::GetIO().DisplaySize.x;
    const float vpH = ImGui::GetIO().DisplaySize.y;

    // Lighten the dream scene under the dialog.
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.f, 0.f), ImVec2(vpW, vpH),
        ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 0.16f)));

    // Push the dialog font at S(20) so all dialog text renders at the correct
    // size regardless of what FontSizeBase was set to during earlier frames
    // (e.g. ProggyClean 13px added by ImGui on the very first frame).
    if (g_regularUiFont)
        ImGui::PushFont(g_regularUiFont, S(20.f));
    DrawSettingsDialog(vpW, vpH);
    if (g_regularUiFont)
        ImGui::PopFont();
}

#ifdef HAVE_WAYLAND
bool SettingsDialogVulkan_FeedKey(uint32_t evdev_key, xkb_keysym_t keysym, bool pressed,
                                  struct xkb_state* state)
{
    if (!g_visible.load(std::memory_order_acquire)) return false;

    ImGuiIO& io = ImGui::GetIO();

    ImGuiKey imkey = ImGuiKey_None;
    switch (keysym)
    {
    case XKB_KEY_BackSpace: imkey = ImGuiKey_Backspace;  break;
    case XKB_KEY_Delete:    imkey = ImGuiKey_Delete;     break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:  imkey = ImGuiKey_Enter;      break;
    case XKB_KEY_Tab:       imkey = ImGuiKey_Tab;        break;
    case XKB_KEY_Left:      imkey = ImGuiKey_LeftArrow;  break;
    case XKB_KEY_Right:     imkey = ImGuiKey_RightArrow; break;
    case XKB_KEY_Home:      imkey = ImGuiKey_Home;       break;
    case XKB_KEY_End:       imkey = ImGuiKey_End;        break;
    case XKB_KEY_Insert:    imkey = ImGuiKey_Insert;     break;
    case XKB_KEY_Escape:    imkey = ImGuiKey_Escape;     break;
    default: break;
    }

    bool ctrl  = false;
    bool shift = false;
    if (state)
    {
        ctrl  = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,  XKB_STATE_MODS_EFFECTIVE) > 0;
        shift = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
    }

    if (ctrl)
    {
        switch (keysym)
        {
        case XKB_KEY_a: imkey = ImGuiKey_A; break;
        case XKB_KEY_c: imkey = ImGuiKey_C; break;
        case XKB_KEY_v: imkey = ImGuiKey_V; break;
        case XKB_KEY_x: imkey = ImGuiKey_X; break;
        case XKB_KEY_z: imkey = ImGuiKey_Z; break;
        default: break;
        }
    }

    if (shift && keysym == XKB_KEY_Insert)
    {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_V, pressed);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        io.AddKeyEvent(ImGuiMod_Shift, false);
        return true;
    }

    if (imkey != ImGuiKey_None)
        io.AddKeyEvent(imkey, pressed);

    io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);

    if (pressed && state && !ctrl && !(shift && keysym == XKB_KEY_Insert))
    {
        const xkb_keycode_t xkb_keycode = evdev_key + 8;
        char utf8charbuf[8] = {};
        int len = xkb_state_key_get_utf8(state, xkb_keycode, utf8charbuf, sizeof utf8charbuf);
        if (len > 0 &&
            static_cast<unsigned char>(utf8charbuf[0]) >= 0x20 &&
            static_cast<unsigned char>(utf8charbuf[0]) != 0x7f)
        {
            io.AddInputCharactersUTF8(utf8charbuf);
        }
    }

    return true;
}
#endif // HAVE_WAYLAND

void SettingsDialogVulkan_FeedMousePos(int x, int y)
{
    if (!g_visible.load(std::memory_order_acquire)) return;
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
}

void SettingsDialogVulkan_FeedMouseButton(uint32_t button, bool pressed)
{
    if (!g_visible.load(std::memory_order_acquire)) return;
    int imguiBtn = -1;
    if      (button == 272) imguiBtn = 0; // BTN_LEFT
    else if (button == 273) imguiBtn = 1; // BTN_RIGHT
    else if (button == 274) imguiBtn = 2; // BTN_MIDDLE
    if (imguiBtn >= 0)
        ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);
}

#endif // !WIN32 && !MAC
