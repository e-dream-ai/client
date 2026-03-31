#ifdef WIN32

#include "SettingsDialogWin32.h"

#include <atomic>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
std::atomic<bool> g_pausedBySettingsDialog{false};
std::atomic<bool> g_imguiInitialized{false};
std::atomic<bool> g_pendingImGuiShutdown{false};
ImGuiContext* g_imguiContext = nullptr;
ImFont* g_boldUiFont = nullptr;
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

bool g_autoInstallScreensaver = false;
bool g_keepScreensaverEnabled = false;

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

static void OnShowPreferencesRequested()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsLight();
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
                ImFont* loaded = io.Fonts->AddFontFromFileTTF(fontPath, 17.0f);
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
                g_boldUiFont = io.Fonts->AddFontFromFileTTF(fontPath, 17.0f);
                if (g_boldUiFont)
                    break;
            }
        }
    }

    if (!ImGui_ImplWin32_Init(dx->GetWindowHandle()))
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

static void TrimWhitespaceInPlace(char* s)
{
    if (!s)
        return;

    size_t len = std::strlen(s);
    size_t first = 0;
    while (first < len && std::isspace(static_cast<unsigned char>(s[first])) != 0)
        ++first;
    size_t last = len;
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1])) != 0)
        --last;

    if (first > 0)
        std::memmove(s, s + first, last - first);
    s[last - first] = '\0';
}

static void StripNonDigits(char* s)
{
    if (!s)
        return;

    size_t w = 0;
    for (size_t r = 0; s[r] != '\0'; ++r)
    {
        if (std::isdigit(static_cast<unsigned char>(s[r])) != 0)
        {
            s[w++] = s[r];
            if (w >= 6)
                break;
        }
    }
    s[w] = '\0';
}

static bool InputTextWithPlaceholder(const char* id, const char* placeholder, char* buf, size_t bufSize,
                                     ImGuiInputTextFlags flags = 0)
{
    const bool changed = ImGui::InputText(id, buf, bufSize, flags);
    if (placeholder && placeholder[0] != '\0' && buf && buf[0] == '\0' && !ImGui::IsItemActive())
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        const float textY = min.y + ((max.y - min.y - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + pad.x, textY),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            placeholder);
    }
    return changed;
}

static void DrawFocusedInputDecoration(bool focused)
{
    if (!focused)
        return;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    const float rounding = ImGui::GetStyle().FrameRounding;

    // Subtle outer glow/shadow like macOS focus ring.
    drawList->AddRect(ImVec2(min.x - 2.f, min.y - 2.f), ImVec2(max.x + 2.f, max.y + 2.f),
                      IM_COL32(64, 132, 255, 70), rounding + 1.f, 0, 3.f);
    // Crisp blue focus border.
    drawList->AddRect(min, max, IM_COL32(0, 122, 255, 255), rounding, 0, 1.6f);
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

static std::string WideToUtf8Path(const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return std::string();
    std::string utf8(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), n, nullptr, nullptr);
    if (!utf8.empty() && utf8.back() == '\0')
        utf8.pop_back();
    return utf8;
}

static bool ChooseContentFolder(char* outBuf, size_t outSize)
{
    HRESULT coInitHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninit = SUCCEEDED(coInitHr);
    const bool canProceed = SUCCEEDED(coInitHr) || coInitHr == RPC_E_CHANGED_MODE;
    if (!canProceed)
        return false;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || !dialog)
    {
        if (shouldUninit)
            CoUninitialize();
        return false;
    }

    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);

    if (outBuf && outBuf[0] != '\0')
    {
        const std::wstring folder = Utf8ToWidePath(std::string(outBuf));
        if (!folder.empty())
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&item))))
            {
                dialog->SetFolder(item);
                item->Release();
            }
        }
    }

    HWND owner = nullptr;
    if (auto* dx = TryGetDx11Display())
        owner = dx->GetWindowHandle();

    bool selected = false;
    if (SUCCEEDED(dialog->Show(owner)))
    {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result)
        {
            PWSTR folderW = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &folderW)) && folderW)
            {
                const std::string folder = WideToUtf8Path(folderW);
                if (!folder.empty())
                {
                    std::strncpy(outBuf, folder.c_str(), outSize - 1);
                    outBuf[outSize - 1] = '\0';
                    selected = true;
                }
                CoTaskMemFree(folderW);
            }
            result->Release();
        }
    }

    dialog->Release();
    if (shouldUninit)
        CoUninitialize();
    return selected;
}

