#ifdef WIN32

#include "SettingsDialogWin32.h"
#include "AboutDialogWin32.h"

#include <atomic>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <objbase.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

// ImGui 1.91+ leaves this out of imgui_impl_win32.h (#if 0) to avoid pulling Win32 types into the header.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "../DisplayOutput/D3D11/DisplayDX11.h"
#include "EDreamClient.h"
#include "PlatformUtils.h"
#include "ServerConfig.h"
#include "Settings.h"
#include "storage.h"
#include "CacheManager.h"
#include "client.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace {

#ifdef STAGE
constexpr const char* kUrlCreateAccount = "https://stage.infinidream.ai/account";
constexpr const char* kUrlHelp = "https://stage.infinidream.ai/help";
constexpr const char* kUrlWebRemote = "https://stage.infinidream.ai/rc";
constexpr const char* kUrlPlaylists = "https://stage.infinidream.ai/playlists";
#else
constexpr const char* kUrlCreateAccount = "https://alpha.infinidream.ai/account";
constexpr const char* kUrlHelp = "https://alpha.infinidream.ai/help";
constexpr const char* kUrlWebRemote = "https://alpha.infinidream.ai/rc";
constexpr const char* kUrlPlaylists = "https://alpha.infinidream.ai/playlists";
#endif

std::atomic<bool> g_overlayAllowed{true};
std::atomic<bool> g_showRequested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_wasPausedBeforeDialog{false};
std::atomic<bool> g_wasUserPausedBeforeDialog{false};
std::atomic<bool> g_pausedBySettingsDialog{false};
std::atomic<bool> g_imguiInitialized{false};
std::atomic<bool> g_pendingImGuiShutdown{false};
ImGuiContext* g_imguiContext = nullptr;
ImFont* g_boldUiFont = nullptr;

// Design-time bump on top of the monitor DPI ratio. Mirrors FirstTimeSetupWin32; tune both together.
constexpr float kBaseUiBump = 1.25f;
// Combined scale: (GetDpiForWindow / 96) × kBaseUiBump. Set in TryInitImGui() from the host
// window's DPI; used by S() at draw-time to scale every layout constant.
static float g_uiScale = kBaseUiBump;
static inline float S(float v) { return v * g_uiScale; }
ID3D11ShaderResourceView* g_srvPlaylistIcon = nullptr;
int g_texPlaylistIconW = 0;
int g_texPlaylistIconH = 0;

char g_nicknameBuf[256] = {};
char g_codeBuf[32] = {};

double g_playerFps = 23.0;
double g_displayFps = 60.0;
bool g_vsync = false;
bool g_preserveAR = false;
bool g_blackoutMonitors = true;
bool g_quietMode = true;
bool g_showAttribution = false;

char g_contentDirBuf[512] = {};
bool g_unlimitedCache = false;
int g_cacheSizeGb = 10;

bool g_useProxy = false;
char g_proxyHostBuf[512] = {};
char g_proxyLoginBuf[256] = {};
char g_proxyPasswordBuf[256] = {};
bool g_debugLog = false;
char g_serverBuf[512] = {};

bool g_keepScreensaverEnabled = true;

bool g_sentCode = false;
char g_statusBuf[256] = {};
char g_previousLoginEmailBuf[256] = {};
bool g_hasPreviousLoginEmail = false;
std::string g_versionText;

static DisplayOutput::CDisplayDX11* TryGetDx11Display()
{
    auto sp = g_Player().Display();
    if (!sp)
        return nullptr;
    return dynamic_cast<DisplayOutput::CDisplayDX11*>(sp.get());
}

