#ifdef WIN32

#include "AboutDialogWin32.h"
#include "FirstTimeSetupWin32.h"
#include "PlatformUtils.h"
#include "Player.h"
#include "Settings.h"
#include "SettingsDialogWin32.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <objbase.h>
#include <wincodec.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

#include "../DisplayOutput/D3D11/DisplayDX11.h"

namespace {

#ifdef STAGE
constexpr const char* kAppDisplayName = "infinidream stage";
#else
constexpr const char* kAppDisplayName = "infinidream";
#endif
constexpr const char* kHomepageUrl = "https://infinidream.ai";
constexpr const char* kHomepageLinkPrefix = "learn more at ";
constexpr const char* kHomepageLinkText = "infinidream.ai";
// Matches typical NSHumanReadableCopyright when not set in Info.plist; adjust if legal adds a plist string.
constexpr const char* kCopyrightLine = "Copyright \xC2\xA9 e-dream, inc.";

std::atomic<bool> g_overlayAllowed{true};
std::atomic<bool> g_showRequested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_wasPausedBeforeDialog{false};
std::atomic<bool> g_wasUserPausedBeforeDialog{false};
std::atomic<bool> g_pausedByAboutDialog{false};
std::atomic<bool> g_imguiInitialized{false};
std::atomic<bool> g_pendingImGuiShutdown{false};
ImGuiContext* g_imguiContext = nullptr;
ImFont* g_aboutBoldFont = nullptr;

// Design-time bump on top of the monitor DPI ratio. Mirrors SettingsDialogWin32; tune both together.
constexpr float kBaseUiBump = 1.25f;
// Combined scale: (GetDpiForWindow / 96) × kBaseUiBump. Set in TryInitImGui() from the host
// window's DPI; used by S() at draw-time to scale every layout constant.
static float g_uiScale = kBaseUiBump;
static inline float S(float v) { return v * g_uiScale; }

ID3D11ShaderResourceView* g_srvAboutLogo = nullptr;
int g_aboutLogoW = 0;
int g_aboutLogoH = 0;

static DisplayOutput::CDisplayDX11* TryGetDx11Display()
{
    auto sp = g_Player().Display();
    if (!sp)
        return nullptr;
    return dynamic_cast<DisplayOutput::CDisplayDX11*>(sp.get());
}

static std::wstring Utf8ToWidePath(const std::string& utf8)
{
    if (utf8.empty())
        return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0)
        return L"";
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, w.data(), n);
    if (!w.empty() && w.back() == L'\0')
        w.pop_back();
    return w;
}