static void CopySettingToBuf(const char* key, std::string_view fallback, char* outBuf, size_t outSize)
{
    const std::string value = g_Settings()->Get(key, std::string(fallback));
    std::strncpy(outBuf, value.c_str(), outSize - 1);
    outBuf[outSize - 1] = '\0';
}

static void LoadSettingsForShow()
{
    CopySettingToBuf("settings.generator.nickname", std::string(), g_nicknameBuf, sizeof g_nicknameBuf);
    g_codeBuf[0] = '\0';
    g_sentCode = false;
    g_previousLoginEmailBuf[0] = '\0';
    g_hasPreviousLoginEmail = false;
    g_versionText = PlatformUtils::GetAppVersion() + " " +
                    PlatformUtils::GetGitRevision() + " " +
                    PlatformUtils::GetBuildDate();

    g_playerFps = g_Settings()->Get("settings.player.player_fps", 23.0);
    g_displayFps = g_Settings()->Get("settings.player.display_fps", 60.0);
    g_vsync = g_Settings()->Get("settings.player.vbl_sync", false);
    g_preserveAR = g_Settings()->Get("settings.player.preserve_AR", false);
    g_blackoutMonitors = g_Settings()->Get("settings.player.blackout_monitors", true);
    g_quietMode = g_Settings()->Get("settings.player.quiet_mode", true);
    g_showAttribution = g_Settings()->Get("settings.app.attributionpng", false);

    CopySettingToBuf("settings.content.sheepdir", std::string(), g_contentDirBuf, sizeof g_contentDirBuf);
    g_unlimitedCache = g_Settings()->Get("settings.content.unlimited_cache", false);
    g_cacheSizeGb = g_Settings()->Get("settings.content.cache_size", 10);
    if (g_cacheSizeGb <= 0)
    {
        g_cacheSizeGb = 10;
        g_unlimitedCache = true;
    }

    g_useProxy = g_Settings()->Get("settings.content.use_proxy", false);
    CopySettingToBuf("settings.content.proxy", std::string(), g_proxyHostBuf, sizeof g_proxyHostBuf);
    CopySettingToBuf("settings.content.proxy_username", std::string(), g_proxyLoginBuf, sizeof g_proxyLoginBuf);
    CopySettingToBuf("settings.content.proxy_password", std::string(), g_proxyPasswordBuf, sizeof g_proxyPasswordBuf);
    g_debugLog = g_Settings()->Get("settings.app.log", false);
    CopySettingToBuf("settings.content.server", ServerConfig::DEFAULT_DREAM_SERVER, g_serverBuf, sizeof g_serverBuf);

    g_autoInstallScreensaver = g_Settings()->Get("settings.app.auto_install_screensaver", false);
    g_keepScreensaverEnabled = g_Settings()->Get("settings.app.keep_screensaver_enabled", false);

    g_statusBuf[0] = '\0';
}

