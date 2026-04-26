#ifdef WIN32

#include "ServerConfig.h"
#include "FirstTimeSetupWin32.h"

#include "PlatformUtils.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <objbase.h>
#include <wincodec.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

// ImGui 1.91+ leaves this out of imgui_impl_win32.h (#if 0) to avoid pulling Win32 types into the header.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "../DisplayOutput/D3D11/DisplayDX11.h"
#include "EDreamClient.h"
#include "Player.h"
#include "Settings.h"
#include "storage.h"

typedef void (*ShowFirstTimeSetupCallback_t)();
extern void ESSetShowFirstTimeSetupCallback(ShowFirstTimeSetupCallback_t);

namespace {

#ifdef STAGE
constexpr const char* kUrlCreateAccount = "https://stage.infinidream.ai/account";
constexpr const char* kUrlWebRemote = "https://stage.infinidream.ai/rc";
constexpr const char* kUrlPlaylists = "https://stage.infinidream.ai/playlists";
#else
constexpr const char* kUrlCreateAccount = "https://alpha.infinidream.ai/account";
constexpr const char* kUrlWebRemote = "https://alpha.infinidream.ai/rc";
constexpr const char* kUrlPlaylists = "https://alpha.infinidream.ai/playlists";
#endif

constexpr float kDesignWinW = 879.f;
// Trimmed from the macOS XIB's 686 — the original window had a lot of empty space below
// content. 580 leaves room for header (139) + thanks panel (~329) + skip footer (54) + padding.
constexpr float kDesignWinH = 580.f;
constexpr float kLogoDisplay = 120.f;
// First startup/*.xib content widths (step panels centered in the window body).
constexpr float kEmailStepPanelW = 571.f;
constexpr float kEmailStepPanelH = 268.f;
constexpr float kCodeStepPanelW = 509.f;
// Trimmed from the macOS XIB's 291 to keep the Try-again row above the body clip
// when the host window is shorter than the design height (e.g. 720p client area at >=125% DPI).
constexpr float kCodeStepPanelH = 250.f;
// EmailStepViewController.xib (Cocoa y-from-bottom converted to top-down).
constexpr float kEmailPromptL = 45.f;
constexpr float kEmailMarginL = 47.f;
constexpr float kEmailFieldW = 364.f;
constexpr float kEmailSendBtnW = 106.f;
constexpr float kEmailCreateAccountW = 235.f;
constexpr float kEmailPromptOffsetY = 30.f;
constexpr float kEmailRowOffsetY = 115.f;
constexpr float kEmailErrorOffsetY = 151.f;
constexpr float kEmailCreateBtnOffsetY = 172.f;
// CodeStepViewController.xib — Y offsets shifted up from the macOS XIB
// (originals: 30/112/166/184/227) so the panel fits when the host window is short.
constexpr float kCodeInstrOffsetY = 16.f;
constexpr float kCodeOtpW = 158.f;
constexpr float kCodeOtpH = 48.f;
constexpr float kCodeOtpOffsetY = 90.f;
constexpr float kCodeErrorOffsetY = 144.f;
constexpr float kCodeVerifyW = 110.f;
constexpr float kCodeVerifyOffsetY = 158.f;
constexpr float kCodeTryAgainW = 88.f;
constexpr float kCodeTryAgainOffsetY = 200.f;
// ThanksStepViewController.xib (tips / “All set” step)
// Row heights and paddings were reduced from the original macOS XIB (190/207/14/20/20)
// so the thanks panel fits inside the wizard body without clipping the bottom row,
// even at 150% Windows display scaling on a ~720p host window.
constexpr float kThanksPanelW = 667.f;
constexpr float kThanksOuterPad = 20.f;
constexpr float kThanksColGap = 20.f;
constexpr float kThanksTitleTop = 2.f;
constexpr float kThanksTitleToGrid = 8.f;
constexpr float kThanksRowTopH = 110.f;
constexpr float kThanksRowBotH = 92.f;
constexpr float kThanksHSepGap = 5.f;
/// Match vertical splitter thickness (1px hairline).
constexpr float kThanksHSepH = 1.f;
constexpr float kThanksCellPad = 6.f;
constexpr float kThanksImgTop = 10.f;
constexpr float kThanksImgTrail = 6.f;
constexpr float kThanksTextImgGap = 12.f;
constexpr float kThanksPlaylistImg = 52.f;
constexpr float kThanksBtnBottom = 6.f;
constexpr float kThanksBottomPad = 4.f;
constexpr float kThanksOpenRemoteBtnW = 118.f;
constexpr float kThanksOpenPlaylistBtnW = 136.f;
constexpr float kThanksTipsBtnH = 28.f;
constexpr float kThanksDreamOnMinW = 82.f;
constexpr float kThanksCopyFontSize = 17.f;
constexpr float kThanksBtnFontSize = 15.f;

// macOS systemBlue focus ring; idle outline matches sheet borderChrome
static constexpr ImVec4 kMacInputBorderFocus(0.f, 0.478f, 1.f, 1.f);
static constexpr ImVec4 kMacInputBorderIdle(0.74f, 0.74f, 0.76f, 1.f);
// NSButton “default” / accent (key-style primary action)
static constexpr ImVec4 kMacAccentBtn(0.f, 0.478f, 1.f, 1.f);
static constexpr ImVec4 kMacAccentBtnHov(0.10f, 0.54f, 1.f, 1.f);
static constexpr ImVec4 kMacAccentBtnAct(0.f, 0.40f, 0.88f, 1.f);
static constexpr ImVec4 kMacAccentBtnBorder(0.f, 0.38f, 0.82f, 1.f);

// Design-time bump on top of the monitor DPI ratio. The macOS XIB design is sized for
// a roomy welcome window (879×686 logical), so 1.0× already feels comfortable here —
// any extra bump pushes the dialog past a typical app window's height.
constexpr float kBaseUiBump = 1.0f;
// Combined scale: (GetDpiForWindow / 96) × kBaseUiBump. Set in TryInitImGui() from the host
// window's DPI; DrawWizard() may further reduce g_uiScale each frame so the dialog fits a
// short host viewport (with a matching FontGlobalScale so text shrinks proportionally).
static float g_baseScale = kBaseUiBump;
static float g_uiScale = kBaseUiBump;
static inline float S(float v) { return v * g_uiScale; }

/// Vertically center a fixed-height panel in the body region below the header.
static float CenteredPanelTopPad(float bodyAvailH, float panelH)
{
    if (bodyAvailH <= 0.f || panelH <= 0.f)
        return 0.f;
    const float pad = (bodyAvailH - panelH) * 0.5f;
    return pad > 0.f ? pad : 0.f;
}

// Updated after each InputText; cleared when leaving email/code steps (public ImGui has no GetActiveID).
static bool g_emailFieldBorderActive = false;
static bool g_otpFieldBorderActive = false;

std::atomic<bool> g_overlayAllowed{true};
std::atomic<bool> g_showRequested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_imguiInitialized{false};
/// Set when the user dismisses the wizard; context must not be destroyed inside NewFrame/Begin/End.
std::atomic<bool> g_pendingImGuiShutdown{false};
/// Mirror settings dialog: pause playback while the sheet is up, restore after close (avoids stuck buffering pause + no key focus).
std::atomic<bool> g_wasPausedBeforeWizard{false};
std::atomic<bool> g_pausedByWizard{false};
ImGuiContext* g_imguiContext = nullptr;

ImFont* g_fontBody = nullptr;
ImFont* g_fontTitle = nullptr;
ImFont* g_fontHeadline = nullptr;
ImFont* g_fontOtp = nullptr;
ImFont* g_fontThanksCopy = nullptr;
ImFont* g_fontThanksBtn = nullptr;

ID3D11ShaderResourceView* g_srvLogo = nullptr;
ID3D11ShaderResourceView* g_srvPlaylist = nullptr;
int g_texLogoW = 0;
int g_texLogoH = 0;
int g_texPlaylistW = 0;
int g_texPlaylistH = 0;

std::mutex g_jobMutex;
std::atomic<bool> g_sendBusy{false};
std::atomic<bool> g_sendDone{false};
bool g_sendOk = false;
EDreamClient::SendCodeResult g_sendResult{false, 0, std::string()};

std::atomic<bool> g_validateBusy{false};
std::atomic<bool> g_validateDone{false};
bool g_validateOk = false;
EDreamClient::ValidateCodeResult g_validateResult{
    false, EDreamClient::ValidationFailureReason::None, 0, std::string()};

char g_emailBuf[256] = {};
char g_codeBuf[16] = {};
char g_errBuf[512] = {};
int g_wizardStep = 0;

struct AuthDialogContent
{
    const char* title;
    std::string message;
};

static DisplayOutput::CDisplayDX11* TryGetDx11Display();

static void OnFirstTimeSetupRequested()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
    g_showRequested.store(true, std::memory_order_release);
    // Hold timeline before ImGui init completes (avoids a few frames of playback under the wizard).
    g_Player().SetFirstRunWizardPlaybackHold(true);
}

