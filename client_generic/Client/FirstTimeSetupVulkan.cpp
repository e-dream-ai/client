#if !defined(WIN32) && !defined(MAC)

#include "FirstTimeSetupVulkan.h"
#include "ServerConfig.h"
#include "EDreamClient.h"
#include "PlatformUtils.h"
#include "Player.h"
#include "Settings.h"
#include "storage.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <imgui.h>

#ifdef HAVE_WAYLAND
#include <xkbcommon/xkbcommon.h>
#endif

typedef void (*ShowFirstTimeSetupCallback_t)();
extern void ESSetShowFirstTimeSetupCallback(ShowFirstTimeSetupCallback_t);

namespace {

#ifdef STAGE
constexpr const char* kUrlCreateAccount = "https://stage.infinidream.ai/account";
constexpr const char* kUrlWebRemote     = "https://stage.infinidream.ai/rc";
constexpr const char* kUrlPlaylists     = "https://stage.infinidream.ai/playlists";
#else
constexpr const char* kUrlCreateAccount = "https://alpha.infinidream.ai/account";
constexpr const char* kUrlWebRemote     = "https://alpha.infinidream.ai/rc";
constexpr const char* kUrlPlaylists     = "https://alpha.infinidream.ai/playlists";
#endif

// Design dimensions match the Windows/macOS wizard for visual consistency.
constexpr float kDesignWinW = 879.f;
constexpr float kDesignWinH = 580.f;
constexpr float kLogoDisplay = 120.f;

constexpr float kEmailStepPanelW  = 571.f;
constexpr float kEmailStepPanelH  = 268.f;
constexpr float kCodeStepPanelW   = 509.f;
constexpr float kCodeStepPanelH   = 250.f;
constexpr float kEmailPromptL     = 45.f;
constexpr float kEmailMarginL     = 47.f;
constexpr float kEmailFieldW      = 364.f;
constexpr float kEmailSendBtnW    = 106.f;
constexpr float kEmailCreateAccountW = 235.f;
constexpr float kEmailPromptOffsetY  = 30.f;
constexpr float kEmailRowOffsetY     = 115.f;
constexpr float kEmailErrorOffsetY   = 151.f;
constexpr float kEmailCreateBtnOffsetY = 172.f;
constexpr float kCodeInstrOffsetY  = 16.f;
constexpr float kCodeOtpOffsetY    = 90.f;
constexpr float kCodeErrorOffsetY  = 144.f;
constexpr float kCodeVerifyW       = 110.f;
constexpr float kCodeVerifyOffsetY = 158.f;
constexpr float kCodeTryAgainW     = 88.f;
constexpr float kCodeTryAgainOffsetY = 200.f;

constexpr float kThanksPanelW      = 667.f;
constexpr float kThanksOuterPad    = 20.f;
constexpr float kThanksColGap      = 20.f;
constexpr float kThanksTitleTop    = 2.f;
constexpr float kThanksTitleToGrid = 8.f;
constexpr float kThanksRowTopH     = 110.f;
constexpr float kThanksRowBotH     = 92.f;
constexpr float kThanksHSepGap     = 5.f;
constexpr float kThanksHSepH       = 1.f;
constexpr float kThanksCellPad     = 6.f;
constexpr float kThanksImgTop      = 10.f;
constexpr float kThanksImgTrail    = 6.f;
constexpr float kThanksTextImgGap  = 12.f;
constexpr float kThanksPlaylistImg = 52.f;
constexpr float kThanksBtnBottom   = 6.f;
constexpr float kThanksBottomPad   = 4.f;
constexpr float kThanksOpenRemoteBtnW   = 118.f;
constexpr float kThanksOpenPlaylistBtnW = 136.f;
constexpr float kThanksTipsBtnH    = 28.f;
constexpr float kThanksDreamOnMinW = 82.f;
constexpr float kThanksCopyFontSize = 17.f;
constexpr float kThanksBtnFontSize  = 15.f;

static constexpr ImVec4 kMacInputBorderFocus(0.f, 0.478f, 1.f, 1.f);
static constexpr ImVec4 kMacInputBorderIdle(0.74f, 0.74f, 0.76f, 1.f);
static constexpr ImVec4 kMacAccentBtn(0.f, 0.478f, 1.f, 1.f);
static constexpr ImVec4 kMacAccentBtnHov(0.10f, 0.54f, 1.f, 1.f);
static constexpr ImVec4 kMacAccentBtnAct(0.f, 0.40f, 0.88f, 1.f);
static constexpr ImVec4 kMacAccentBtnBorder(0.f, 0.38f, 0.82f, 1.f);

// On Linux we use a 1:1 UI scale (no DPI scaling applied yet).
static float g_uiScale = 1.0f;
static inline float S(float v) { return v * g_uiScale; }

std::atomic<bool> g_showRequested{false};
std::atomic<bool> g_visible{false};
std::atomic<bool> g_fontsLoaded{false};
std::atomic<bool> g_wasPausedBeforeWizard{false};
std::atomic<bool> g_pausedByWizard{false};

ImFont* g_fontBody       = nullptr;
ImFont* g_fontTitle      = nullptr;
ImFont* g_fontHeadline   = nullptr;
ImFont* g_fontOtp        = nullptr;
ImFont* g_fontThanksCopy = nullptr;
ImFont* g_fontThanksBtn  = nullptr;

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
char g_codeBuf[16]   = {};
char g_errBuf[512]   = {};
int  g_wizardStep    = 0;

bool g_emailFieldBorderActive = false;
bool g_otpFieldBorderActive   = false;

// --- Helpers -----------------------------------------------------------------

static bool IsLikelyEmail(const char* s)
{
    if (!s || !*s) return false;
    const char* at  = std::strchr(s, '@');
    if (!at || at == s) return false;
    const char* dot = std::strchr(at + 1, '.');
    return dot != nullptr && dot > at + 1 && std::strlen(dot) > 1;
}

static int FilterDigitsOnly(ImGuiInputTextCallbackData* data)
{
    constexpr int kMaxDigits = 6;
    if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
    {
        if (data->EventChar < '0' || data->EventChar > '9') return 1;
        if (data->BufTextLen >= kMaxDigits) return 1;
        return 0;
    }
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        int w = 0;
        for (int r = 0; r < data->BufTextLen && data->Buf[r] != '\0'; ++r)
        {
            const char c = data->Buf[r];
            if (c >= '0' && c <= '9')
            {
                data->Buf[w++] = c;
                if (w >= kMaxDigits) break;
            }
        }
        data->Buf[w]    = '\0';
        data->BufTextLen = w;
        data->BufDirty  = true;
        return 0;
    }
    return 0;
}