static void SaveSettings()
{
    if (g_playerFps < 0.1)
        g_playerFps = 20.0;
    if (g_displayFps < 0.1)
        g_displayFps = 60.0;
    if (g_cacheSizeGb < 1)
        g_cacheSizeGb = 1;

    g_Settings()->Set("settings.player.player_fps", g_playerFps);
    g_Settings()->Set("settings.player.display_fps", g_displayFps);
    g_Settings()->Set("settings.player.DisplayMode", 2);
    g_Settings()->Set("settings.player.vbl_sync", g_vsync);
    g_Settings()->Set("settings.player.preserve_AR", g_preserveAR);
    g_Settings()->Set("settings.player.blackout_monitors", g_blackoutMonitors);
    g_Settings()->Set("settings.player.quiet_mode", g_quietMode);
    g_Settings()->Set("settings.app.attributionpng", g_showAttribution);

    g_Settings()->Set("settings.content.sheepdir", std::string(g_contentDirBuf));
    g_Settings()->Set("settings.content.unlimited_cache", g_unlimitedCache);
    g_Settings()->Set("settings.content.cache_size", g_cacheSizeGb);

    g_Settings()->Set("settings.content.use_proxy", g_useProxy);
    g_Settings()->Set("settings.content.proxy", std::string(g_proxyHostBuf));
    g_Settings()->Set("settings.content.proxy_username", std::string(g_proxyLoginBuf));
    g_Settings()->Set("settings.content.proxy_password", std::string(g_proxyPasswordBuf));
    g_Settings()->Set("settings.app.log", g_debugLog);
    g_Settings()->Set("settings.content.server", std::string(g_serverBuf));

    g_Settings()->Set("settings.app.auto_install_screensaver", g_autoInstallScreensaver);
    g_Settings()->Set("settings.app.keep_screensaver_enabled", g_keepScreensaverEnabled);
    g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));
    g_Settings()->Storage()->Commit();

    if (!g_unlimitedCache)
    {
        const std::uintmax_t cacheSize = static_cast<std::uintmax_t>(g_cacheSizeGb) * 1024ull * 1024ull * 1024ull;
        Cache::CacheManager::getInstance().resizeCache(cacheSize);
    }
}

static void ResetFormForShow()
{
    LoadSettingsForShow();
}

static void ApplyDialogPauseState(bool visible)
{
    if (visible)
    {
        const bool wasPaused = g_Player().IsPaused();
        g_wasPausedBeforeDialog.store(wasPaused, std::memory_order_release);
        if (!wasPaused)
        {
            g_Player().SetPaused(true);
            g_pausedBySettingsDialog.store(true, std::memory_order_release);
        }
        else
        {
            g_pausedBySettingsDialog.store(false, std::memory_order_release);
        }
        return;
    }

    if (g_pausedBySettingsDialog.exchange(false, std::memory_order_acq_rel))
        g_Player().SetPaused(g_wasPausedBeforeDialog.load(std::memory_order_acquire));
}

static void CloseDialog(bool saveBeforeClose)
{
    if (saveBeforeClose)
        SaveSettings();
    ApplyDialogPauseState(false);
    g_visible.store(false, std::memory_order_release);
    g_pendingImGuiShutdown.store(true, std::memory_order_release);
}