static std::string AssetBaseDir()
{
    return g_Settings()->Get("settings.app.InstallDir", PlatformUtils::GetWorkingDir());
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

static void ReleaseAboutLogo()
{
    if (g_srvAboutLogo)
    {
        g_srvAboutLogo->Release();
        g_srvAboutLogo = nullptr;
    }
    g_aboutLogoW = 0;
    g_aboutLogoH = 0;
}

static void LoadAboutLogo(ID3D11Device* device)
{
    ReleaseAboutLogo();
    if (!device)
        return;

    std::string dir = AssetBaseDir();
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
        dir.push_back('\\');
    TryLoadTextureFromPngUtf8(device, dir + "logo.png", &g_srvAboutLogo, &g_aboutLogoW, &g_aboutLogoH);
}

static std::string AboutPanelVersionString()
{
    return PlatformUtils::GetAppVersion();
}

static void ApplyAboutPauseState(bool visible)
{
    if (visible)
    {
        const bool wasPaused = g_Player().IsPaused();
        const bool wasUserPaused = g_Player().IsUserPaused();
        g_wasPausedBeforeDialog.store(wasPaused, std::memory_order_release);
        g_wasUserPausedBeforeDialog.store(wasUserPaused, std::memory_order_release);
        g_Player().SetPaused(true, /*isUserInitiated=*/true);
        g_pausedByAboutDialog.store(true, std::memory_order_release);
        return;
    }

    if (g_pausedByAboutDialog.exchange(false, std::memory_order_acq_rel))
    {
        const bool restorePaused = g_wasPausedBeforeDialog.load(std::memory_order_acquire);
        const bool restoreUserPaused = g_wasUserPausedBeforeDialog.load(std::memory_order_acquire);
        if (restorePaused && !restoreUserPaused)
        {
            // Was paused only by buffering (not by user). If buffering has already completed while
            // the dialog was open, the buffering-complete event won't fire again, so just unpause.
            // If buffering is still active, restore the system-only pause so buffering-complete can
            // resume normally (two-step needed because dialog set m_UserPaused=true).
            if (g_Player().IsPausedForBuffering())
            {
                g_Player().SetPaused(false, false);
                g_Player().SetPaused(true, false);
            }
            else
            {
                g_Player().SetPaused(false, false);
            }
        }
        else
        {
            g_Player().SetPaused(restorePaused, restoreUserPaused);
        }
    }
}

static void ShutdownImGui()
{
    if (!g_imguiInitialized.load(std::memory_order_acquire))
        return;

    g_pendingImGuiShutdown.store(false, std::memory_order_release);
    ReleaseAboutLogo();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_imguiContext = nullptr;
    g_aboutBoldFont = nullptr;
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
    g_aboutBoldFont = nullptr;

    {
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
                ImFont* loaded = io.Fonts->AddFontFromFileTTF(fontPath, S(13.0f));
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
                g_aboutBoldFont = io.Fonts->AddFontFromFileTTF(fontPath, S(18.0f));
                if (g_aboutBoldFont)
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

    LoadAboutLogo(dx->GetDevice());

    g_imguiContext = ImGui::GetCurrentContext();
    g_imguiInitialized.store(true, std::memory_order_release);
    return true;
}

static void CloseAboutDialog()
{
    ApplyAboutPauseState(false);
    g_visible.store(false, std::memory_order_release);
    g_pendingImGuiShutdown.store(true, std::memory_order_release);
}

static float WindowInnerWidth()
{
    return ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
}

static void CenteredTextUnformatted(const char* text)
{
    if (!text || !*text)
        return;
    const float inner = WindowInnerWidth();
    const float tw = ImGui::CalcTextSize(text).x;
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMin().x + std::max(0.f, (inner - tw) * 0.5f));
    ImGui::TextUnformatted(text);
}

static void CenteredMutedTextUnformatted(const char* text)
{
    if (!text || !*text)
        return;
    const float inner = WindowInnerWidth();
    const float tw = ImGui::CalcTextSize(text).x;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMin().x + std::max(0.f, (inner - tw) * 0.5f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

static void CenteredPrefixedLink(const char* prefix, const char* linkText, const char* url)
{
    if (!linkText || !*linkText || !url || !*url)
        return;
    const float prefixW = (prefix && *prefix) ? ImGui::CalcTextSize(prefix).x : 0.f;
    const float linkW = ImGui::CalcTextSize(linkText).x;
    const float totalW = prefixW + linkW;
    const float inner = WindowInnerWidth();
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMin().x + std::max(0.f, (inner - totalW) * 0.5f));

    if (prefix && *prefix)
    {
        ImGui::TextUnformatted(prefix);
        ImGui::SameLine(0.f, 0.f);
    }

    const ImVec4 linkColor(0.10f, 0.40f, 0.85f, 1.00f);
    ImGui::PushStyleColor(ImGuiCol_Text, linkColor);
    ImGui::TextUnformatted(linkText);
    ImGui::PopStyleColor();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(itemMin.x, itemMax.y), ImVec2(itemMax.x, itemMax.y),
                                        ImGui::GetColorU32(linkColor), 1.0f);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsItemClicked())
        PlatformUtils::OpenURLExternally(url);
}

