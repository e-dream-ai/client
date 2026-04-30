#include "PlatformUtils.h"
#include "clientversion.h"
#include "edream_build_version_generated.h"
#include <Windows.h>
#include <algorithm>
#include <iostream>
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#include "Log.h"

#include <openssl/md5.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <array>
#include <cctype>
#include <cstdio>
#include <mutex>

#include <vector>

static HWND g_win32MsgHwnd = nullptr;
static std::mutex g_mouseCallbackMutex;
static std::function<void(int, int)> g_onMouseMoved;

namespace
{
std::string TrimWhitespace(std::string value)
{
    const auto isWs = [](unsigned char c) { return std::isspace(c) != 0; };
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(),
                             [&](unsigned char c) { return !isWs(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](unsigned char c) { return !isWs(c); })
                    .base(),
                value.end());
    return value;
}

std::string GetEnvVar(const char* key)
{
    if (!key || !*key)
        return {};

    const DWORD required = GetEnvironmentVariableA(key, nullptr, 0);
    if (required == 0)
        return {};

    std::string value(required, '\0');
    const DWORD written =
        GetEnvironmentVariableA(key, value.data(), required);
    if (written == 0)
        return {};

    if (!value.empty() && value.back() == '\0')
        value.pop_back();
    return TrimWhitespace(std::move(value));
}

std::string RunCommandAndGetFirstLine(const char* command)
{
    if (!command || !*command)
        return {};

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
        return {};

    // Reader handle must not be inherited by child process.
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::string cmdLine = "cmd.exe /C ";
    cmdLine += command;
    std::vector<char> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back('\0');

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    const BOOL created =
        CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    // Parent never writes to child stdout/stderr.
    CloseHandle(writePipe);
    writePipe = nullptr;

    if (!created)
    {
        CloseHandle(readPipe);
        return {};
    }

    std::array<char, 512> buffer{};
    std::string output;
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(),
                    static_cast<DWORD>(buffer.size() - 1), &bytesRead, nullptr) &&
           bytesRead > 0)
    {
        output.append(buffer.data(), bytesRead);
        if (output.size() > 8192)
            break;
    }

    CloseHandle(readPipe);
    readPipe = nullptr;

    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (output.empty())
        return {};

    const size_t newline = output.find_first_of("\r\n");
    if (newline != std::string::npos)
        output.resize(newline);
    return TrimWhitespace(std::move(output));
}
} // namespace

void PlatformUtils::Win32SetMessageWindow(void* hwnd)
{
    g_win32MsgHwnd = static_cast<HWND>(hwnd);
}

void PlatformUtils::NotifyMouseMoved(int x, int y)
{
    std::function<void(int, int)> cb;
    {
        std::lock_guard<std::mutex> lock(g_mouseCallbackMutex);
        cb = g_onMouseMoved;
    }
    if (cb)
        cb(x, y);
}

bool PlatformUtils::IsInternetReachable()
{ 
	// TODO: Implement this
	return true; 
}


std::string PlatformUtils::GetBuildDate()
{
    const std::string fromEnv = GetEnvVar("BUILD_DATE");
    if (!fromEnv.empty())
        return fromEnv;

    const std::string embedded(EDREAM_BUILD_DATE_UTC);
    if (!embedded.empty())
        return embedded;

    return std::string(__DATE__) + " " + __TIME__;
}

std::string PlatformUtils::GetGitRevision()
{
    static const std::string revision = []() -> std::string {
        const std::string fromEnv = GetEnvVar("GIT_REVISION");
        if (!fromEnv.empty())
            return fromEnv;

        const std::string fromBuildRev = GetEnvVar("BUILD_REVISION");
        if (!fromBuildRev.empty())
            return fromBuildRev;

        const std::string embeddedGit(EDREAM_BUILD_GIT_REVISION);
        if (!embeddedGit.empty() && embeddedGit != "unknown")
            return embeddedGit;

        const std::string fromGit = RunCommandAndGetFirstLine(
            "git -C . rev-parse --short HEAD 2>nul");
        if (!fromGit.empty())
            return fromGit;

        return "unknown";
    }();
    return revision;
}

std::string PlatformUtils::GetAppVersion()
{
    static const std::string version = []() -> std::string {
        const std::string fromEnv = GetEnvVar("BUILD_VERSION");
        if (!fromEnv.empty())
            return fromEnv;

        const std::string fromAppVersion = GetEnvVar("APP_VERSION");
        if (!fromAppVersion.empty())
            return fromAppVersion;

        // Semver from WinBuild/build.py (-v / --version) or clientversion.h, compiled in.
        return std::string(OS_PREFIX_PRETTY) + EDREAM_BUILD_VERSION_SEMVER;
    }();
    return version;
}

std::string PlatformUtils::GetPlatformName() { return "native W64"; }

std::string PlatformUtils::GetWorkingDir()
{
    char buffer[MAX_PATH];
    DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);

    if (length == 0)
    {
        // Handle error case
        g_Log->Error("Failed to get current directory. Error code: %lu",
                     GetLastError());
        return "";
    }

    // Convert backslashes to forward slashes for consistency
    std::string workingDir(buffer);
    std::replace(workingDir.begin(), workingDir.end(), '\\', '/');

    // Ensure path ends with a forward slash
    if (!workingDir.empty() && workingDir.back() != '/')
    {
        workingDir += '/';
    }

    return workingDir;
}