static void DrawAccountTab()
{
    const bool loggedIn = EDreamClient::IsLoggedIn();
    const float actionButtonWidth = 100.f;
    const float actionButtonHeight = 30.f;
    const float panelMaxWidth = 495.f;
    const float sidePadding = 60.f;

    ImVec4 authColor = ImVec4(0.89f, 0.20f, 0.24f, 1.0f);
    const char* authText = "Please sign in.";
    if (loggedIn)
    {
        authColor = ImVec4(0.18f, 0.72f, 0.29f, 1.0f);
        authText = "Signed in";
    }
    else if (g_sentCode)
    {
        authColor = ImVec4(0.95f, 0.66f, 0.18f, 1.0f);
        authText = "Check your e-mail for confirmation code";
    }

    const float availW = ImGui::GetContentRegionAvail().x;
    const float availH = ImGui::GetContentRegionAvail().y;
    const float safePanelWidth = (availW < 300.f) ? 300.f : availW;

    const float formHeight = loggedIn ? 92.f : 230.f;
    const float topPad = (availH - formHeight) * 0.5f;
    if (topPad > 0.f)
        ImGui::Dummy(ImVec2(0.f, topPad));

    ImGui::BeginChild("account_centered_body", ImVec2(safePanelWidth, formHeight), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

    if (loggedIn)
    {
        const std::string signedInText = std::string("Signed in as ") + g_nicknameBuf;
        const float dotRadius = 5.f;
        const float rowW = dotRadius * 2.f + 8.f + ImGui::CalcTextSize(signedInText.c_str()).x;
        const float rowX = sidePadding;
        if (rowX > 0.f)
            ImGui::SetCursorPosX(rowX);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float lineHeight = ImGui::GetTextLineHeight();
        const ImVec2 dotCenter(ImGui::GetCursorScreenPos().x + dotRadius, ImGui::GetCursorScreenPos().y + lineHeight * 0.5f);
        drawList->AddCircleFilled(dotCenter, dotRadius, ImGui::ColorConvertFloat4ToU32(authColor), 24);
        ImGui::Dummy(ImVec2(dotRadius * 2.f, lineHeight));
        ImGui::SameLine(0.f, 8.f);
        ImGui::TextUnformatted(signedInText.c_str());
    }

    if (!loggedIn)
    {
        ImGui::Spacing();
        const float contentW = safePanelWidth - sidePadding * 2.f;
        const float fieldGap = 10.f;
        const float emailLabelW = ImGui::CalcTextSize("Email:").x;
        const float codeLabelW = ImGui::CalcTextSize("Code:").x;
        const float labelW = (emailLabelW > codeLabelW) ? emailLabelW : codeLabelW;
        const float emailInputW = contentW - labelW - fieldGap;
        const float codeInputW = 96.f; // Match the compact macOS code input feel.
        const float leftX = sidePadding;

        ImGui::SetCursorPosX(leftX);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Email:");
        ImGui::BeginDisabled(g_sentCode);
        ImGui::SameLine(0.f, fieldGap);
        ImGui::PushItemWidth(emailInputW);
        InputTextWithPlaceholder("##email", "eg: john@smith.com", g_nicknameBuf, sizeof g_nicknameBuf);
        DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
        ImGui::PopItemWidth();
        ImGui::EndDisabled();

        ImGui::SetCursorPosX(leftX);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Code:");
        ImGui::BeginDisabled(!g_sentCode);
        ImGui::SameLine(0.f, fieldGap);
        ImGui::PushItemWidth(codeInputW);
        InputTextWithPlaceholder("##code", "6 digit code", g_codeBuf, sizeof g_codeBuf,
                                 ImGuiInputTextFlags_CharsDecimal);
        StripNonDigits(g_codeBuf);
        DrawFocusedInputDecoration(ImGui::IsItemActive() || ImGui::IsItemFocused());
        ImGui::PopItemWidth();
        ImGui::EndDisabled();

        ImGui::SetCursorPosX(leftX);
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const float dotRadius = 5.f;
            const float lineHeight = ImGui::GetTextLineHeight();
            const ImVec2 dotCenter(ImGui::GetCursorScreenPos().x + dotRadius,
                                   ImGui::GetCursorScreenPos().y + lineHeight * 0.5f);
            drawList->AddCircleFilled(dotCenter, dotRadius, ImGui::ColorConvertFloat4ToU32(authColor), 24);
            ImGui::Dummy(ImVec2(dotRadius * 2.f, lineHeight));
            ImGui::SameLine(0.f, 8.f);
        }
        ImGui::TextUnformatted(authText);

        const float buttonRowW = actionButtonWidth * 2.f + ImGui::GetStyle().ItemSpacing.x;
        const float buttonX = safePanelWidth - sidePadding - buttonRowW;
        if (buttonX > 0.f)
            ImGui::SetCursorPosX(buttonX);

        if (!g_sentCode)
        {
            ImGui::BeginDisabled();
            ImGui::Button("Start Again", ImVec2(actionButtonWidth, actionButtonHeight));
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            if (g_boldUiFont)
                ImGui::PushFont(g_boldUiFont);
            if (ImGui::Button("Send Code", ImVec2(actionButtonWidth, actionButtonHeight)))
            {
                TrimWhitespaceInPlace(g_nicknameBuf);
                g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));
                g_Settings()->Storage()->Commit();
                const EDreamClient::SendCodeResult result = EDreamClient::SendCode();
                g_sentCode = result.success;
                if (result.success)
                {
                    SetStatus(result.message.empty() ? "Check your e-mail for confirmation code" : result.message);
                }
                else
                {
                    const AuthDialogContent dialog = BuildSendCodeFailureDialog(result);
                    SetStatus(dialog.message);
                    ShowAuthWarningDialog(dialog);
                }
            }
            if (g_boldUiFont)
                ImGui::PopFont();
            ImGui::PopStyleColor(4);
        }
        else
        {
            if (ImGui::Button("Start again", ImVec2(actionButtonWidth, actionButtonHeight)))
            {
                g_sentCode = false;
                g_codeBuf[0] = '\0';
                SetStatus("Code flow restarted.");
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
            if (g_boldUiFont)
                ImGui::PushFont(g_boldUiFont);
            if (ImGui::Button("Validate", ImVec2(actionButtonWidth, actionButtonHeight)))
            {
                StripNonDigits(g_codeBuf);
                const EDreamClient::ValidateCodeResult validateResult =
                    EDreamClient::ValidateCodeDetailed(std::string(g_codeBuf));
                if (validateResult.success)
                {
                    const bool accountChanged =
                        g_hasPreviousLoginEmail &&
                        std::strcmp(g_previousLoginEmailBuf, g_nicknameBuf) != 0;
                    EDreamClient::DidSignIn();
                    g_sentCode = false;
                    SetStatus("Login successful.");
                    if (accountChanged)
                    {
                        MessageBoxA(nullptr,
                                    "infinidream will now exit. Please restart the application "
                                    "to take your new settings into account.",
                                    "Account Change Detected", MB_OK | MB_ICONINFORMATION);
                        std::exit(0);
                    }
                }
                else
                {
                    const AuthDialogContent dialog = BuildValidateFailureDialog(validateResult);
                    SetStatus(dialog.message);
                    ShowAuthWarningDialog(dialog);
                }
            }
            if (g_boldUiFont)
                ImGui::PopFont();
            ImGui::PopStyleColor(4);
        }

        ImGui::SetCursorPosX(sidePadding);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.86f, 0.86f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
        if (ImGui::Button("Need an account? Create one", ImVec2(contentW, actionButtonHeight)))
            PlatformUtils::OpenURLExternally(kUrlCreateAccount);
        ImGui::PopStyleColor(4);
    }
    else
    {
        const float signOutX = safePanelWidth - sidePadding - actionButtonWidth;
        if (signOutX > 0.f)
            ImGui::SetCursorPosX(signOutX);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.00f, 0.48f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.56f, 1.00f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.00f, 0.40f, 0.86f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.f));
        if (g_boldUiFont)
            ImGui::PushFont(g_boldUiFont);
        if (ImGui::Button("Sign out", ImVec2(actionButtonWidth, actionButtonHeight)))
        {
            std::strncpy(g_previousLoginEmailBuf, g_nicknameBuf, sizeof g_previousLoginEmailBuf - 1);
            g_previousLoginEmailBuf[sizeof g_previousLoginEmailBuf - 1] = '\0';
            g_hasPreviousLoginEmail = true;
            EDreamClient::SignOut();
            g_sentCode = false;
            g_codeBuf[0] = '\0';
            SetStatus("Signed out.");
        }
        if (g_boldUiFont)
            ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }

    ImGui::EndChild();
}

