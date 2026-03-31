#ifdef WIN32

#include "SettingsDialogWin32.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

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
    ImGui::StyleColorsDark();

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

static void CloseDialog()
{
    g_visible.store(false, std::memory_order_release);
    g_pendingImGuiShutdown.store(true, std::memory_order_release);
}

static void DrawAccountTab()
{
    if (EDreamClient::IsLoggedIn())
    {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Signed in as %s", g_nicknameBuf);
    }
    else if (g_sentCode)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "Code sent. Please validate.");
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Please sign in.");
    }

    ImGui::InputTextWithHint("Email", "john@smith.com", g_nicknameBuf, sizeof g_nicknameBuf);
    ImGui::InputTextWithHint("Code", "6 digit code", g_codeBuf, sizeof g_codeBuf);

    if (!EDreamClient::IsLoggedIn())
    {
        if (!g_sentCode)
        {
            if (ImGui::Button("Send Code"))
            {
                g_Settings()->Set("settings.generator.nickname", std::string(g_nicknameBuf));
                g_Settings()->Storage()->Commit();
                const auto result = EDreamClient::SendVerificationCodeOutcome();
                g_sentCode = result.first;
                SetStatus(result.second);
            }
        }
        else
        {
            if (ImGui::Button("Validate"))
            {
                if (EDreamClient::ValidateCode(std::string(g_codeBuf)))
                {
                    EDreamClient::DidSignIn();
                    g_sentCode = false;
                    SetStatus("Login successful.");
                }
                else
                {
                    SetStatus("Invalid code. Please try again.");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Start Again"))
            {
                g_sentCode = false;
                g_codeBuf[0] = '\0';
                SetStatus("Code flow restarted.");
            }
        }
    }
    else
    {
        if (ImGui::Button("Sign Out"))
        {
            EDreamClient::SignOut();
            g_sentCode = false;
            g_codeBuf[0] = '\0';
            SetStatus("Signed out.");
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Need an account? Create one"))
        PlatformUtils::OpenURLExternally(kUrlCreateAccount);
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
    ImGui::InputText("Content Folder", g_contentDirBuf, sizeof g_contentDirBuf);
    ImGui::Checkbox("Unlimited cache", &g_unlimitedCache);
    if (!g_unlimitedCache)
        ImGui::InputInt("Max disk space (GB)", &g_cacheSizeGb);
}

static void DrawDisplayTab()
{
    ImGui::InputDouble("Player FPS", &g_playerFps);
    ImGui::InputDouble("Display FPS", &g_displayFps);
    ImGui::Checkbox("Vertical Synchronization", &g_vsync);
    ImGui::Checkbox("Black Out Other Monitors In Fullscreen", &g_blackoutMonitors);
    ImGui::Checkbox("Preserve Aspect Ratio", &g_preserveAR);
}

static void DrawAdvancedTab()
{
    ImGui::Checkbox("Quiet Mode", &g_quietMode);
    ImGui::Checkbox("Show Attribution", &g_showAttribution);
    ImGui::Checkbox("Log Debug Messages", &g_debugLog);

    ImGui::Separator();
    ImGui::Checkbox("Use Proxy", &g_useProxy);
    ImGui::InputText("Proxy Host", g_proxyHostBuf, sizeof g_proxyHostBuf);
    ImGui::InputText("Proxy Login", g_proxyLoginBuf, sizeof g_proxyLoginBuf);
    ImGui::InputText("Proxy Password", g_proxyPasswordBuf, sizeof g_proxyPasswordBuf,
                     ImGuiInputTextFlags_Password);

    ImGui::Separator();
    ImGui::InputText("Server", g_serverBuf, sizeof g_serverBuf);
    ImGui::Checkbox("Install and update screensaver", &g_autoInstallScreensaver);
    ImGui::Checkbox("Keep screensaver enabled", &g_keepScreensaverEnabled);
}

static void DrawSettingsDialog(float viewportW, float viewportH)
{
    const ImVec2 windowSize(760.f, 560.f);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2((viewportW - windowSize.x) * 0.5f, (viewportH - windowSize.y) * 0.5f),
                            ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin("Settings", nullptr, flags))
    {
        ImGui::TextUnformatted("Windows Settings");
        ImGui::Separator();

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

        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(140.f, 0.f)))
        {
            SaveSettings();
            SetStatus("Saved. Restart may be required for some changes.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(140.f, 0.f)))
            CloseDialog();

        if (g_statusBuf[0] != '\0')
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", g_statusBuf);
        }
    }
    ImGui::End();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        CloseDialog();
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