static void SnapWindowTo16By9IfNeeded()
{
    if (!g_Settings() || !g_Settings()->Get("settings.player.preserve_AR", false))
        return;

    auto* dx = TryGetDx11Display();
    if (!dx)
        return;

    HWND hwnd = dx->GetWindowHandle();
    if (!hwnd || !IsWindow(hwnd) || IsZoomed(hwnd))
        return;

    const LONG style = GetWindowLong(hwnd, GWL_STYLE);
    if ((style & WS_OVERLAPPEDWINDOW) == 0)
        return;

    RECT clientRc = {};
    RECT windowRc = {};
    if (!GetClientRect(hwnd, &clientRc) || !GetWindowRect(hwnd, &windowRc))
        return;

    const LONG clientW = std::max<LONG>(1L, clientRc.right - clientRc.left);
    const LONG clientH = std::max<LONG>(1L, clientRc.bottom - clientRc.top);
    const int targetClientH = std::max(1, static_cast<int>(std::round(static_cast<double>(clientW) * 9.0 / 16.0)));

    // Already 16:9 within rounding — don't fire SetWindowPos. CDisplayDX11::WndProc posts the
    // deferred-snap message on every non-trivial WM_SIZE; this guard keeps that idempotent and
    // prevents a SetWindowPos -> WM_SIZE -> deferred-snap feedback loop.
    if (std::abs(static_cast<int>(clientH) - targetClientH) <= 1)
        return;

    RECT targetWindow = {0, 0, clientW, targetClientH};
    const LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    const BOOL hasMenu = GetMenu(hwnd) != nullptr;
    if (!AdjustWindowRectEx(&targetWindow, static_cast<DWORD>(style), hasMenu, static_cast<DWORD>(exStyle)))
        return;

    const int targetWindowW = targetWindow.right - targetWindow.left;
    const int targetWindowH = targetWindow.bottom - targetWindow.top;

    SetWindowPos(hwnd, nullptr, windowRc.left, windowRc.top, targetWindowW, targetWindowH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void OnShowPreferencesRequested()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
    AboutDialogWin32_DismissWithoutSaveForExternalOverlay();
    g_showRequested.store(true, std::memory_order_release);
}

static std::wstring Utf8ToWidePath(const std::string& utf8);

static std::string AssetBaseDir()
{
    return g_Settings()->Get("settings.app.InstallDir", PlatformUtils::GetWorkingDir());
}

static void ReleaseOverlayTextures()
{
    if (g_srvPlaylistIcon)
    {
        g_srvPlaylistIcon->Release();
        g_srvPlaylistIcon = nullptr;
    }
    g_texPlaylistIconW = 0;
    g_texPlaylistIconH = 0;
}

static HRESULT CreateSrvFromRgba(ID3D11Device* device, const uint8_t* rgba, UINT w, UINT h,
                                 ID3D11ShaderResourceView** outSrv)
{
    if (!device || !rgba || !outSrv || w == 0 || h == 0)
        return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem = rgba;
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = device->CreateTexture2D(&desc, &sub, &tex);
    if (FAILED(hr))
        return hr;
    hr = device->CreateShaderResourceView(tex, nullptr, outSrv);
    tex->Release();
    return hr;
}

static bool DecodePngToRgba(const std::wstring& path, std::vector<uint8_t>& outRgba, UINT& outW, UINT& outH)
{
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory)
        return false;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                            &decoder);
    if (FAILED(hr) || !decoder)
    {
        factory->Release();
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame)
    {
        decoder->Release();
        factory->Release();
        return false;
    }

    IWICFormatConverter* conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv)
    {
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                          WICBitmapPaletteTypeMedianCut);
    if (FAILED(hr))
    {
        conv->Release();
        frame->Release();
        decoder->Release();
        factory->Release();
        return false;
    }

    conv->GetSize(&outW, &outH);
    const UINT stride = outW * 4;
    const UINT bufSize = stride * outH;
    outRgba.resize(bufSize);
    hr = conv->CopyPixels(nullptr, stride, bufSize, outRgba.data());

    conv->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return SUCCEEDED(hr);
}

static bool TryLoadTextureFromPngUtf8(ID3D11Device* device, const std::string& pathUtf8, ID3D11ShaderResourceView** srv,
                                      int* outW, int* outH)
{
    if (!device || !srv || !outW || !outH)
        return false;
    *srv = nullptr;
    *outW = 0;
    *outH = 0;

    std::vector<uint8_t> rgba;
    UINT w = 0;
    UINT h = 0;
    if (!DecodePngToRgba(Utf8ToWidePath(pathUtf8), rgba, w, h))
        return false;

    ID3D11ShaderResourceView* loadedSrv = nullptr;
    if (FAILED(CreateSrvFromRgba(device, rgba.data(), w, h, &loadedSrv)))
        return false;

    *srv = loadedSrv;
    *outW = static_cast<int>(w);
    *outH = static_cast<int>(h);
    return true;
}

static void LoadOverlayTextures(ID3D11Device* device)
{
    ReleaseOverlayTextures();
    if (!device)
        return;

    std::string dir = AssetBaseDir();
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
        dir.push_back('\\');
    TryLoadTextureFromPngUtf8(device, dir + "play-playlist.png", &g_srvPlaylistIcon, &g_texPlaylistIconW,
                              &g_texPlaylistIconH);
}