static bool IsLikelyEmail(const char* s)
{
    if (!s || !*s)
        return false;
    const char* at = std::strchr(s, '@');
    if (!at || at == s)
        return false;
    const char* dot = std::strchr(at + 1, '.');
    return dot != nullptr && dot > at + 1 && std::strlen(dot) > 1;
}

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
    const bool isClientErrorHttp = (result.httpCode >= 400 && result.httpCode < 500);
    const bool isServerErrorHttp = (result.httpCode >= 500);

    if (isClientErrorHttp)
    {
        return {"Invalid Code",
                "Check for typos and check to be sure you have the most recent code. Try again or start over"};
    }

    if (isServerErrorHttp)
    {
        std::string message = "Try again later.";
        if (!result.message.empty())
            message += " " + result.message;
        return {"Server Error", message};
    }

    return {"Authentication Error",
            result.message.empty()
                ? "Backend is temporarily unavailable. Please try again shortly."
                : result.message};
}

static int FilterDigitsOnly(ImGuiInputTextCallbackData* data)
{
    const auto isDigit = [](ImWchar c) { return c >= '0' && c <= '9'; };
    const int kMaxDigits = 6;

    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    {
        if (!isDigit(data->EventChar))
            return 1; // discard non-digits

        // Prevent typing beyond 6 digits.
        if (data->BufTextLen >= kMaxDigits)
            return 1;

        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        // Robust: clamp after any edit (paste/selection/etc).
        int w = 0;
        for (int r = 0; r < data->BufTextLen && data->Buf[r] != '\0'; ++r)
        {
            const char c = data->Buf[r];
            if (c >= '0' && c <= '9')
            {
                data->Buf[w++] = c;
                if (w >= kMaxDigits)
                    break;
            }
        }
        data->Buf[w] = '\0';
        data->BufTextLen = w;
        data->BufDirty = true;
        return 0;
    }

    return 0;
}

static void StripNonDigits(char* buf, size_t bufSize)
{
    size_t w = 0;
    for (size_t r = 0; buf[r] != '\0' && w + 1 < bufSize; ++r)
    {
        if (buf[r] >= '0' && buf[r] <= '9')
            buf[w++] = buf[r];
    }
    buf[w] = '\0';
    if (w > 6)
        buf[6] = '\0';
}

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

static void ReleaseOverlayTextures()
{
    if (g_srvLogo)
    {
        g_srvLogo->Release();
        g_srvLogo = nullptr;
    }
    if (g_srvPlaylist)
    {
        g_srvPlaylist->Release();
        g_srvPlaylist = nullptr;
    }
    g_texLogoW = g_texLogoH = g_texPlaylistW = g_texPlaylistH = 0;
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
    *outW = *outH = 0;
    std::vector<uint8_t> rgba;
    UINT w = 0, h = 0;
    if (!DecodePngToRgba(Utf8ToWidePath(pathUtf8), rgba, w, h))
        return false;
    ID3D11ShaderResourceView* s = nullptr;
    if (FAILED(CreateSrvFromRgba(device, rgba.data(), w, h, &s)))
        return false;
    *srv = s;
    *outW = static_cast<int>(w);
    *outH = static_cast<int>(h);
    return true;
}