static void StripNonDigits(char* buf, size_t bufSize)
{
    size_t w = 0;
    for (size_t r = 0; buf[r] != '\0' && w + 1 < bufSize; ++r)
        if (buf[r] >= '0' && buf[r] <= '9') buf[w++] = buf[r];
    buf[w] = '\0';
    if (w > 6) buf[6] = '\0';
}

struct AuthDialogContent { const char* title; std::string message; };

static AuthDialogContent BuildSendCodeFailureDialog(const EDreamClient::SendCodeResult& r)
{
    if (r.httpCode >= 400 && r.httpCode < 500)
    {
        std::string msg = "We couldn't send a verification email. Make sure your email address is correct, then try Send code again.";
        if (!r.message.empty()) msg += "\n\n" + r.message;
        return {"Unable to send code", msg};
    }
    if (r.httpCode >= 500)
    {
        std::string msg = "Try again later.";
        if (!r.message.empty()) msg += " " + r.message;
        return {"Server Error", msg};
    }
    return {"Authentication Error",
            r.message.empty() ? "Failed to send verification code." : r.message};
}

static AuthDialogContent BuildValidateFailureDialog(const EDreamClient::ValidateCodeResult& r)
{
    if (r.httpCode >= 400 && r.httpCode < 500)
        return {"Invalid Code", "Check for typos and check to be sure you have the most recent code. Try again or start over"};
    if (r.httpCode >= 500)
    {
        std::string msg = "Try again later.";
        if (!r.message.empty()) msg += " " + r.message;
        return {"Server Error", msg};
    }
    return {"Authentication Error",
            r.message.empty() ? "Backend is temporarily unavailable. Please try again shortly." : r.message};
}

// Inline error inside the wizard (no separate modal on Linux — just update g_errBuf).
static void ShowAuthWarning(const AuthDialogContent& content)
{
    std::snprintf(g_errBuf, sizeof g_errBuf, "%s", content.message.c_str());
}

// --- Font loading ------------------------------------------------------------

static std::string FindSystemFont(const char* name)
{
    // Search common Linux font directories in order of likelihood.
    static const char* kDirs[] = {
        "/usr/share/fonts/TTF/",
        "/usr/share/fonts/truetype/",
        "/usr/share/fonts/noto/",
        "/usr/share/fonts/truetype/noto/",
        "/usr/share/fonts/truetype/freefont/",
        "/usr/share/fonts/truetype/liberation/",
        nullptr,
    };
    for (int i = 0; kDirs[i]; ++i)
    {
        std::string path = kDirs[i];
        path += name;
        if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fclose(f); return path; }
    }
    return {};
}