static void ShutdownImGui()
{
    if (!g_imguiInitialized.load(std::memory_order_acquire))
        return;

    g_pendingImGuiShutdown.store(false, std::memory_order_release);
    ReleaseOverlayTextures();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imguiContext = nullptr;
    g_imguiInitialized.store(false, std::memory_order_release);
}

static bool TryInitImGui()
{
    if (g_imguiInitialized.load(std::memory_order_acquire))
        return true;

    auto* dx = TryGetDx11Display();
    if (!dx || !dx->GetWindowHandle() || !dx->GetDevice() || !dx->GetContext())
        return false;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Manifest is system-DPI-aware (electricsheep.vcxproj); GetDpiForWindow returns the system DPI.
    HWND hwnd = dx->GetWindowHandle();
    const UINT dpi = GetDpiForWindow(hwnd);
    const float dpiScale = (dpi > 0u) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
    g_uiScale = dpiScale * kBaseUiBump;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsLight();
    // Scale paddings/spacing/rounding from the default light style.
    ImGui::GetStyle().ScaleAllSizes(g_uiScale);
    g_boldUiFont = nullptr;
    {
        // Prefer San Francisco if available; fallback to close sans-serif fonts on Windows.
        const std::array<const char*, 5> preferredFonts = {
            "C:\\Windows\\Fonts\\SFPRODISPLAYREGULAR.OTF",
            "C:\\Windows\\Fonts\\SFPROTEXT-REGULAR.OTF",
            "C:\\Windows\\Fonts\\segoeui.ttf",
            "C:\\Windows\\Fonts\\Calibri.ttf",
            "C:\\Windows\\Fonts\\arial.ttf",
        };
        for (const char* fontPath : preferredFonts)
        {
            const DWORD attrs = GetFileAttributesA(fontPath);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                ImFont* loaded = io.Fonts->AddFontFromFileTTF(fontPath, S(17.0f));
                if (loaded)
                {
                    io.FontDefault = loaded;
                    break;
                }
            }
        }

        const std::array<const char*, 4> preferredBoldFonts = {
            "C:\\Windows\\Fonts\\SFPRODISPLAYBOLD.OTF",
            "C:\\Windows\\Fonts\\SFPROTEXT-BOLD.OTF",
            "C:\\Windows\\Fonts\\seguisb.ttf",
            "C:\\Windows\\Fonts\\arialbd.ttf",
        };
        for (const char* fontPath : preferredBoldFonts)
        {
            const DWORD attrs = GetFileAttributesA(fontPath);
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                g_boldUiFont = io.Fonts->AddFontFromFileTTF(fontPath, S(17.0f));
                if (g_boldUiFont)
                    break;
            }
        }
    }

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(dx->GetDevice(), dx->GetContext()))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    LoadOverlayTextures(dx->GetDevice());

    g_imguiContext = ImGui::GetCurrentContext();
    g_imguiInitialized.store(true, std::memory_order_release);
    return true;
}

static void SetStatus(const std::string& status)
{
    std::strncpy(g_statusBuf, status.c_str(), sizeof g_statusBuf - 1);
    g_statusBuf[sizeof g_statusBuf - 1] = '\0';
}

struct AuthDialogContent
{
    const char* title;
    std::string message;
};

static void ShowAuthWarningDialog(const AuthDialogContent& content)
{
    HWND owner = nullptr;
    if (auto* dx = TryGetDx11Display())
        owner = dx->GetWindowHandle();
    MessageBoxA(owner, content.message.c_str(), content.title, MB_OK | MB_ICONWARNING);
}

static AuthDialogContent BuildSendCodeFailureDialog(const EDreamClient::SendCodeResult& result)
{
    const bool isClientErrorHttp = (result.httpCode >= 400 && result.httpCode < 500);
    const bool isServerErrorHttp = (result.httpCode >= 500);

    if (isClientErrorHttp)
    {
        std::string message =
            "We couldn't send a verification email. Make sure your email address is correct, then try Send code again.";
        if (!result.message.empty())
            message += "\n\n" + result.message;
        return {"Unable to send code", message};
    }

    if (isServerErrorHttp)
    {
        std::string message = "Try again later.";
        if (!result.message.empty())
            message += " " + result.message;
        return {"Server Error", message};
    }

    return {"Authentication Error",
            result.message.empty() ? "Failed to send verification code." : result.message};
}