static void ApplyLightSheetStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsLight(&s);
    s.WindowRounding = 8.f;
    s.ChildRounding = 6.f;
    // NSButton rounded bezel ~5pt corner radius on recent macOS.
    s.FrameRounding = 5.f;
    s.PopupRounding = 6.f;
    s.ScrollbarRounding = 8.f;
    s.GrabRounding = 4.f;
    s.WindowBorderSize = 1.f;
    // Buttons and framed inputs use RenderFrame(..., borders=true); 1px edge matches macOS hairline.
    s.FrameBorderSize = 1.f;
    s.WindowPadding = ImVec2(14.f, 14.f);
    s.ItemSpacing = ImVec2(10.f, 8.f);
    s.CellPadding = ImVec2(6.f, 4.f);
    // ~NSControlSizeLarge padding feel; email/OTP still override locally.
    s.FramePadding = ImVec2(10.f, 8.f);

    ImVec4 text(0.10f, 0.11f, 0.13f, 1.f);
    ImVec4 winBg(0.965f, 0.967f, 0.975f, 0.985f);
    // Push button: white face, slight gray press; border = opaque separator gray (see ImGui::RenderFrame).
    ImVec4 btn(1.f, 1.f, 1.f, 1.f);
    ImVec4 btnHov(0.96f, 0.96f, 0.98f, 1.f);
    ImVec4 btnAct(0.90f, 0.90f, 0.93f, 1.f);
    // Unfocused controls: ~gridColor / systemGray in light mode (opaque so button outline reads clearly).
    ImVec4 borderChrome(0.74f, 0.74f, 0.76f, 1.f);

    s.Colors[ImGuiCol_Text] = text;
    s.Colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.47f, 0.50f, 1.f);
    s.Colors[ImGuiCol_WindowBg] = winBg;
    // Keep child panel backgrounds consistent with the main sheet like macOS.
    s.Colors[ImGuiCol_ChildBg] = winBg;
    s.Colors[ImGuiCol_Border] = borderChrome;
    s.Colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
    s.Colors[ImGuiCol_FrameBg] = ImVec4(1.f, 1.f, 1.f, 1.f);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.98f, 0.98f, 0.99f, 1.f);
    s.Colors[ImGuiCol_FrameBgActive] = ImVec4(1.f, 1.f, 1.f, 1.f);
    s.Colors[ImGuiCol_Button] = btn;
    s.Colors[ImGuiCol_ButtonHovered] = btnHov;
    s.Colors[ImGuiCol_ButtonActive] = btnAct;
    s.Colors[ImGuiCol_Header] = ImVec4(0.86f, 0.89f, 0.97f, 1.f);
    s.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.93f, 0.94f, 0.96f, 0.85f);
    // Keyboard/gamepad focus ring — keep visible; mouse-active fields use Border push below.
    s.Colors[ImGuiCol_NavHighlight] = ImVec4(0.f, 0.478f, 1.f, 0.55f);
}

static void LoadSystemFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    g_fontBody = nullptr;
    g_fontTitle = nullptr;
    g_fontHeadline = nullptr;
    g_fontOtp = nullptr;
    g_fontThanksCopy = nullptr;
    g_fontThanksBtn = nullptr;

    char winDir[MAX_PATH] = {};
    if (GetWindowsDirectoryA(winDir, MAX_PATH) == 0)
        return;

    std::string base(winDir);
    if (!base.empty() && base.back() != '\\' && base.back() != '/')
        base += '\\';
    for (char& c : base)
        if (c == '/')
            c = '\\';

    const std::string segoe = base + "Fonts\\segoeui.ttf";
    const std::string segSb = base + "Fonts\\seguisb.ttf";

    FILE* f = std::fopen(segoe.c_str(), "rb");
    if (!f)
        return;
    std::fclose(f);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;

    g_fontBody = io.Fonts->AddFontFromFileTTF(segoe.c_str(), S(15.f), &cfg, io.Fonts->GetGlyphRangesDefault());
    g_fontTitle = io.Fonts->AddFontFromFileTTF(segoe.c_str(), S(28.f), &cfg, io.Fonts->GetGlyphRangesDefault());
    g_fontOtp = io.Fonts->AddFontFromFileTTF(segoe.c_str(), S(36.f), &cfg, io.Fonts->GetGlyphRangesDefault());
    g_fontThanksCopy = io.Fonts->AddFontFromFileTTF(segoe.c_str(), S(kThanksCopyFontSize), &cfg,
                                                    io.Fonts->GetGlyphRangesDefault());

    FILE* fsb = std::fopen(segSb.c_str(), "rb");
    if (fsb)
    {
        std::fclose(fsb);
        g_fontHeadline = io.Fonts->AddFontFromFileTTF(segSb.c_str(), S(32.f), &cfg, io.Fonts->GetGlyphRangesDefault());
        g_fontThanksBtn = io.Fonts->AddFontFromFileTTF(segSb.c_str(), S(kThanksBtnFontSize), &cfg,
                                                       io.Fonts->GetGlyphRangesDefault());
    }
    if (!g_fontHeadline)
        g_fontHeadline = io.Fonts->AddFontFromFileTTF(segoe.c_str(), S(32.f), &cfg, io.Fonts->GetGlyphRangesDefault());
    if (!g_fontThanksBtn)
        g_fontThanksBtn = io.Fonts->AddFontFromFileTTF(segoe.c_str(), S(kThanksBtnFontSize), &cfg,
                                                       io.Fonts->GetGlyphRangesDefault());

    if (g_fontBody)
        io.FontDefault = g_fontBody;
}