static void LoadWizardFonts()
{
    if (g_fontsLoaded.load(std::memory_order_acquire)) return;

    ImGuiIO& io = ImGui::GetIO();

    // Try Noto Sans (common on modern Linux), then fall back to other sans-serif fonts.
    static const char* kCandidates[] = {
        "NotoSans-Regular.ttf",
        "NotoSans[wdth,wght].ttf",
        "DejaVuSans.ttf",
        "LiberationSans-Regular.ttf",
        "FreeSans.ttf",
        nullptr,
    };
    static const char* kBoldCandidates[] = {
        "NotoSans-Bold.ttf",
        "NotoSans[wdth,wght].ttf",
        "DejaVuSans-Bold.ttf",
        "LiberationSans-Bold.ttf",
        "FreeSansBold.ttf",
        nullptr,
    };

    std::string regular, bold;
    for (int i = 0; kCandidates[i] && regular.empty(); ++i)
        regular = FindSystemFont(kCandidates[i]);
    for (int i = 0; kBoldCandidates[i] && bold.empty(); ++i)
        bold = FindSystemFont(kBoldCandidates[i]);

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;

    if (!regular.empty())
    {
        g_fontBody       = io.Fonts->AddFontFromFileTTF(regular.c_str(), S(15.f), &cfg);
        g_fontTitle      = io.Fonts->AddFontFromFileTTF(regular.c_str(), S(28.f), &cfg);
        g_fontOtp        = io.Fonts->AddFontFromFileTTF(regular.c_str(), S(36.f), &cfg);
        g_fontThanksCopy = io.Fonts->AddFontFromFileTTF(regular.c_str(), S(kThanksCopyFontSize), &cfg);
    }

    const std::string& headlinePath = bold.empty() ? regular : bold;
    if (!headlinePath.empty())
    {
        g_fontHeadline  = io.Fonts->AddFontFromFileTTF(headlinePath.c_str(), S(32.f), &cfg);
        g_fontThanksBtn = io.Fonts->AddFontFromFileTTF(headlinePath.c_str(), S(kThanksBtnFontSize), &cfg);
    }

    if (g_fontBody) io.FontDefault = g_fontBody;

    g_fontsLoaded.store(true, std::memory_order_release);
}

// --- Style -------------------------------------------------------------------

static void ApplyLightSheetStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsLight(&s);
    s.WindowRounding  = 8.f;
    s.ChildRounding   = 6.f;
    s.FrameRounding   = 5.f;
    s.PopupRounding   = 6.f;
    s.ScrollbarRounding = 8.f;
    s.GrabRounding    = 4.f;
    s.WindowBorderSize = 1.f;
    s.FrameBorderSize  = 1.f;
    s.WindowPadding   = ImVec2(14.f, 14.f);
    s.ItemSpacing     = ImVec2(10.f, 8.f);
    s.CellPadding     = ImVec2(6.f, 4.f);
    s.FramePadding    = ImVec2(10.f, 8.f);

    ImVec4 text(0.10f, 0.11f, 0.13f, 1.f);
    ImVec4 winBg(0.965f, 0.967f, 0.975f, 0.985f);
    ImVec4 btn(1.f, 1.f, 1.f, 1.f);
    ImVec4 btnHov(0.96f, 0.96f, 0.98f, 1.f);
    ImVec4 btnAct(0.90f, 0.90f, 0.93f, 1.f);
    ImVec4 borderChrome(0.74f, 0.74f, 0.76f, 1.f);

    s.Colors[ImGuiCol_Text]           = text;
    s.Colors[ImGuiCol_TextDisabled]   = ImVec4(0.45f, 0.47f, 0.50f, 1.f);
    s.Colors[ImGuiCol_WindowBg]       = winBg;
    s.Colors[ImGuiCol_ChildBg]        = winBg;
    s.Colors[ImGuiCol_Border]         = borderChrome;
    s.Colors[ImGuiCol_BorderShadow]   = ImVec4(0.f, 0.f, 0.f, 0.f);
    s.Colors[ImGuiCol_FrameBg]        = ImVec4(1.f, 1.f, 1.f, 1.f);
    s.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.98f, 0.98f, 0.99f, 1.f);
    s.Colors[ImGuiCol_FrameBgActive]  = ImVec4(1.f, 1.f, 1.f, 1.f);
    s.Colors[ImGuiCol_Button]         = btn;
    s.Colors[ImGuiCol_ButtonHovered]  = btnHov;
    s.Colors[ImGuiCol_ButtonActive]   = btnAct;
    s.Colors[ImGuiCol_Header]         = ImVec4(0.86f, 0.89f, 0.97f, 1.f);
    s.Colors[ImGuiCol_ScrollbarBg]    = ImVec4(0.93f, 0.94f, 0.96f, 0.85f);
    s.Colors[ImGuiCol_NavHighlight]   = ImVec4(0.f, 0.478f, 1.f, 0.55f);
}

// --- Playback pause management -----------------------------------------------