static AuthDialogContent BuildValidateFailureDialog(const EDreamClient::ValidateCodeResult& result)
{
    if (result.reason == EDreamClient::ValidationFailureReason::InvalidSession &&
        result.httpCode >= 400 && result.httpCode < 500)
    {
        return {"Invalid Code",
                "Check for typos and check to be sure you have the most recent code. Try again or start over"};
    }

    if (result.httpCode >= 500)
    {
        std::string message = "Try again later.";
        if (!result.message.empty())
            message += " " + result.message;
        return {"Server Error", message};
    }

    if (result.reason == EDreamClient::ValidationFailureReason::InvalidSession)
    {
        return {"Authentication Error",
                result.message.empty()
                    ? "Validation failed. Please request a new code and sign in again."
                    : result.message};
    }

    return {"Authentication Error",
            result.message.empty()
                ? "Backend is temporarily unavailable. Please try again shortly."
                : result.message};
}

#include "SettingsDialogWin32/SettingsDialogWin32.CommonTextUi.inl"

#include "SettingsDialogWin32/SettingsDialogWin32.CommonPathDialogs.inl"
#include "SettingsDialogWin32/SettingsDialogWin32.CommonSettingsState.inl"
#include "SettingsDialogWin32/SettingsDialogWin32.DialogLifecycle.inl"

static void DrawAccountTab()
{
    #include "SettingsDialogWin32/SettingsDialogWin32.TabAccount.inl"
}

static void DrawControlsTab()
{
    #include "SettingsDialogWin32/SettingsDialogWin32.TabControls.inl"
}

static void DrawDiskTab()
{
    #include "SettingsDialogWin32/SettingsDialogWin32.TabDisk.inl"
}

static void DrawDisplayTab()
{
    #include "SettingsDialogWin32/SettingsDialogWin32.TabDisplay.inl"
}

static void DrawAdvancedTab()
{
    #include "SettingsDialogWin32/SettingsDialogWin32.TabAdvanced.inl"
}

static void DrawSettingsDialog(float viewportW, float viewportH)
{
    const float targetWidth = S(541.f);   // Match macOS settings dialog content width.
    const float targetHeight = S(390.f);  // Match macOS settings dialog content height.
    const float windowWidth = (viewportW > (targetWidth + S(64.f))) ? targetWidth : (viewportW - S(32.f));
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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.98f, 0.98f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.92f, 0.92f, 0.92f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.98f, 0.98f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.95f, 0.95f, 0.95f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.93f, 0.93f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.88f, 0.88f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.84f, 0.84f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.97f, 0.97f, 0.97f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.95f, 0.95f, 0.95f, 1.00f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##SettingsDialog", nullptr, flags))
    {
        const float footerHeight = S(44.f);
        const float separatorReserve = ImGui::GetStyle().ItemSpacing.y + S(2.f);
        float contentHeight = ImGui::GetContentRegionAvail().y - footerHeight - separatorReserve;
        if (contentHeight < S(120.f))
            contentHeight = S(120.f);

        ImGui::BeginChild("settings_content_region", ImVec2(0.f, contentHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Flat tabs with a hairline below the strip and an accent underline on the active tab.
        // ImGui's default raised-button tab look reads as five buttons, not as a tab strip.
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.f, 0.f, 0.f, 0.06f));
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_TabBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.f), S(8.f)));

        // Capture the active tab's button rect so we can paint the underline + baseline below.
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
                // Tab strip uses bumped FramePadding for taller tab buttons; restore the default
                // for tab-body widgets so radios/inputs/buttons aren't oversized.
                ImGui::PopStyleVar(); // pop FramePadding push from outside the tab bar
                body();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(14.f), S(8.f)));
                ImGui::EndTabItem();
            }
        };

        if (ImGui::BeginTabBar("settings_tabs"))
        {
            drawTabItem("Account", DrawAccountTab);
            drawTabItem("Controls", DrawControlsTab);
            drawTabItem("Disk", DrawDiskTab);
            drawTabItem("Display", DrawDisplayTab);
            drawTabItem("Advanced", DrawAdvancedTab);
            ImGui::EndTabBar();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        if (activeTabFound)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 borderCol = ImGui::GetColorU32(ImGuiCol_Border);
            // macOS systemBlue (matches the wizard's primary action).
            const ImU32 accentCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.f, 0.478f, 1.f, 1.f));
            const ImVec2 wPos = ImGui::GetWindowPos();
            const float wW = ImGui::GetWindowWidth();
            const float lineY = activeTabMax.y;
            // Hairline along the entire tab strip baseline...
            dl->AddLine(ImVec2(wPos.x, lineY), ImVec2(wPos.x + wW, lineY), borderCol, 1.f);
            // ...with the active tab's underline drawn over it in accent color.
            dl->AddLine(ImVec2(activeTabMin.x, lineY), ImVec2(activeTabMax.x, lineY), accentCol, S(2.f));
        }

        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("settings_footer_region", ImVec2(0.f, 0.f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const float helpButtonSize = S(20.f);
            const float closeButtonWidth = S(78.f);
            const float closeButtonHeight = S(24.f);
            const float originX = ImGui::GetCursorPosX();
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float contentHeight = ImGui::GetContentRegionAvail().y;
            const float rowCenterY = ImGui::GetCursorPosY() + (contentHeight * 0.5f);
            const float buttonY = rowCenterY - (closeButtonHeight * 0.5f);
            const float textY = rowCenterY - (ImGui::GetTextLineHeight() * 0.5f);
            const float closeX = originX + contentWidth - closeButtonWidth;

            ImGui::SetCursorPos(ImVec2(closeX, buttonY));
            if (ImGui::Button("Close", ImVec2(closeButtonWidth, closeButtonHeight)))
                CloseDialog(true);

            ImGui::SetCursorPos(ImVec2(originX, rowCenterY - (helpButtonSize * 0.5f)));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, helpButtonSize * 0.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.f, 0.f));
            if (ImGui::Button("?", ImVec2(helpButtonSize, helpButtonSize)))
                PlatformUtils::OpenURLExternally(kUrlHelp);
            ImGui::PopStyleVar(2);

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

} // namespace