static void LoadOverlayTextures(ID3D11Device* device)
{
    ReleaseOverlayTextures();
    if (!device)
        return;

    const std::string dir = AssetBaseDir();
    TryLoadTextureFromPngUtf8(device, dir + "logo.png", &g_srvLogo, &g_texLogoW, &g_texLogoH);
    TryLoadTextureFromPngUtf8(device, dir + "play-playlist.png", &g_srvPlaylist, &g_texPlaylistW,
                             &g_texPlaylistH);
}

static void ApplyFirstTimeWizardPauseState(bool visible)
{
    if (visible)
    {
        g_Player().SetFirstRunWizardPlaybackHold(true);
        const bool wasPaused = g_Player().IsPaused();
        g_wasPausedBeforeWizard.store(wasPaused, std::memory_order_release);
        if (!wasPaused)
        {
            // User-initiated pause: survives internal SetPaused(true) from buffering (Player.h).
            g_Player().SetPaused(true, true);
            g_pausedByWizard.store(true, std::memory_order_release);
        }
        else
            g_pausedByWizard.store(false, std::memory_order_release);
        return;
    }
    g_Player().SetFirstRunWizardPlaybackHold(false);
    if (g_pausedByWizard.exchange(false, std::memory_order_acq_rel))
        g_Player().SetPaused(g_wasPausedBeforeWizard.load(std::memory_order_acquire), false);
    // Drop stale auto-pause from stream bootstrap behind the overlay; main loop will re-pause if still buffering.
    g_Player().SetPausedForBuffering(false);
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
    g_fontBody = g_fontTitle = g_fontHeadline = g_fontOtp = nullptr;
    g_fontThanksCopy = g_fontThanksBtn = nullptr;
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

    // Manifest is system-DPI-aware (electricsheep.vcxproj), so this returns the system DPI;
    // dialog opens at the same scale on every monitor for the life of the process.
    HWND hwnd = dx->GetWindowHandle();
    const UINT dpi = GetDpiForWindow(hwnd);
    const float dpiScale = (dpi > 0u) ? (static_cast<float>(dpi) / 96.0f) : 1.0f;
    g_uiScale = dpiScale * kBaseUiBump;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ApplyLightSheetStyle();
    // Scale paddings/spacing/rounding from the design values set in ApplyLightSheetStyle().
    ImGui::GetStyle().ScaleAllSizes(g_uiScale);
    LoadSystemFonts();

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

static void ResetWizardForShow()
{
    g_wizardStep = 0;
    g_emailFieldBorderActive = false;
    g_otpFieldBorderActive = false;
    g_codeBuf[0] = '\0';
    g_errBuf[0] = '\0';
    std::string existing = g_Settings()->Get("settings.generator.nickname", std::string());
    std::strncpy(g_emailBuf, existing.c_str(), sizeof g_emailBuf - 1);
    g_emailBuf[sizeof g_emailBuf - 1] = '\0';
}

static void PollWorkerResults()
{
    if (g_sendDone.exchange(false))
    {
        std::lock_guard<std::mutex> lock(g_jobMutex);
        if (g_sendOk)
        {
            g_wizardStep = 1;
            g_errBuf[0] = '\0';
        }
        else
        {
            const AuthDialogContent dialog = BuildSendCodeFailureDialog(g_sendResult);
            std::strncpy(g_errBuf, dialog.message.c_str(), sizeof g_errBuf - 1);
            g_errBuf[sizeof g_errBuf - 1] = '\0';
            ShowAuthWarningDialog(dialog);
        }
    }

    if (g_validateDone.exchange(false))
    {
        if (g_validateOk)
        {
            g_Settings()->Set("settings.app.firsttimesetup", true);
            g_Settings()->Storage()->Commit();
            EDreamClient::DidSignIn();
            g_wizardStep = 2;
            g_errBuf[0] = '\0';
        }
        else
        {
            const AuthDialogContent dialog = BuildValidateFailureDialog(g_validateResult);
            std::strncpy(g_errBuf, dialog.message.c_str(), sizeof g_errBuf - 1);
            g_errBuf[sizeof g_errBuf - 1] = '\0';
            g_codeBuf[0] = '\0';
            ShowAuthWarningDialog(dialog);
        }
    }
}

static void DrawSkipFooter()
{
    if (g_wizardStep >= 2)
        return;

    // Pin the footer to the bottom-right inside the dialog sheet.
    const float skipW = S(80.f);

    const float padY = ImGui::GetStyle().WindowPadding.y;
    const float skipH = S(36.f); // matches other large controls on the sheet
    float y = ImGui::GetWindowHeight() - padY - skipH;
    if (y < 0.f)
        y = ImGui::GetCursorPosY();

    const float x = ImGui::GetCursorStartPos().x + ImGui::GetContentRegionAvail().x - skipW;
    ImGui::SetCursorPos(ImVec2(x, y));

    if (ImGui::Button("Skip", ImVec2(skipW, skipH)))
    {
        g_visible.store(false, std::memory_order_release);
        g_pendingImGuiShutdown.store(true, std::memory_order_release);
    }
}

static void DrawWizard()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    const ImVec2 dialogPadding = ImGui::GetStyle().WindowPadding;

    // ImGui clamps *root* windows to the viewport, so a single fixed-size root window would shrink
    // with the app. Use a fullscreen host + sized child instead. Clamp the child to the viewport so
    // the bottom (Skip button) stays visible even when the host window is shorter than the design.
    const ImVec2 panelSize((std::min)(S(kDesignWinW), display.x),
                           (std::min)(S(kDesignWinH), display.y));
    float panelX = (display.x - panelSize.x) * 0.5f;
    float panelY = (display.y - panelSize.y) * 0.5f;
    panelX = (std::max)(0.f, panelX);
    panelY = (std::max)(0.f, panelY);

    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImVec4 sheetBg = ImGui::GetStyle().Colors[ImGuiCol_ChildBg];
    // Main overlay host should be transparent; the dialog sheet is drawn by the Welcome child below.
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
    ImGui::Begin("##FirstTimeOverlayHost", nullptr, hostFlags);

    ImGui::SetCursorPos(ImVec2(panelX, panelY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dialogPadding);
    // Make the sheet background consistent inside the dialog (fix header/body seam).
    ImGui::PushStyleColor(ImGuiCol_WindowBg, sheetBg);
    ImGui::BeginChild("Welcome to Infinidream", panelSize, true, ImGuiWindowFlags_NoScrollbar);

    // --- Header — StartupWindowController.xib: logo leading 10 top 11; title centerX at y=50 (not inline with logo)
    {
        const float logoSz = S(kLogoDisplay);
        const float hdrH = logoSz + S(11.f) + S(8.f); // logo + top inset + gap before body (xib)
        const float winInnerW = ImGui::GetWindowWidth();
        if (g_srvLogo && g_texLogoW > 0 && g_texLogoH > 0)
        {
            ImGui::SetCursorPos(ImVec2(S(10.f), S(11.f)));
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_srvLogo)),
                         ImVec2(logoSz, logoSz));
        }

        if (g_fontHeadline)
            ImGui::PushFont(g_fontHeadline);
        const char* headline = "Welcome to Infinidream";
        const ImVec2 ts = ImGui::CalcTextSize(headline);
        ImGui::SetCursorPos(ImVec2((winInnerW - ts.x) * 0.5f, S(50.f)));
        ImGui::TextUnformatted(headline);
        if (g_fontHeadline)
            ImGui::PopFont();

        // Continue layout right below the header area (avoid an extra child boundary).
        ImGui::SetCursorPos(ImVec2(0.f, hdrH));
    }

    // Body fills remaining sheet height above the Skip row; no extra background (same as header/dialog).
    // DrawSkipFooter early-returns on the thanks step, so reclaim that vertical reserve there —
    // matters at 150% display scaling where every design pixel counts to fit the tips grid.
    const ImGuiStyle& sheetSt = ImGui::GetStyle();
    const float skipReserve = (g_wizardStep < 2)
                                  ? (sheetSt.WindowPadding.y + S(36.f) + S(4.f))
                                  : (sheetSt.WindowPadding.y + S(8.f));
    float bodyH = ImGui::GetContentRegionAvail().y - skipReserve;
    if (bodyH < 1.f)
        bodyH = 1.f;
    // Suppress the body scrollbar on every step. Each step's panel is sized to fit the body;
    // if a tiny host viewport ever clips content, fall through silently rather than show a scrollbar.
    ImGuiWindowFlags bodyChildFlags =
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("body", ImVec2(0, bodyH), ImGuiChildFlags_None, bodyChildFlags);

    if (g_wizardStep == 0)
    {
        g_otpFieldBorderActive = false;
        const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
        // Top-align with a small gap; centering left ~110px of dead space below the header.
        ImGui::Dummy(ImVec2(0.f, S(20.f)));
        float stepPadX = (bodyAvail.x - S(kEmailStepPanelW)) * 0.5f;
        if (stepPadX > 0.f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + stepPadX);
        // No separate ChildBg — match the sheet (same as transparent body region).
        ImGui::BeginChild("emailStep", ImVec2(S(kEmailStepPanelW), S(kEmailStepPanelH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImGui::SetCursorPos(ImVec2(S(kEmailPromptL), S(kEmailPromptOffsetY)));
        if (g_fontTitle)
            ImGui::PushFont(g_fontTitle);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(473.f));
        ImGui::TextUnformatted("Enter your email to sign in:");
        ImGui::PopTextWrapPos();
        if (g_fontTitle)
            ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(S(kEmailMarginL), S(kEmailRowOffsetY)));
        // Match the email input height to the Send button height.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(10.f)));
        ImGui::PushItemWidth(S(kEmailFieldW));
        bool emailEnter = false;
        {
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  g_emailFieldBorderActive ? kMacInputBorderFocus : kMacInputBorderIdle);
            // EnterReturnsTrue so pressing Return submits, just like clicking Send code.
            emailEnter = ImGui::InputTextWithHint("##email", "your@email.com", g_emailBuf, sizeof g_emailBuf,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor();
            g_emailFieldBorderActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::SameLine(0.f, S(10.f));
        const bool busy = g_sendBusy.load(std::memory_order_acquire);

        // macOS-like primary action: blue background + white foreground.
        // While busy: hide the normal text and show centered "Sending..." inside the same button.
        const float sendBtnH = S(36.f);
        const char* sendBtnText = busy ? "Sending..." : "Send code";
        const char* sendBtnId = "##send_code_btn";
        ImVec2 sendBtnMin = ImGui::GetCursorScreenPos();

        ImGui::PushStyleColor(ImGuiCol_Button, kMacAccentBtn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kMacAccentBtnHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kMacAccentBtnAct);
        ImGui::PushStyleColor(ImGuiCol_Border, kMacAccentBtnBorder);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        const bool clicked = ImGui::Button(sendBtnId, ImVec2(S(kEmailSendBtnW), sendBtnH));
        ImGui::PopStyleColor(5);

        if (!busy && (clicked || emailEnter))
        {
            const size_t elen = std::strlen(g_emailBuf);
            if (elen == 0)
            {
                std::strncpy(g_errBuf, "Please enter your email address", sizeof g_errBuf - 1);
                g_errBuf[sizeof g_errBuf - 1] = '\0';
            }
            else if (!IsLikelyEmail(g_emailBuf))
            {
                std::strncpy(g_errBuf, "Please enter a valid email address", sizeof g_errBuf - 1);
                g_errBuf[sizeof g_errBuf - 1] = '\0';
            }
            else
            {
                g_errBuf[0] = '\0';
                g_Settings()->Set("settings.generator.nickname", std::string(g_emailBuf));
                g_Settings()->Storage()->Commit();
                g_sendBusy.store(true, std::memory_order_release);
                std::thread([]() {
                    EDreamClient::SendCodeResult result = EDreamClient::SendCode();
                    {
                        std::lock_guard<std::mutex> lock(g_jobMutex);
                        g_sendOk = result.success;
                        g_sendResult = std::move(result);
                    }
                    g_sendDone.store(true, std::memory_order_release);
                    g_sendBusy.store(false, std::memory_order_release);
                }).detach();
            }
        }

        if (g_errBuf[0] != '\0')
        {
            ImGui::SetCursorPos(ImVec2(S(kEmailPromptL), S(kEmailErrorOffsetY)));
            ImGui::TextColored(ImVec4(0.75f, 0.18f, 0.18f, 1.f), "%s", g_errBuf);
        }
        {
            // Draw label text centered within the button frame (Button label is hidden via "##send_code_btn").
            const ImVec2 ts = ImGui::CalcTextSize(sendBtnText);
            const ImVec2 textPos(sendBtnMin.x + (S(kEmailSendBtnW) - ts.x) * 0.5f,
                                 sendBtnMin.y + (sendBtnH - ts.y) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(
                textPos, ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f)), sendBtnText);
        }

        ImGui::SetCursorPos(ImVec2((S(kEmailStepPanelW) - S(kEmailCreateAccountW)) * 0.5f, S(kEmailCreateBtnOffsetY)));
        if (ImGui::Button("Need an account? Create one", ImVec2(S(kEmailCreateAccountW), S(36.f))))
            PlatformUtils::OpenURLExternally(kUrlCreateAccount);

        ImGui::EndChild();
    }
    else if (g_wizardStep == 1)
    {
        g_emailFieldBorderActive = false;
        const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
        // Small top gap; the code step is the tallest, so keep this minimal so the Try-again
        // row clears the body clip on shorter host windows.
        ImGui::Dummy(ImVec2(0.f, S(6.f)));
        float stepPadX = (bodyAvail.x - S(kCodeStepPanelW)) * 0.5f;
        if (stepPadX > 0.f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + stepPadX);
        ImGui::BeginChild("codeStep", ImVec2(S(kCodeStepPanelW), S(kCodeStepPanelH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImGui::SetCursorPos(ImVec2(S(38.f), S(kCodeInstrOffsetY)));
        if (g_fontTitle)
            ImGui::PushFont(g_fontTitle);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(434.f));
        ImGui::TextUnformatted("Check your email for a one-time code, and enter it below.");
        ImGui::PopTextWrapPos();
        if (g_fontTitle)
            ImGui::PopFont();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(10.f)));
        if (g_fontOtp)
            ImGui::PushFont(g_fontOtp);

        // Make the OTP input width fit exactly 6 digits (like the macOS layout).
        const float otpTextW = ImGui::CalcTextSize("000000").x;
        const float otpInputW = otpTextW + ImGui::GetStyle().FramePadding.x * 2.f;
        ImGui::SetCursorPos(ImVec2((S(kCodeStepPanelW) - otpInputW) * 0.5f, S(kCodeOtpOffsetY)));
        ImGui::PushItemWidth(otpInputW);
        bool otpEnter = false;
        {
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  g_otpFieldBorderActive ? kMacInputBorderFocus : kMacInputBorderIdle);
            // EnterReturnsTrue plus the digit/edit callbacks: Enter submits, callbacks still filter input.
            otpEnter = ImGui::InputTextWithHint("##otp", "000000", g_codeBuf, sizeof g_codeBuf,
                                                ImGuiInputTextFlags_CallbackCharFilter |
                                                    ImGuiInputTextFlags_CallbackEdit |
                                                    ImGuiInputTextFlags_EnterReturnsTrue,
                                                FilterDigitsOnly);
            ImGui::PopStyleColor();
            g_otpFieldBorderActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
        }
        if (g_fontOtp)
            ImGui::PopFont();
        ImGui::PopStyleVar();
        ImGui::PopItemWidth();
        StripNonDigits(g_codeBuf, sizeof g_codeBuf);

        const bool busy = g_validateBusy.load(std::memory_order_acquire);
        if (g_errBuf[0] != '\0')
        {
            ImGui::SetCursorPos(ImVec2(S(18.f), S(kCodeErrorOffsetY)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(473.f));
            ImGui::TextColored(ImVec4(0.75f, 0.18f, 0.18f, 1.f), "%s", g_errBuf);
            ImGui::PopTextWrapPos();
        }

        const bool canVerify = std::strlen(g_codeBuf) == 6 && !busy;
        ImGui::SetCursorPos(ImVec2((S(kCodeStepPanelW) - S(kCodeVerifyW)) * 0.5f, S(kCodeVerifyOffsetY)));
        if (!canVerify || busy)
            ImGui::BeginDisabled();
        const bool verifyClicked = ImGui::Button("Verify code", ImVec2(S(kCodeVerifyW), S(36.f)));
        if ((verifyClicked || otpEnter) && canVerify && !busy)
        {
            g_errBuf[0] = '\0';
            std::string codeCopy(g_codeBuf);
            g_validateBusy.store(true, std::memory_order_release);
            std::thread([code = std::move(codeCopy)]() {
                EDreamClient::ValidateCodeResult result =
                    EDreamClient::ValidateCodeDetailed(code);
                g_validateOk = result.success;
                g_validateResult = std::move(result);
                g_validateDone.store(true, std::memory_order_release);
                g_validateBusy.store(false, std::memory_order_release);
            }).detach();
        }
        if (!canVerify || busy)
            ImGui::EndDisabled();

        if (busy)
        {
            ImGui::SetCursorPos(ImVec2(S(kCodeStepPanelW) * 0.5f + S(kCodeVerifyW) * 0.5f + S(12.f),
                                       S(kCodeVerifyOffsetY) + S(6.f)));
            ImGui::TextDisabled("Verifying...");
        }

        ImGui::SetCursorPos(ImVec2((S(kCodeStepPanelW) - S(kCodeTryAgainW)) * 0.5f, S(kCodeTryAgainOffsetY)));
        if (ImGui::Button("Try again", ImVec2(S(kCodeTryAgainW), S(36.f))))
        {
            g_wizardStep = 0;
            g_codeBuf[0] = '\0';
            g_errBuf[0] = '\0';
        }

        ImGui::EndChild();
    }
    else
    {
        g_emailFieldBorderActive = false;
        g_otpFieldBorderActive = false;

        const float bodyAvailW = ImGui::GetContentRegionAvail().x;
        const float panelW = bodyAvailW < S(kThanksPanelW) ? bodyAvailW : S(kThanksPanelW);
        const float centerPad = (bodyAvailW - panelW) * 0.5f;
        if (centerPad > 0.f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerPad);

        const float innerW = panelW - 2.f * S(kThanksOuterPad);
        const float colW = (innerW - S(kThanksColGap)) * 0.5f;

        if (g_fontTitle)
            ImGui::PushFont(g_fontTitle);
        const char* tipsTitle = "All set! Quick tips:";
        const ImVec2 tipsTs = ImGui::CalcTextSize(tipsTitle);
        if (g_fontTitle)
            ImGui::PopFont();

        const float panelH = S(kThanksTitleTop) + tipsTs.y + S(kThanksTitleToGrid) + S(kThanksRowTopH) +
                             S(kThanksHSepGap) + S(kThanksHSepH) + S(kThanksHSepGap) + S(kThanksRowBotH) +
                             S(kThanksBottomPad);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::BeginChild("thanksPanel", ImVec2(panelW, panelH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Dummy(ImVec2(0.f, S(kThanksTitleTop)));

        if (g_fontTitle)
            ImGui::PushFont(g_fontTitle);
        {
            const float tipX = (panelW - tipsTs.x) * 0.5f;
            if (tipX > 0.f)
                ImGui::SetCursorPosX(tipX);
            ImGui::TextUnformatted(tipsTitle);
        }
        if (g_fontTitle)
            ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.f, S(kThanksTitleToGrid)));

        const float yTop = ImGui::GetCursorPosY();
        ImDrawList* panelDl = ImGui::GetWindowDrawList();
        const ImU32 sepU32 = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Border]);

        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad), yTop));
        ImGui::BeginChild("tl", ImVec2(colW, S(kThanksRowTopH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            if (g_fontThanksCopy)
                ImGui::PushFont(g_fontThanksCopy);
            ImGui::SetCursorPos(ImVec2(S(kThanksCellPad), S(kThanksCellPad)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + colW - 2.f * S(kThanksCellPad));
            ImGui::TextWrapped(
                "Use the A and D keys to adjust the speed of playback. Press F1 to see more keyboard "
                "controls. You can also interact with the remote control installed on your phone, or from "
                "a web browser:");
            ImGui::PopTextWrapPos();
            if (g_fontThanksCopy)
                ImGui::PopFont();

            const float btnY = S(kThanksRowTopH) - S(kThanksBtnBottom) - S(kThanksTipsBtnH);
            ImGui::SetCursorPos(ImVec2((colW - S(kThanksOpenRemoteBtnW)) * 0.5f, btnY));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            if (g_fontThanksBtn)
                ImGui::PushFont(g_fontThanksBtn);
            const bool openRemote =
                ImGui::Button("Open web remote", ImVec2(S(kThanksOpenRemoteBtnW), S(kThanksTipsBtnH)));
            if (g_fontThanksBtn)
                ImGui::PopFont();
            ImGui::PopStyleVar();
            if (openRemote)
                PlatformUtils::OpenURLExternally(kUrlWebRemote);
        }
        ImGui::EndChild();
        const ImVec2 tlRectMin = ImGui::GetItemRectMin();
        const ImVec2 tlRectMax = ImGui::GetItemRectMax();

        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad) + colW + S(kThanksColGap), yTop));
        ImGui::BeginChild("tr", ImVec2(colW, S(kThanksRowTopH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            const float textWrapW =
                colW - S(kThanksCellPad) - S(kThanksTextImgGap) - S(kThanksPlaylistImg) - S(kThanksImgTrail);
            if (g_fontThanksCopy)
                ImGui::PushFont(g_fontThanksCopy);
            ImGui::SetCursorPos(ImVec2(S(kThanksCellPad), S(kThanksCellPad)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWrapW);
            ImGui::TextWrapped(
                "Change your dreams by selecting a playlist from the browser. Click the button on a "
                "thumbnail to start that playlist.");
            ImGui::PopTextWrapPos();
            if (g_fontThanksCopy)
                ImGui::PopFont();

            if (g_srvPlaylist && g_texPlaylistW > 0 && g_texPlaylistH > 0)
            {
                const ImVec2 trWin = ImGui::GetWindowPos();
                const ImVec2 imgMin(trWin.x + colW - S(kThanksImgTrail) - S(kThanksPlaylistImg),
                                    trWin.y + S(kThanksImgTop));
                const ImVec2 imgMax(imgMin.x + S(kThanksPlaylistImg), imgMin.y + S(kThanksPlaylistImg));
                ImGui::GetWindowDrawList()->AddImage(
                    static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_srvPlaylist)), imgMin, imgMax);
            }

            const float btnY = S(kThanksRowTopH) - S(kThanksBtnBottom) - S(kThanksTipsBtnH);
            ImGui::SetCursorPos(ImVec2((colW - S(kThanksOpenPlaylistBtnW)) * 0.5f, btnY));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            if (g_fontThanksBtn)
                ImGui::PushFont(g_fontThanksBtn);
            const bool openPl =
                ImGui::Button("Open playlist browser", ImVec2(S(kThanksOpenPlaylistBtnW), S(kThanksTipsBtnH)));
            if (g_fontThanksBtn)
                ImGui::PopFont();
            ImGui::PopStyleVar();
            if (openPl)
                PlatformUtils::OpenURLExternally(kUrlPlaylists);
        }
        ImGui::EndChild();

        const float vSepX = tlRectMax.x + S(10.f);
        panelDl->AddRectFilled(ImVec2(vSepX, tlRectMin.y),
                               ImVec2(vSepX + 1.f, tlRectMin.y + S(kThanksRowTopH)), sepU32);

        const float hSepTop = tlRectMax.y + S(kThanksHSepGap);
        panelDl->AddRectFilled(ImVec2(tlRectMin.x, hSepTop),
                               ImVec2(tlRectMin.x + innerW, hSepTop + S(kThanksHSepH)), sepU32);

        const float yBot = yTop + S(kThanksRowTopH) + S(kThanksHSepGap) + S(kThanksHSepH) + S(kThanksHSepGap);
        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad), yBot));
        ImGui::BeginChild("bl", ImVec2(colW, S(kThanksRowBotH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            static const char kBlTips[] =
                "Use fullscreen (e.g. F11 or the app window menu) for the best experience on a large "
                "display. Run the app to get updates.";
            const float wrapPx = colW - 2.f * S(kThanksCellPad);
            ImVec2 wrapped;
            if (g_fontThanksCopy)
                ImGui::PushFont(g_fontThanksCopy);
            wrapped = ImGui::CalcTextSize(kBlTips, nullptr, false, wrapPx);
            if (g_fontThanksCopy)
                ImGui::PopFont();
            float yText = (S(kThanksRowBotH) - wrapped.y) * 0.5f;
            if (yText < S(kThanksCellPad))
                yText = S(kThanksCellPad);
            ImGui::SetCursorPos(ImVec2(S(kThanksCellPad), yText));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapPx);
            if (g_fontThanksCopy)
                ImGui::PushFont(g_fontThanksCopy);
            ImGui::TextWrapped("%s", kBlTips);
            if (g_fontThanksCopy)
                ImGui::PopFont();
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        const ImVec2 blRectMin = ImGui::GetItemRectMin();

        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad) + colW + S(kThanksColGap), yBot));
        ImGui::BeginChild("br", ImVec2(colW, S(kThanksRowBotH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            if (g_fontThanksBtn)
                ImGui::PushFont(g_fontThanksBtn);
            float dreamW = ImGui::CalcTextSize("Dream on!").x + ImGui::GetStyle().FramePadding.x * 2.f +
                           ImGui::GetStyle().FrameBorderSize * 2.f;
            if (g_fontThanksBtn)
                ImGui::PopFont();
            ImGui::PopStyleVar();
            if (dreamW < S(kThanksDreamOnMinW))
                dreamW = S(kThanksDreamOnMinW);

            const float bx = (colW - dreamW) * 0.5f;
            const float by = (S(kThanksRowBotH) - S(kThanksTipsBtnH)) * 0.5f;
            ImGui::SetCursorPos(ImVec2(bx, by));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Button, kMacAccentBtn);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kMacAccentBtnHov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, kMacAccentBtnAct);
            ImGui::PushStyleColor(ImGuiCol_Border, kMacAccentBtnBorder);
            if (g_fontThanksBtn)
                ImGui::PushFont(g_fontThanksBtn);
            const bool dreamOn = ImGui::Button("Dream on!", ImVec2(dreamW, S(kThanksTipsBtnH)));
            if (g_fontThanksBtn)
                ImGui::PopFont();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar();
            if (dreamOn)
            {
                g_visible.store(false, std::memory_order_release);
                g_pendingImGuiShutdown.store(true, std::memory_order_release);
            }
        }
        ImGui::EndChild();

        panelDl->AddRectFilled(ImVec2(vSepX, blRectMin.y),
                               ImVec2(vSepX + 1.f, blRectMin.y + S(kThanksRowBotH)), sepU32);

        ImGui::EndChild(); // thanksPanel
        ImGui::PopStyleVar(); // WindowPadding 0,0
    }

    ImGui::EndChild(); // body (transparent; sheet bg shows through)

    DrawSkipFooter();

    ImGui::EndChild(); // "Welcome to Infinidream"
    ImGui::PopStyleColor(); // sheetBg for Welcome
    ImGui::PopStyleVar(); // dialog padding
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(); // host padding (0,0)
}

} // namespace