static void ApplyWizardPauseState(bool visible)
{
    if (visible)
    {
        g_Player().SetFirstRunWizardPlaybackHold(true);
        const bool wasPaused = g_Player().IsPaused();
        g_wasPausedBeforeWizard.store(wasPaused, std::memory_order_release);
        if (!wasPaused)
        {
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
    g_Player().SetPausedForBuffering(false);
}

// --- Wizard state reset ------------------------------------------------------

static void ResetWizardForShow()
{
    g_wizardStep             = 0;
    g_emailFieldBorderActive = false;
    g_otpFieldBorderActive   = false;
    g_codeBuf[0]             = '\0';
    g_errBuf[0]              = '\0';
    std::string existing = g_Settings()->Get("settings.generator.nickname", std::string());
    std::strncpy(g_emailBuf, existing.c_str(), sizeof g_emailBuf - 1);
    g_emailBuf[sizeof g_emailBuf - 1] = '\0';
}

// --- Async worker result polling ---------------------------------------------

static void PollWorkerResults()
{
    if (g_sendDone.exchange(false))
    {
        std::lock_guard<std::mutex> lock(g_jobMutex);
        if (g_sendOk)
        {
            g_wizardStep = 1;
            g_errBuf[0]  = '\0';
        }
        else
        {
            const AuthDialogContent dlg = BuildSendCodeFailureDialog(g_sendResult);
            ShowAuthWarning(dlg);
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
            g_errBuf[0]  = '\0';
        }
        else
        {
            const AuthDialogContent dlg = BuildValidateFailureDialog(g_validateResult);
            ShowAuthWarning(dlg);
            g_codeBuf[0] = '\0';
        }
    }
}

// --- Drawing -----------------------------------------------------------------

static void DrawSkipFooter()
{
    if (g_wizardStep >= 2) return;

    const float skipW  = S(80.f);
    const float padY   = ImGui::GetStyle().WindowPadding.y;
    const float skipH  = S(36.f);
    float y = ImGui::GetWindowHeight() - padY - skipH;
    if (y < 0.f) y = ImGui::GetCursorPosY();
    const float x = ImGui::GetCursorStartPos().x + ImGui::GetContentRegionAvail().x - skipW;
    ImGui::SetCursorPos(ImVec2(x, y));

    if (ImGui::Button("Skip", ImVec2(skipW, skipH)))
    {
        g_visible.store(false, std::memory_order_release);
        ApplyWizardPauseState(false);
    }
}

static void DrawWizard()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display       = io.DisplaySize;
    const ImVec2 dialogPadding = ImGui::GetStyle().WindowPadding;

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
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.35f));
    ImGui::Begin("##FirstTimeOverlayHost", nullptr, hostFlags);

    ImGui::SetCursorPos(ImVec2(panelX, panelY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, dialogPadding);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, sheetBg);
    ImGui::BeginChild("Welcome to Infinidream", panelSize, true, ImGuiWindowFlags_NoScrollbar);

    // Header
    {
        const float logoSz   = S(kLogoDisplay);
        const float hdrH     = logoSz + S(11.f) + S(8.f);
        const float winInnerW = ImGui::GetWindowWidth();

        if (g_fontHeadline) ImGui::PushFont(g_fontHeadline);
        const char* headline = "Welcome to Infinidream";
        const ImVec2 ts = ImGui::CalcTextSize(headline);
        ImGui::SetCursorPos(ImVec2((winInnerW - ts.x) * 0.5f, S(50.f)));
        ImGui::TextUnformatted(headline);
        if (g_fontHeadline) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(0.f, hdrH));
    }

    const ImGuiStyle& sheetSt = ImGui::GetStyle();
    const float skipReserve = (g_wizardStep < 2)
                                  ? (sheetSt.WindowPadding.y + S(36.f) + S(4.f))
                                  : (sheetSt.WindowPadding.y + S(8.f));
    float bodyH = ImGui::GetContentRegionAvail().y - skipReserve;
    if (bodyH < 1.f) bodyH = 1.f;

    ImGuiWindowFlags bodyChildFlags =
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("body", ImVec2(0, bodyH), ImGuiChildFlags_None, bodyChildFlags);

    // ---- Step 0: Email -------------------------------------------------------
    if (g_wizardStep == 0)
    {
        g_otpFieldBorderActive = false;
        const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(0.f, S(20.f)));
        float stepPadX = (bodyAvail.x - S(kEmailStepPanelW)) * 0.5f;
        if (stepPadX > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + stepPadX);
        ImGui::BeginChild("emailStep", ImVec2(S(kEmailStepPanelW), S(kEmailStepPanelH)),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImGui::SetCursorPos(ImVec2(S(kEmailPromptL), S(kEmailPromptOffsetY)));
        if (g_fontTitle) ImGui::PushFont(g_fontTitle);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(473.f));
        ImGui::TextUnformatted("Enter your email to sign in:");
        ImGui::PopTextWrapPos();
        if (g_fontTitle) ImGui::PopFont();

        ImGui::SetCursorPos(ImVec2(S(kEmailMarginL), S(kEmailRowOffsetY)));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(10.f)));
        ImGui::PushItemWidth(S(kEmailFieldW));
        bool emailEnter = false;
        {
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  g_emailFieldBorderActive ? kMacInputBorderFocus : kMacInputBorderIdle);
            emailEnter = ImGui::InputTextWithHint("##email", "your@email.com", g_emailBuf, sizeof g_emailBuf,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleColor();
            g_emailFieldBorderActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
        }
        ImGui::PopItemWidth();
        ImGui::PopStyleVar();
        ImGui::SameLine(0.f, S(10.f));

        const bool busy = g_sendBusy.load(std::memory_order_acquire);
        const char* sendBtnText = busy ? "Sending..." : "Send code";
        const float sendBtnH    = S(36.f);
        ImVec2 sendBtnMin = ImGui::GetCursorScreenPos();

        ImGui::PushStyleColor(ImGuiCol_Button,        kMacAccentBtn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kMacAccentBtnHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kMacAccentBtnAct);
        ImGui::PushStyleColor(ImGuiCol_Border,        kMacAccentBtnBorder);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.f, 1.f, 1.f, 1.f));
        const bool clicked = ImGui::Button("##send_code_btn", ImVec2(S(kEmailSendBtnW), sendBtnH));
        ImGui::PopStyleColor(5);

        if (!busy && (clicked || emailEnter))
        {
            const size_t elen = std::strlen(g_emailBuf);
            if (elen == 0)
                std::snprintf(g_errBuf, sizeof g_errBuf, "Please enter your email address");
            else if (!IsLikelyEmail(g_emailBuf))
                std::snprintf(g_errBuf, sizeof g_errBuf, "Please enter a valid email address");
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
                        g_sendOk     = result.success;
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
        // Draw button label centered (button uses "##" id to hide ImGui's label)
        {
            const ImVec2 ts = ImGui::CalcTextSize(sendBtnText);
            const ImVec2 textPos(sendBtnMin.x + (S(kEmailSendBtnW) - ts.x) * 0.5f,
                                 sendBtnMin.y + (sendBtnH - ts.y) * 0.5f);
            ImGui::GetWindowDrawList()->AddText(
                textPos, ImGui::ColorConvertFloat4ToU32(ImVec4(1.f, 1.f, 1.f, 1.f)), sendBtnText);
        }

        ImGui::SetCursorPos(ImVec2((S(kEmailStepPanelW) - S(kEmailCreateAccountW)) * 0.5f,
                                   S(kEmailCreateBtnOffsetY)));
        if (ImGui::Button("Need an account? Create one", ImVec2(S(kEmailCreateAccountW), S(36.f))))
            PlatformUtils::OpenURLExternally(kUrlCreateAccount);

        ImGui::EndChild(); // emailStep
    }
    // ---- Step 1: OTP --------------------------------------------------------
    else if (g_wizardStep == 1)
    {
        g_emailFieldBorderActive = false;
        const ImVec2 bodyAvail = ImGui::GetContentRegionAvail();
        ImGui::Dummy(ImVec2(0.f, S(6.f)));
        float stepPadX = (bodyAvail.x - S(kCodeStepPanelW)) * 0.5f;
        if (stepPadX > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + stepPadX);
        ImGui::BeginChild("codeStep", ImVec2(S(kCodeStepPanelW), S(kCodeStepPanelH)),
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        ImGui::SetCursorPos(ImVec2(S(38.f), S(kCodeInstrOffsetY)));
        if (g_fontTitle) ImGui::PushFont(g_fontTitle);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(434.f));
        ImGui::TextUnformatted("Check your email for a one-time code, and enter it below.");
        ImGui::PopTextWrapPos();
        if (g_fontTitle) ImGui::PopFont();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.f), S(10.f)));
        if (g_fontOtp) ImGui::PushFont(g_fontOtp);

        const float otpTextW   = ImGui::CalcTextSize("000000").x;
        const float otpInputW  = otpTextW + ImGui::GetStyle().FramePadding.x * 2.f;
        ImGui::SetCursorPos(ImVec2((S(kCodeStepPanelW) - otpInputW) * 0.5f, S(kCodeOtpOffsetY)));
        ImGui::PushItemWidth(otpInputW);
        bool otpEnter = false;
        {
            ImGui::PushStyleColor(ImGuiCol_Border,
                                  g_otpFieldBorderActive ? kMacInputBorderFocus : kMacInputBorderIdle);
            otpEnter = ImGui::InputTextWithHint("##otp", "000000", g_codeBuf, sizeof g_codeBuf,
                                                ImGuiInputTextFlags_CallbackCharFilter |
                                                    ImGuiInputTextFlags_CallbackEdit |
                                                    ImGuiInputTextFlags_EnterReturnsTrue,
                                                FilterDigitsOnly);
            ImGui::PopStyleColor();
            g_otpFieldBorderActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
        }
        if (g_fontOtp) ImGui::PopFont();
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
        if (!canVerify || busy) ImGui::BeginDisabled();
        const bool verifyClicked = ImGui::Button("Verify code", ImVec2(S(kCodeVerifyW), S(36.f)));
        if ((verifyClicked || otpEnter) && canVerify && !busy)
        {
            g_errBuf[0] = '\0';
            std::string codeCopy(g_codeBuf);
            g_validateBusy.store(true, std::memory_order_release);
            std::thread([code = std::move(codeCopy)]() {
                EDreamClient::ValidateCodeResult result = EDreamClient::ValidateCodeDetailed(code);
                g_validateOk     = result.success;
                g_validateResult = std::move(result);
                g_validateDone.store(true, std::memory_order_release);
                g_validateBusy.store(false, std::memory_order_release);
            }).detach();
        }
        if (!canVerify || busy) ImGui::EndDisabled();

        if (busy)
        {
            ImGui::SetCursorPos(ImVec2(S(kCodeStepPanelW) * 0.5f + S(kCodeVerifyW) * 0.5f + S(12.f),
                                       S(kCodeVerifyOffsetY) + S(6.f)));
            ImGui::TextDisabled("Verifying...");
        }

        ImGui::SetCursorPos(ImVec2((S(kCodeStepPanelW) - S(kCodeTryAgainW)) * 0.5f,
                                   S(kCodeTryAgainOffsetY)));
        if (ImGui::Button("Try again", ImVec2(S(kCodeTryAgainW), S(36.f))))
        {
            g_wizardStep = 0;
            g_codeBuf[0] = '\0';
            g_errBuf[0]  = '\0';
        }

        ImGui::EndChild(); // codeStep
    }
    // ---- Step 2: Thanks / tips ----------------------------------------------
    else
    {
        g_emailFieldBorderActive = false;
        g_otpFieldBorderActive   = false;

        const float bodyAvailW = ImGui::GetContentRegionAvail().x;
        const float panelW     = bodyAvailW < S(kThanksPanelW) ? bodyAvailW : S(kThanksPanelW);
        const float centerPad  = (bodyAvailW - panelW) * 0.5f;
        if (centerPad > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centerPad);

        const float innerW = panelW - 2.f * S(kThanksOuterPad);
        const float colW   = (innerW - S(kThanksColGap)) * 0.5f;

        if (g_fontTitle) ImGui::PushFont(g_fontTitle);
        const char* tipsTitle = "All set! Quick tips:";
        const ImVec2 tipsTs   = ImGui::CalcTextSize(tipsTitle);
        if (g_fontTitle) ImGui::PopFont();

        const float panelH = S(kThanksTitleTop) + tipsTs.y + S(kThanksTitleToGrid) +
                             S(kThanksRowTopH) + S(kThanksHSepGap) + S(kThanksHSepH) +
                             S(kThanksHSepGap) + S(kThanksRowBotH) + S(kThanksBottomPad);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::BeginChild("thanksPanel", ImVec2(panelW, panelH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);

        ImGui::Dummy(ImVec2(0.f, S(kThanksTitleTop)));
        if (g_fontTitle) ImGui::PushFont(g_fontTitle);
        {
            const float tipX = (panelW - tipsTs.x) * 0.5f;
            if (tipX > 0.f) ImGui::SetCursorPosX(tipX);
            ImGui::TextUnformatted(tipsTitle);
        }
        if (g_fontTitle) ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.f, S(kThanksTitleToGrid)));

        const float    yTop    = ImGui::GetCursorPosY();
        ImDrawList*    panelDl = ImGui::GetWindowDrawList();
        const ImU32    sepU32  = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Border]);

        // Top-left cell
        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad), yTop));
        ImGui::BeginChild("tl", ImVec2(colW, S(kThanksRowTopH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            if (g_fontThanksCopy) ImGui::PushFont(g_fontThanksCopy);
            ImGui::SetCursorPos(ImVec2(S(kThanksCellPad), S(kThanksCellPad)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + colW - 2.f * S(kThanksCellPad));
            ImGui::TextWrapped(
                "Use the A and D keys to adjust the speed of playback. Press F1 to see more keyboard "
                "controls. You can also interact with the remote control from a web browser:");
            ImGui::PopTextWrapPos();
            if (g_fontThanksCopy) ImGui::PopFont();

            const float btnY = S(kThanksRowTopH) - S(kThanksBtnBottom) - S(kThanksTipsBtnH);
            ImGui::SetCursorPos(ImVec2((colW - S(kThanksOpenRemoteBtnW)) * 0.5f, btnY));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            if (g_fontThanksBtn) ImGui::PushFont(g_fontThanksBtn);
            const bool openRemote =
                ImGui::Button("Open web remote", ImVec2(S(kThanksOpenRemoteBtnW), S(kThanksTipsBtnH)));
            if (g_fontThanksBtn) ImGui::PopFont();
            ImGui::PopStyleVar();
            if (openRemote) PlatformUtils::OpenURLExternally(kUrlWebRemote);
        }
        ImGui::EndChild();
        const ImVec2 tlRectMin = ImGui::GetItemRectMin();
        const ImVec2 tlRectMax = ImGui::GetItemRectMax();

        // Top-right cell
        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad) + colW + S(kThanksColGap), yTop));
        ImGui::BeginChild("tr", ImVec2(colW, S(kThanksRowTopH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            if (g_fontThanksCopy) ImGui::PushFont(g_fontThanksCopy);
            ImGui::SetCursorPos(ImVec2(S(kThanksCellPad), S(kThanksCellPad)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + colW - 2.f * S(kThanksCellPad));
            ImGui::TextWrapped(
                "Change your dreams by selecting a playlist from the browser. Click the button on a "
                "thumbnail to start that playlist.");
            ImGui::PopTextWrapPos();
            if (g_fontThanksCopy) ImGui::PopFont();

            const float btnY = S(kThanksRowTopH) - S(kThanksBtnBottom) - S(kThanksTipsBtnH);
            ImGui::SetCursorPos(ImVec2((colW - S(kThanksOpenPlaylistBtnW)) * 0.5f, btnY));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            if (g_fontThanksBtn) ImGui::PushFont(g_fontThanksBtn);
            const bool openPl =
                ImGui::Button("Open playlist browser", ImVec2(S(kThanksOpenPlaylistBtnW), S(kThanksTipsBtnH)));
            if (g_fontThanksBtn) ImGui::PopFont();
            ImGui::PopStyleVar();
            if (openPl) PlatformUtils::OpenURLExternally(kUrlPlaylists);
        }
        ImGui::EndChild();

        // Vertical separator between top cells
        const float vSepX = tlRectMax.x + S(10.f);
        panelDl->AddRectFilled(ImVec2(vSepX, tlRectMin.y),
                               ImVec2(vSepX + 1.f, tlRectMin.y + S(kThanksRowTopH)), sepU32);

        // Horizontal separator
        const float hSepTop = tlRectMax.y + S(kThanksHSepGap);
        panelDl->AddRectFilled(ImVec2(tlRectMin.x, hSepTop),
                               ImVec2(tlRectMin.x + innerW, hSepTop + S(kThanksHSepH)), sepU32);

        const float yBot = yTop + S(kThanksRowTopH) + S(kThanksHSepGap) + S(kThanksHSepH) + S(kThanksHSepGap);

        // Bottom-left cell
        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad), yBot));
        ImGui::BeginChild("bl", ImVec2(colW, S(kThanksRowBotH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            static const char kBlTips[] =
                "Press F to toggle fullscreen mode (or use the --fullscreen flag at launch). "
                "Press Ctrl+R to open the remote control in a browser.";
            const float wrapPx = colW - 2.f * S(kThanksCellPad);
            if (g_fontThanksCopy) ImGui::PushFont(g_fontThanksCopy);
            const ImVec2 wrapped = ImGui::CalcTextSize(kBlTips, nullptr, false, wrapPx);
            if (g_fontThanksCopy) ImGui::PopFont();
            float yText = (S(kThanksRowBotH) - wrapped.y) * 0.5f;
            if (yText < S(kThanksCellPad)) yText = S(kThanksCellPad);
            ImGui::SetCursorPos(ImVec2(S(kThanksCellPad), yText));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapPx);
            if (g_fontThanksCopy) ImGui::PushFont(g_fontThanksCopy);
            ImGui::TextWrapped("%s", kBlTips);
            if (g_fontThanksCopy) ImGui::PopFont();
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        const ImVec2 blRectMin = ImGui::GetItemRectMin();

        // Bottom-right cell: Dream on! button
        ImGui::SetCursorPos(ImVec2(S(kThanksOuterPad) + colW + S(kThanksColGap), yBot));
        ImGui::BeginChild("br", ImVec2(colW, S(kThanksRowBotH)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            if (g_fontThanksBtn) ImGui::PushFont(g_fontThanksBtn);
            float dreamW = ImGui::CalcTextSize("Dream on!").x + ImGui::GetStyle().FramePadding.x * 2.f +
                           ImGui::GetStyle().FrameBorderSize * 2.f;
            if (g_fontThanksBtn) ImGui::PopFont();
            ImGui::PopStyleVar();
            if (dreamW < S(kThanksDreamOnMinW)) dreamW = S(kThanksDreamOnMinW);

            const float bx = (colW - dreamW) * 0.5f;
            const float by = (S(kThanksRowBotH) - S(kThanksTipsBtnH)) * 0.5f;
            ImGui::SetCursorPos(ImVec2(bx, by));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8.f), S(3.f)));
            ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(1.f, 1.f, 1.f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_Button,        kMacAccentBtn);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kMacAccentBtnHov);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kMacAccentBtnAct);
            ImGui::PushStyleColor(ImGuiCol_Border,        kMacAccentBtnBorder);
            if (g_fontThanksBtn) ImGui::PushFont(g_fontThanksBtn);
            const bool dreamOn = ImGui::Button("Dream on!", ImVec2(dreamW, S(kThanksTipsBtnH)));
            if (g_fontThanksBtn) ImGui::PopFont();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar();
            if (dreamOn)
            {
                g_visible.store(false, std::memory_order_release);
                ApplyWizardPauseState(false);
            }
        }
        ImGui::EndChild();

        // Vertical separator between bottom cells
        panelDl->AddRectFilled(ImVec2(vSepX, blRectMin.y),
                               ImVec2(vSepX + 1.f, blRectMin.y + S(kThanksRowBotH)), sepU32);

        ImGui::EndChild(); // thanksPanel
        ImGui::PopStyleVar(); // WindowPadding 0,0
    }

    ImGui::EndChild(); // body
    DrawSkipFooter();
    ImGui::EndChild(); // "Welcome to Infinidream"
    ImGui::PopStyleColor(); // sheetBg
    ImGui::PopStyleVar();   // dialog padding
    ImGui::End();
    ImGui::PopStyleColor(); // host bg
    ImGui::PopStyleVar();   // host padding
}