static void DrawControlsTab()
{
    const float sectionHeight = 190.f;
    const float panelGap = 8.f;
    const float panelInnerPadding = 8.f;
    const float buttonWidth = 170.f;
    const float buttonHeight = 30.f;
    const float footerRowHeight = 34.f;
    const float dividerWidth = 1.f;
    const ImVec4 sectionDividerColor(0.90f, 0.90f, 0.90f, 1.00f);

    const float controlsFontScale = 14.0f / 17.0f;
    ImGui::SetWindowFontScale(controlsFontScale); // Match macOS controls-tab text and button sizing.

    const float rowStartY = ImGui::GetCursorPosY();
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float panelWidth = (availWidth - (panelGap * 2.f) - dividerWidth) * 0.5f;

    auto drawControlPanel = [&](const char* panelId, const char* bodyText, const char* buttonLabel, const char* url,
                                bool showPlaylistIcon) {
        ImGui::BeginChild(panelId, ImVec2(panelWidth, sectionHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::SetCursorPos(ImVec2(panelInnerPadding, panelInnerPadding));
        float textWrapPos = panelWidth - panelInnerPadding;
        if (showPlaylistIcon && g_srvPlaylistIcon && g_texPlaylistIconW > 0 && g_texPlaylistIconH > 0)
            textWrapPos -= (41.f + 8.f);
        ImGui::PushTextWrapPos(textWrapPos);
        ImGui::TextWrapped("%s", bodyText);
        ImGui::PopTextWrapPos();

        if (showPlaylistIcon && g_srvPlaylistIcon && g_texPlaylistIconW > 0 && g_texPlaylistIconH > 0)
        {
            const float iconW = 41.f;
            const float iconH = 44.f;
            const float iconX = panelWidth - iconW - 6.f;
            const float iconY = 32.f;
            ImGui::SetCursorPos(ImVec2(iconX, iconY));
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_srvPlaylistIcon)), ImVec2(iconW, iconH));
        }

        const float buttonX = (panelWidth - buttonWidth) * 0.5f;
        const float buttonY = sectionHeight - buttonHeight - 10.f;
        ImGui::SetCursorPos(ImVec2(buttonX, buttonY));
        if (ImGui::Button(buttonLabel, ImVec2(buttonWidth, buttonHeight)))
            PlatformUtils::OpenURLExternally(url);
        ImGui::EndChild();
    };

    drawControlPanel("controls_remote_panel",
                     "Use the A and D keys to adjust the speed of playback. Press F1 to see more keyboard controls. You can also interact with the remote control installed on your phone, or from a web browser:",
                     "Open web remote", kUrlWebRemote, false);

    ImGui::SameLine(0.f, panelGap);
    ImGui::BeginGroup();
    {
        const ImVec2 dividerTop = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(dividerWidth, sectionHeight));
        const ImVec2 dividerBottom(dividerTop.x, dividerTop.y + sectionHeight);
        ImGui::GetWindowDrawList()->AddLine(dividerTop, dividerBottom,
                                            ImGui::GetColorU32(sectionDividerColor), dividerWidth);
    }
    ImGui::EndGroup();

    ImGui::SameLine(0.f, panelGap);
    drawControlPanel("controls_playlist_panel",
                     "Change your dreams by selecting a playlist from the browser. Click the button on a thumbnail to start that playlist.",
                     "Open playlist browser", kUrlPlaylists, true);

    ImGui::SetCursorPosY(rowStartY + sectionHeight + panelGap);
    ImGui::PushStyleColor(ImGuiCol_Separator, sectionDividerColor);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + panelGap);

    const float footerStartX = ImGui::GetCursorPosX();
    const float footerStartY = ImGui::GetCursorPosY();
    const float footerAvailWidth = ImGui::GetContentRegionAvail().x;
    const float footerGap = 12.f;
    const char* footerLabel = "For more controls and explanation:";

    ImGui::SetWindowFontScale(16.0f / 17.0f);
    const ImVec2 footerLabelSize = ImGui::CalcTextSize(footerLabel);
    ImGui::SetWindowFontScale(controlsFontScale);

    const float footerGroupWidth = footerLabelSize.x + footerGap + buttonWidth;
    const float footerGroupStartX = footerStartX + (footerAvailWidth - footerGroupWidth) * 0.5f;
    const float footerTextY = footerStartY + (footerRowHeight - footerLabelSize.y) * 0.5f;
    const float footerButtonY = footerStartY + (footerRowHeight - buttonHeight) * 0.5f;

    ImGui::SetWindowFontScale(16.0f / 17.0f);
    ImGui::SetCursorPos(ImVec2(footerGroupStartX, footerTextY));
    ImGui::TextUnformatted(footerLabel);
    ImGui::SetWindowFontScale(controlsFontScale);

    ImGui::SetCursorPos(ImVec2(footerGroupStartX + footerLabelSize.x + footerGap, footerButtonY));
    if (ImGui::Button("View Help Page", ImVec2(buttonWidth, buttonHeight)))
        PlatformUtils::OpenURLExternally(kUrlHelp);

    ImGui::SetWindowFontScale(1.0f);
}