std::string PlatformUtils::GetAppPath()
{
    char pathBuf[MAX_PATH]{};
    const DWORD len = GetModuleFileNameA(nullptr, pathBuf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};

    std::string full(pathBuf, len);

    // Strip filename to directory.
    const size_t slash = full.find_last_of("\\/");
    if (slash == std::string::npos)
        return {};

    std::string dir = full.substr(0, slash + 1);

    // Ensure trailing separator is backslash for Win32 consumers.
    if (!dir.empty() && dir.back() != '\\' && dir.back() != '/')
        dir.push_back('\\');
    return dir;
}

void PlatformUtils::SetCursorHidden(bool _hidden)
{
    // ShowCursor uses a process-global display counter. A single call is not
    // sufficient if other parts of the process have adjusted the counter.
    // Force the counter to the requested visible/hidden state.
    if (_hidden)
    {
        while (ShowCursor(FALSE) >= 0) { /* keep decrementing until hidden */ }
    }
    else
    {
        while (ShowCursor(TRUE) < 0) { /* keep incrementing until visible */ }
    }
}

void PlatformUtils::SetOnMouseMovedCallback(std::function<void(int, int)> _callback)
{
    std::lock_guard<std::mutex> lock(g_mouseCallbackMutex);
    g_onMouseMoved = std::move(_callback);
}

void PlatformUtils::OpenURLExternally(std::string_view _url)
{
	ShellExecuteA(nullptr, "open", _url.data(), nullptr, nullptr, SW_SHOWNORMAL);
}

void PlatformUtils::SetThreadName(std::string_view _name)
{
	// Convert to WSTR 
	std::wstring stemp = std::wstring(_name.begin(), _name.end());
    LPCWSTR sw = stemp.c_str(); 
	
    HRESULT r;
    r = SetThreadDescription(GetCurrentThread(), sw);
}


void PlatformUtils::NotifyError(std::string_view errorMessage)
{
    // TODO : implement this 
    
    // Report the error to Bugsnag
    //[Bugsnag notify:[NSException exceptionWithName:@"Error" reason:nsErrorMessage userInfo:nil]];
}


void PlatformUtils::DispatchOnMainThread(std::function<void()> _func)
{
	// Not implemented, launch on same thread
    //g_Log->Error("DispatchOnMainThread not implemented yet on WIN32");
    _func();
}

CDelayedDispatch::CDelayedDispatch(std::function<void()> _func)
    : m_DispatchTime(0), m_Func(std::move(_func))
{
}

void CDelayedDispatch::Cancel()
{
    std::lock_guard<std::mutex> lock(m_TimerMutex);
    if (g_win32MsgHwnd)
        KillTimer(g_win32MsgHwnd, reinterpret_cast<UINT_PTR>(this));
    m_DispatchTime = 0;
}

void CALLBACK CDelayedDispatch::Win32TimerProc(HWND hwnd, UINT, UINT_PTR idEvent,
                                               DWORD)
{
    KillTimer(hwnd, idEvent);
    auto* self = reinterpret_cast<CDelayedDispatch*>(idEvent);
    if (self)
        self->Win32OnTimerFired();
}

void CDelayedDispatch::Win32OnTimerFired()
{
    std::function<void()> fn;
    {
        std::lock_guard<std::mutex> lock(m_TimerMutex);
        if (m_DispatchTime == 0)
            return;
        m_DispatchTime = 0;
        fn = m_Func;
    }
    if (fn)
        fn();
}

void CDelayedDispatch::DispatchAfter(uint64_t _seconds)
{
    std::lock_guard<std::mutex> lock(m_TimerMutex);
    HWND hwnd = g_win32MsgHwnd;
    if (hwnd)
        KillTimer(hwnd, reinterpret_cast<UINT_PTR>(this));

    if (_seconds == 0)
    {
        m_DispatchTime = 0;
        if (m_Func)
            m_Func();
        return;
    }

    if (!hwnd)
        return;

    m_DispatchTime = 1;
    // SetTimer elapse is limited to 0x7FFFFFFF ms (~24.8 days).
    constexpr DWORD kMaxTimerMs = 0x7FFFFFFF;
    const DWORD ms =
        (_seconds > (static_cast<uint64_t>(kMaxTimerMs) / 1000ULL))
            ? kMaxTimerMs
            : static_cast<DWORD>(_seconds * 1000ULL);
    const UINT_PTR timerId = reinterpret_cast<UINT_PTR>(this);
    if (!SetTimer(hwnd, timerId, ms, &CDelayedDispatch::Win32TimerProc))
    {
        g_Log->Warning("CDelayedDispatch::DispatchAfter SetTimer failed");
        m_DispatchTime = 0;
    }
}

// This uses OpenSSL implementation of md5.
// Note : should we have issues on other platforms, boost also has an implementation
std::string PlatformUtils::CalculateFileMD5(const std::string& filepath)
{
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
    {
        return "";
    }

    MD5_CTX context;
    MD5_Init(&context);

    char buffer[1024];
    while (file.read(buffer, sizeof(buffer)))
    {
        MD5_Update(&context, buffer, file.gcount());
    }
    if (file.gcount() > 0)
    {
        MD5_Update(&context, buffer, file.gcount());
    }

    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_Final(digest, &context);

    std::stringstream ss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
    {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<int>(digest[i]);
    }

    return ss.str();
}