static void DrawAboutDialog(float viewportW, float viewportH)
{
    // NSApplication orderFrontStandardAboutPanel: compact centered sheet (icon, name, version, copyright).
    const float kTargetW = S(360.f);
    const float kTargetH = S(300.f);
    const float kLogoDisplay = S(96.f);

    const ImVec2 windowSize((viewportW > kTargetW + S(32.f)) ? kTargetW : (viewportW - S(32.f)),
                            (viewportH > kTargetH + S(32.f)) ? kTargetH : (viewportH - S(32.f)));
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((viewportW - windowSize.x) * 0.5f, (viewportH - windowSize.y) * 0.5f),
                            ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, S(12.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(20.f), S(20.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(8.f), S(6.f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.98f, 0.98f, 0.98f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.40f, 0.40f, 0.40f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.93f, 0.93f, 0.93f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.88f, 0.88f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.84f, 0.84f, 0.84f, 1.00f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("##AboutInfinidream", nullptr, flags))
    {
        if (g_srvAboutLogo && g_aboutLogoW > 0 && g_aboutLogoH > 0)
        {
            const float aspect = static_cast<float>(g_aboutLogoH) / static_cast<float>(g_aboutLogoW);
            const float dispW = kLogoDisplay;
            const float dispH = kLogoDisplay * aspect;
            const float inner = WindowInnerWidth();
            ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMin().x + std::max(0.f, (inner - dispW) * 0.5f));
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_srvAboutLogo)),
                         ImVec2(dispW, dispH));
            ImGui::Spacing();
        }

        if (g_aboutBoldFont)
            ImGui::PushFont(g_aboutBoldFont);
        CenteredTextUnformatted(kAppDisplayName);
        if (g_aboutBoldFont)
            ImGui::PopFont();

        ImGui::Spacing();
        const std::string verCore = AboutPanelVersionString();
        if (!verCore.empty())
        {
            const std::string verLine = std::string("Version ") + verCore;
            CenteredTextUnformatted(verLine.c_str());
        }

        ImGui::Spacing();
        CenteredPrefixedLink(kHomepageLinkPrefix, kHomepageLinkText, kHomepageUrl);

        const float closeW = S(88.f);
        const float closeH = S(26.f);
        const float copyrightH = ImGui::GetTextLineHeight();
        const float bottomGap = ImGui::GetStyle().ItemSpacing.y;
        const float bottomReserve = copyrightH + bottomGap + closeH + S(10.f);
        const float availY = ImGui::GetContentRegionAvail().y;
        if (availY > bottomReserve)
            ImGui::Dummy(ImVec2(0.f, availY - bottomReserve));
        CenteredMutedTextUnformatted(kCopyrightLine);
        const float inner = WindowInnerWidth();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMin().x + std::max(0.f, (inner - closeW) * 0.5f));
        if (ImGui::Button("OK", ImVec2(closeW, closeH)))
            CloseAboutDialog();
    }
    ImGui::End();
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(4);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        CloseAboutDialog();
}

} // namespace

void AboutDialogWin32_SetOverlayAllowed(bool allow)
{
    g_overlayAllowed.store(allow, std::memory_order_release);
    if (!allow)
    {
        ApplyAboutPauseState(false);
        g_showRequested.store(false, std::memory_order_release);
        g_visible.store(false, std::memory_order_release);
        ShutdownImGui();
    }
}

bool AboutDialogWin32_HasPendingOrVisible()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return false;
    return g_showRequested.load(std::memory_order_acquire) || g_visible.load(std::memory_order_acquire);
}

void AboutDialogWin32_RequestShow()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
    if (FirstTimeSetupWin32_IsWizardVisible())
        return;

    SettingsDialogWin32_DismissWithoutSaveForExternalOverlay();
    g_showRequested.store(true, std::memory_order_release);
}

void AboutDialogWin32_DismissWithoutSaveForExternalOverlay()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
    g_showRequested.store(false, std::memory_order_release);
    if (g_visible.load(std::memory_order_acquire))
    {
        ApplyAboutPauseState(false);
        g_visible.store(false, std::memory_order_release);
    }
    if (g_imguiInitialized.load(std::memory_order_acquire))
        ShutdownImGui();
}

bool AboutDialogWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        LRESULT* outResult)
{
    if (!g_visible.load(std::memory_order_acquire) || !g_imguiInitialized.load(std::memory_order_acquire) ||
        g_imguiContext == nullptr)
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

bool AboutDialogWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
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
            ApplyAboutPauseState(true);
        }
    }

    if (!g_visible.load(std::memory_order_acquire) || !g_imguiInitialized.load(std::memory_order_acquire))
        return false;

    ImGui::SetCurrentContext(g_imguiContext);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2(viewportW, viewportH);
    ImGui::NewFrame();

    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0.0f, 0.0f), ImVec2(viewportW, viewportH),
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.16f)));

    DrawAboutDialog(viewportW, viewportH);

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