// --- Clipboard (used by ImGui for Ctrl+V paste) ------------------------------

static const char* GetClipboardText(void*)
{
    static std::string s_clipboardBuf;
    s_clipboardBuf.clear();

    // Try wl-paste first (native Wayland), then fall back to X11 tools.
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
            s_clipboardBuf += buf;

        pclose(fp);

        if (!s_clipboardBuf.empty())
            return s_clipboardBuf.c_str();
    }

    return s_clipboardBuf.c_str();
}

// --- Callback registered with EDreamClient -----------------------------------

static void OnFirstTimeSetupRequested()
{
    g_showRequested.store(true, std::memory_order_release);
    g_Player().SetFirstRunWizardPlaybackHold(true);
}

} // namespace

// --- Public API --------------------------------------------------------------

void FirstTimeSetupVulkan_Register()
{
    ESSetShowFirstTimeSetupCallback(OnFirstTimeSetupRequested);
}

bool FirstTimeSetupVulkan_IsWizardVisible()
{
    return g_visible.load(std::memory_order_acquire);
}

void FirstTimeSetupVulkan_DrawIfNeeded()
{
    if (g_showRequested.load(std::memory_order_acquire))
    {
        g_showRequested.store(false, std::memory_order_release);

        // Load wizard fonts into the shared atlas on first show.
        // ImGui_ImplVulkan_NewFrame() handles re-uploading the atlas automatically.
        LoadWizardFonts();

        // Save current style and apply wizard light-sheet style.
        // (Restored in the same call via PushStyleVar/Color inside DrawWizard.)
        ApplyLightSheetStyle();
        ImGui::GetStyle().ScaleAllSizes(g_uiScale);

        g_visible.store(true, std::memory_order_release);
        ResetWizardForShow();
        ApplyWizardPauseState(true);

        // Enable keyboard navigation for text fields.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        // Wire up clipboard so Ctrl+V paste works via wl-paste / xclip / xsel.
        ImGui::GetIO().GetClipboardTextFn = GetClipboardText;
    }

    if (!g_visible.load(std::memory_order_acquire)) return;

    PollWorkerResults();
    DrawWizard();
}