static void DrawDiskTab()
{
    const float chooseButtonWidth = 100.f;
    const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    float contentWidth = ImGui::GetContentRegionAvail().x - chooseButtonWidth - itemSpacing;
    if (contentWidth < 160.f)
        contentWidth = 160.f;

    ImGui::PushItemWidth(contentWidth);
    ImGui::InputText("Content Folder", g_contentDirBuf, sizeof g_contentDirBuf);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Choose...", ImVec2(chooseButtonWidth, 0.f)))
    {
        if (ChooseContentFolder(g_contentDirBuf, sizeof g_contentDirBuf))
            SetStatus("Content folder updated.");
    }
    ImGui::Checkbox("Unlimited cache", &g_unlimitedCache);
    if (!g_unlimitedCache)
        ImGui::InputInt("Max disk space (GB)", &g_cacheSizeGb);
}

static void DrawDisplayTab()
{
    ImGui::Checkbox("Preserve Aspect Ratio", &g_preserveAR);
}

static void DrawAdvancedTab()
{
    ImGui::Checkbox("Use Proxy", &g_useProxy);
    ImGui::PushItemWidth(-1.f);
    ImGui::InputText("Proxy Host", g_proxyHostBuf, sizeof g_proxyHostBuf);
    ImGui::InputText("Proxy Login", g_proxyLoginBuf, sizeof g_proxyLoginBuf);
    ImGui::InputText("Proxy Password", g_proxyPasswordBuf, sizeof g_proxyPasswordBuf,
                     ImGuiInputTextFlags_Password);
    ImGui::PopItemWidth();

#ifdef DEBUG
    ImGui::Separator();
    ImGui::PushItemWidth(-1.f);
    ImGui::InputText("Server", g_serverBuf, sizeof g_serverBuf);
    ImGui::PopItemWidth();
#endif
    ImGui::Checkbox("Install and update screensaver", &g_autoInstallScreensaver);
    ImGui::Checkbox("Keep screensaver enabled", &g_keepScreensaverEnabled);
}

