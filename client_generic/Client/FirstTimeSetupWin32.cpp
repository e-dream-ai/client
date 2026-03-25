#ifdef WIN32

#include "FirstTimeSetupWin32.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

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

std::atomic<bool> g_overlayAllowed{true};
std::atomic<bool> g_showRequested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_imguiInitialized{false};
/// Set when the user dismisses the wizard; context must not be destroyed inside NewFrame/Begin/End.
std::atomic<bool> g_pendingImGuiShutdown{false};
ImGuiContext* g_imguiContext = nullptr;
std::mutex g_jobMutex;
std::atomic<bool> g_sendBusy{false};
std::atomic<bool> g_sendDone{false};
bool g_sendOk = false;
std::string g_sendMessage;

std::atomic<bool> g_validateBusy{false};
std::atomic<bool> g_validateDone{false};
bool g_validateOk = false;

char g_emailBuf[256] = {};
char g_codeBuf[16] = {};
char g_errBuf[512] = {};
int g_wizardStep = 0;

static void OnFirstTimeSetupRequested()
{
    if (!g_overlayAllowed.load(std::memory_order_acquire))
        return;
    g_showRequested.store(true, std::memory_order_release);
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

static int FilterDigitsOnly(ImGuiInputTextCallbackData* data)
{
    if (data->EventChar >= '0' && data->EventChar <= '9')
        return 0;
    return 1;
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

static void ResetWizardForShow()
{
    g_wizardStep = 0;
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
            std::strncpy(g_errBuf, g_sendMessage.c_str(), sizeof g_errBuf - 1);
            g_errBuf[sizeof g_errBuf - 1] = '\0';
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
            std::strncpy(g_errBuf, "Invalid verification code. Please try again.", sizeof g_errBuf - 1);
            g_errBuf[sizeof g_errBuf - 1] = '\0';
        }
    }
}

static void DrawWizard()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    const ImVec2 winSize(480.f, 360.f);
    ImGui::SetNextWindowPos(ImVec2((display.x - winSize.x) * 0.5f, (display.y - winSize.y) * 0.5f));
    ImGui::SetNextWindowSize(winSize);
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;

    ImGui::Begin("Welcome to infinidream", nullptr, wflags);

    if (g_wizardStep == 0)
    {
        ImGui::TextUnformatted("Sign in with your email. We will send you a verification code.");
        ImGui::Spacing();
        ImGui::InputText("Email", g_emailBuf, sizeof g_emailBuf);
        if (g_errBuf[0] != '\0')
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", g_errBuf);

        const bool busy = g_sendBusy.load(std::memory_order_acquire);
        if (busy)
            ImGui::BeginDisabled();
        if (ImGui::Button("Send code", ImVec2(-1, 0)) && !busy)
        {
            if (!IsLikelyEmail(g_emailBuf))
            {
                std::strncpy(g_errBuf, "Please enter a valid email address.", sizeof g_errBuf - 1);
                g_errBuf[sizeof g_errBuf - 1] = '\0';
            }
            else
            {
                g_errBuf[0] = '\0';
                g_Settings()->Set("settings.generator.nickname", std::string(g_emailBuf));
                g_Settings()->Storage()->Commit();
                g_sendBusy.store(true, std::memory_order_release);
                std::thread([]() {
                    auto result = EDreamClient::SendVerificationCodeOutcome();
                    {
                        std::lock_guard<std::mutex> lock(g_jobMutex);
                        g_sendOk = result.first;
                        g_sendMessage = std::move(result.second);
                    }
                    g_sendDone.store(true, std::memory_order_release);
                    g_sendBusy.store(false, std::memory_order_release);
                }).detach();
            }
        }
        if (busy)
        {
            ImGui::EndDisabled();
            ImGui::TextUnformatted("Sending...");
        }
    }
    else if (g_wizardStep == 1)
    {
        ImGui::TextUnformatted("Enter the 6-digit code from your email.");
        ImGui::Spacing();
        ImGui::InputText("Code", g_codeBuf, sizeof g_codeBuf,
                         ImGuiInputTextFlags_CallbackCharFilter, FilterDigitsOnly);
        StripNonDigits(g_codeBuf, sizeof g_codeBuf);
        if (g_errBuf[0] != '\0')
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "%s", g_errBuf);

        const bool busy = g_validateBusy.load(std::memory_order_acquire);
        const bool canVerify = std::strlen(g_codeBuf) == 6 && !busy;
        if (!canVerify || busy)
            ImGui::BeginDisabled();
        if (ImGui::Button("Verify", ImVec2(-1, 0)) && canVerify && !busy)
        {
            g_errBuf[0] = '\0';
            std::string codeCopy(g_codeBuf);
            g_validateBusy.store(true, std::memory_order_release);
            std::thread([code = std::move(codeCopy)]() {
                bool ok = EDreamClient::ValidateCode(code);
                g_validateOk = ok;
                g_validateDone.store(true, std::memory_order_release);
                g_validateBusy.store(false, std::memory_order_release);
            }).detach();
        }
        if (!canVerify || busy)
            ImGui::EndDisabled();

        if (busy)
            ImGui::TextUnformatted("Verifying...");

        ImGui::Spacing();
        if (ImGui::Button("Use a different email"))
        {
            g_wizardStep = 0;
            g_codeBuf[0] = '\0';
            g_errBuf[0] = '\0';
        }
    }
    else
    {
        ImGui::TextUnformatted("You are signed in. Enjoy infinidream.");
        ImGui::Spacing();
        if (ImGui::Button("Continue", ImVec2(-1, 0)))
        {
            g_visible.store(false, std::memory_order_release);
            g_pendingImGuiShutdown.store(true, std::memory_order_release);
        }
    }

    ImGui::End();
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
    }
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
        }
    }

    if (!g_visible.load(std::memory_order_acquire) || !g_imguiInitialized.load(std::memory_order_acquire))
        return false;

    PollWorkerResults();

    ImGui::SetCurrentContext(g_imguiContext);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
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
        ShutdownImGui();

    return true;
}

#endif // WIN32