#ifdef HAVE_WAYLAND
bool FirstTimeSetupVulkan_FeedKey(uint32_t evdev_key, xkb_keysym_t keysym, bool pressed,
                                  struct xkb_state* state)
{
    if (!g_visible.load(std::memory_order_acquire)) return false;

    ImGuiIO& io = ImGui::GetIO();

    // Map xkb keysyms to ImGui keys for navigation and editing.
    ImGuiKey imkey = ImGuiKey_None;
    switch (keysym)
    {
    case XKB_KEY_BackSpace:  imkey = ImGuiKey_Backspace;    break;
    case XKB_KEY_Delete:     imkey = ImGuiKey_Delete;       break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:   imkey = ImGuiKey_Enter;        break;
    case XKB_KEY_Tab:        imkey = ImGuiKey_Tab;          break;
    case XKB_KEY_Left:       imkey = ImGuiKey_LeftArrow;    break;
    case XKB_KEY_Right:      imkey = ImGuiKey_RightArrow;   break;
    case XKB_KEY_Home:       imkey = ImGuiKey_Home;         break;
    case XKB_KEY_End:        imkey = ImGuiKey_End;          break;
    case XKB_KEY_Insert:     imkey = ImGuiKey_Insert;       break;
    case XKB_KEY_Escape:     imkey = ImGuiKey_Escape;       break;
    default: break;
    }