static void DrawSettingsDialog(float viewportW, float viewportH)
{
    const float targetWidth = 541.f;   // Match macOS settings dialog content width.
    const float targetHeight = 390.f;  // Match macOS settings dialog content height.
    const float windowWidth = (viewportW > (targetWidth + 64.f)) ? targetWidth : (viewportW - 32.f);
    const float windowHeight = (viewportH > (targetHeight + 64.f)) ? targetHeight : (viewportH - 32.f);
    const ImVec2 windowSize((windowWidth < 460.f) ? 460.f : windowWidth,
                            (windowHeight < 340.f) ? 340.f : windowHeight);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((viewportW - windowSize.x) * 0.5f, (viewportH - windowSize.y) * 0.5f),
                            ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.f, 16.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 10.f));
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
        const float footerHeight = 44.f;
        const float separatorReserve = ImGui::GetStyle().ItemSpacing.y + 2.f;
        float contentHeight = ImGui::GetContentRegionAvail().y - footerHeight - separatorReserve;
        if (contentHeight < 120.f)
            contentHeight = 120.f;

        ImGui::BeginChild("settings_content_region", ImVec2(0.f, contentHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::BeginTabBar("settings_tabs"))
        {
            if (ImGui::BeginTabItem("Account"))
            {
                DrawAccountTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controls"))
            {
                DrawControlsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Disk"))
            {
                DrawDiskTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Display"))
            {
                DrawDisplayTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Advanced"))
            {
                DrawAdvancedTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("settings_footer_region", ImVec2(0.f, 0.f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const float helpButtonSize = 20.f;
            const float closeButtonWidth = 78.f;
            const float closeButtonHeight = 24.f;
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