void FirstTimeSetupWin32_Register()
{
    ESSetShowFirstTimeSetupCallback(OnFirstTimeSetupRequested);
}

void FirstTimeSetupWin32_SetOverlayAllowed(bool allow)
{
    g_overlayAllowed.store(allow, std::memory_order_release);
    if (!allow)
    {
        g_showRequested.store(false, std::memory_order_release);
        g_visible.store(false, std::memory_order_release);
        ShutdownImGui();
        ApplyFirstTimeWizardPauseState(false);
    }
}

bool FirstTimeSetupWin32_IsWizardVisible()
{
    return g_visible.load(std::memory_order_acquire) &&
           g_imguiInitialized.load(std::memory_order_acquire);
}

bool FirstTimeSetupWin32_TryConsumeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            LRESULT* outResult)
{
    if (!g_visible.load(std::memory_order_acquire) ||
        !g_imguiInitialized.load(std::memory_order_acquire))
        return false;

    if (g_imguiContext == nullptr)
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

bool FirstTimeSetupWin32_RenderIfNeeded(ID3D11Device* device, ID3D11DeviceContext* ctx,
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
            ResetWizardForShow();
            ApplyFirstTimeWizardPauseState(true);
        }
    }

    if (!g_visible.load(std::memory_order_acquire) || !g_imguiInitialized.load(std::memory_order_acquire))
        return false;

    PollWorkerResults();

    ImGui::SetCurrentContext(g_imguiContext);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    // Match the swap chain / viewport we render into (Win32 client rect can disagree during resize / DPI).
    ImGui::GetIO().DisplaySize = ImVec2(viewportW, viewportH);
    ImGui::NewFrame();

    DrawWizard();

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
    {
        ShutdownImGui();
        ApplyFirstTimeWizardPauseState(false);
    }

    return true;
}

#endif // WIN32
