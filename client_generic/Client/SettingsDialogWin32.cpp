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

#include <shobjidl.h>

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
std::atomic<bool> g_imguiInitialized{false};
std::atomic<bool> g_pendingImGuiShutdown{false};
ImGuiContext* g_imguiContext = nullptr;

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

static void ShutdownImGui()
{
    if (!g_imguiInitialized.load(std::memory_order_acquire))
        return;

    g_pendingImGuiShutdown.store(false, std::memory_order_release);
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsLight();
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

    g_imguiContext = ImGui::GetCurrentContext();
    g_imguiInitialized.store(true, std::memory_order_release);
    return true;
}

static void SetStatus(const std::string& status)
{
    std::strncpy(g_statusBuf, status.c_str(), sizeof g_statusBuf - 1);
    g_statusBuf[sizeof g_statusBuf - 1] = '\0';
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

static void CloseDialog(bool saveBeforeClose)
{
    if (saveBeforeClose)
        SaveSettings();
    g_visible.store(false, std::memory_order_release);
    g_pendingImGuiShutdown.store(true, std::memory_order_release);
}

static void DrawAccountTab()
{
    const bool loggedIn = EDreamClient::IsLoggedIn();
    const float actionButtonWidth = 140.f;

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

    ImGui::ColorButton("##authdot", authColor,
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop |
                           ImGuiColorEditFlags_NoBorder,
                       ImVec2(18.f, 18.f));
    ImGui::SameLine();
    if (loggedIn)
        ImGui::Text("Signed in as %s", g_nicknameBuf);
    else
        ImGui::TextUnformatted(authText);

    if (!loggedIn)
    {
        ImGui::BeginDisabled(g_sentCode);
        ImGui::PushItemWidth(-1.f);
        ImGui::InputTextWithHint("Email", "john@smith.com", g_nicknameBuf, sizeof g_nicknameBuf);
        ImGui::PopItemWidth();
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!g_sentCode);
        ImGui::PushItemWidth(-1.f);
        ImGui::InputTextWithHint("Code", "6 digit code", g_codeBuf, sizeof g_codeBuf);
        ImGui::PopItemWidth();
        ImGui::EndDisabled();
    }

    if (!loggedIn)
    {
        if (!g_sentCode)
        {
            if (ImGui::Button("Send Code", ImVec2(actionButtonWidth, 0.f)))
            {
                TrimWhitespaceInPlace(g_nicknameBuf);
                g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));
                g_Settings()->Storage()->Commit();
                const auto result = EDreamClient::SendVerificationCodeOutcome();
                g_sentCode = result.first;
                SetStatus(result.second);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled();
            ImGui::Button("Start Again", ImVec2(actionButtonWidth, 0.f));
            ImGui::EndDisabled();
        }
        else
        {
            if (ImGui::Button("Validate", ImVec2(actionButtonWidth, 0.f)))
            {
                StripNonDigits(g_codeBuf);
                if (EDreamClient::ValidateCode(std::string(g_codeBuf)))
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
                    SetStatus("Invalid code. Please try again.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Start Again", ImVec2(actionButtonWidth, 0.f)))
            {
                g_sentCode = false;
                g_codeBuf[0] = '\0';
                SetStatus("Code flow restarted.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Need an account? Create one", ImVec2(220.f, 0.f)))
            PlatformUtils::OpenURLExternally(kUrlCreateAccount);
    }
    else
    {
        if (ImGui::Button("Sign Out", ImVec2(actionButtonWidth, 0.f)))
        {
            std::strncpy(g_previousLoginEmailBuf, g_nicknameBuf, sizeof g_previousLoginEmailBuf - 1);
            g_previousLoginEmailBuf[sizeof g_previousLoginEmailBuf - 1] = '\0';
            g_hasPreviousLoginEmail = true;
            EDreamClient::SignOut();
            g_sentCode = false;
            g_codeBuf[0] = '\0';
            SetStatus("Signed out.");
        }
    }
}

static void DrawControlsTab()
{
    ImGui::TextWrapped("Use A/D to change playback speed. Press F1 to see all keyboard controls.");
    if (ImGui::Button("Open web remote"))
        PlatformUtils::OpenURLExternally(kUrlWebRemote);
    ImGui::SameLine();
    if (ImGui::Button("Open playlist browser"))
        PlatformUtils::OpenURLExternally(kUrlPlaylists);
    if (ImGui::Button("View Help Page"))
        PlatformUtils::OpenURLExternally(kUrlHelp);
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
    const float windowWidth = (viewportW > 960.f) ? 860.f : (viewportW - 64.f);
    const float windowHeight = (viewportH > 820.f) ? 680.f : (viewportH - 64.f);
    const ImVec2 windowSize((windowWidth < 520.f) ? 520.f : windowWidth,
                            (windowHeight < 420.f) ? 420.f : windowHeight);
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
        if (EDreamClient::IsLoggedIn())
        {
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
        }
        else
        {
            DrawAccountTab();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("settings_footer_region", ImVec2(0.f, 0.f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            const float rowHeight = 24.f;
            const float helpButtonSize = 22.f;
            const float closeButtonWidth = 78.f;
            const float closeButtonHeight = 24.f;
            const float originX = ImGui::GetCursorPosX();
            const float contentWidth = ImGui::GetContentRegionAvail().x;
            const float rowY = ImGui::GetCursorPosY() +
                               ((ImGui::GetContentRegionAvail().y - rowHeight) * 0.5f);
            const float closeX = originX + contentWidth - closeButtonWidth;

            ImGui::SetCursorPos(ImVec2(closeX, rowY));
            if (ImGui::Button("Close", ImVec2(closeButtonWidth, closeButtonHeight)))
                CloseDialog(true);

            ImGui::SetCursorPos(ImVec2(originX, rowY + ((rowHeight - helpButtonSize) * 0.5f)));
            if (ImGui::Button("?", ImVec2(helpButtonSize, helpButtonSize)))
                PlatformUtils::OpenURLExternally(kUrlHelp);

            ImGui::SameLine();
            ImGui::SetCursorPosY(rowY + ((rowHeight - ImGui::GetTextLineHeight()) * 0.5f));
            ImGui::TextDisabled("%s", g_versionText.c_str());

            if (g_statusBuf[0] != '\0')
            {
                ImGui::SameLine();
                ImGui::SetCursorPosY(rowY + ((rowHeight - ImGui::GetTextLineHeight()) * 0.5f));
                ImGui::TextDisabled(" | %s", g_statusBuf);
            }
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
        g_showRequested.store(false, std::memory_order_release);
        g_visible.store(false, std::memory_order_release);
        ShutdownImGui();
    }
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
        }
    }

    if (!g_visible.load(std::memory_order_acquire) || !g_imguiInitialized.load(std::memory_order_acquire))
        return false;

    ImGui::SetCurrentContext(g_imguiContext);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::GetIO().DisplaySize = ImVec2(viewportW, viewportH);
    ImGui::NewFrame();

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