    bool ctrl = false;
    bool shift = false;
    if (state)
    {
        ctrl  = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_CTRL,
                                              XKB_STATE_MODS_EFFECTIVE) > 0;
        shift = xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
                                              XKB_STATE_MODS_EFFECTIVE) > 0;
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

    // Shift+Insert is the classic alternate paste shortcut — route it through
    // ImGui's normal Ctrl+V paste path so GetClipboardTextFn is called.
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

    // Feed printable characters using xkb_state_key_get_utf8 for full layout support
    // (handles non-ASCII characters correctly for any active keyboard layout).
    if (pressed && state && !ctrl && !(shift && keysym == XKB_KEY_Insert))
    {
        const xkb_keycode_t xkb_keycode = evdev_key + 8;

        // One Unicode codepoint is at most 4 bytes in UTF-8 + null terminator = 5;
        // 8 gives comfortable headroom for the xkb_state_key_get_utf8 output buffer.
        char utf8charbuf[8] = {};
        int len = xkb_state_key_get_utf8(state, xkb_keycode, utf8charbuf, sizeof utf8charbuf);

        // Exclude control characters (< 0x20) and DEL (0x7F).
        if (len > 0 &&
            static_cast<unsigned char>(utf8charbuf[0]) >= 0x20 &&
            static_cast<unsigned char>(utf8charbuf[0]) != 0x7f)
        {
            io.AddInputCharactersUTF8(utf8charbuf);
        }
    }

    return true; // always consume while wizard is visible
}
#endif // HAVE_WAYLAND

void FirstTimeSetupVulkan_FeedMousePos(int x, int y)
{
    if (!g_visible.load(std::memory_order_acquire)) return;
    ImGui::GetIO().AddMousePosEvent(static_cast<float>(x), static_cast<float>(y));
}

void FirstTimeSetupVulkan_FeedMouseButton(uint32_t button, bool pressed)
{
    if (!g_visible.load(std::memory_order_acquire)) return;
    // Wayland BTN_LEFT=272, BTN_RIGHT=273, BTN_MIDDLE=274
    int imguiBtn = -1;
    if (button == 272) imguiBtn = 0;      // left
    else if (button == 273) imguiBtn = 1; // right
    else if (button == 274) imguiBtn = 2; // middle
    if (imguiBtn >= 0)
        ImGui::GetIO().AddMouseButtonEvent(imguiBtn, pressed);
}

#endif // !WIN32 && !MAC