void SettingsDialogWin32_DismissWithoutSaveForExternalOverlay()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
    DismissSettingsWithoutSaveImpl();
}

void SettingsDialogWin32_HandleDeferredSnap()
{
    SnapWindowTo16By9IfNeeded();
}

void SettingsDialogWin32_Register()
{
    ESSetShowPreferencesCallback(OnShowPreferencesRequested);
}

void SettingsDialogWin32_SetOverlayAllowed(bool allow)
{
    g_overlayAllowed.store(allow, std::memory_order_release);
    if (!allow)
    {
        ApplyDialogPauseState(false);
        g_showRequested.store(false, std::memory_order_release);
        g_visible.store(false, std::memory_order_release);
        ShutdownImGui();
    }
}

bool SettingsDialogWin32_HasPendingOrVisible()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return false;
    return g_showRequested.load(std::memory_order_acquire) ||
           g_visible.load(std::memory_order_acquire);
}

bool SettingsDialogWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           LRESULT* outResult)
{
    if (!g_visible.load(std::memory_order_acquire) ||
        !g_imguiInitialized.load(std::memory_order_acquire) || g_imguiContext == nullptr)
        return false;

    ImGui::SetCurrentContext(g_imguiContext);
    const LRESULT r = ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    if (r != 0)
    {
        if (outResult)
            *outResult = r;
        return true;
    }
    return false;
}

bool SettingsDialogWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
                                        ID3D11RenderTargetView* rtv, float viewportW, float viewportH)
{
    (void)device;

    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return false;

    if (g_showRequested.load(std::memory_order_acquire))
    {
        if (TryInitImGui())
        {
            g_showRequested.store(false, std::memory_order_release);
            g_visible.store(true, std::memory_order_release);
            ResetFormForShow();
            ApplyDialogPauseState(true);
        }
    }

    if (!g_visible.load(std::memory_order_acquire) || !g_imguiInitialized.load(std::memory_order_acquire))
        return false;

    ImGui::SetCurrentContext(g_imguiContext);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2(viewportW, viewportH);
    ImGui::NewFrame();

    // Keep the dream scene visible but slightly whitened under settings.
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), ImVec2(viewportW, viewportH),
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.16f)));

    DrawSettingsDialog(viewportW, viewportH);

    ImGui::Render();

    ID3D11RenderTargetView* rtvNonConst = rtv;
    ctx->OMSetRenderTargets(1, &rtvNonConst, nullptr);
    D3D11_VIEWPORT vp = {};
    vp.Width = viewportW;
    vp.Height = viewportH;
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    ctx->RSSetViewports(1, &vp);

    if (ImGui::GetDrawData() != nullptr)
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (g_pendingImGuiShutdown.exchange(false, std::memory_order_acq_rel))
        ShutdownImGui();

    return true;
}

#endif // WIN32
