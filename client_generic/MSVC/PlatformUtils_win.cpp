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
#include <vector>

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
    return __DATE__;
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

void PlatformUtils::SetCursorHidden(bool _hidden)
{
	ShowCursor(!_hidden);
}

void PlatformUtils::SetOnMouseMovedCallback(std::function<void(int, int)> _callback)
{
	// Not implemented
    g_Log->Error("SetOnMouseMovedCallback not implemented yet on WIN32");
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
    : m_DispatchTime(0), m_Func(_func)
{
    g_Log->Error("DelayedDispatch not implemented yet on WIN32");
}

void CDelayedDispatch::Cancel()
{
	m_DispatchTime = 0;
}

void CDelayedDispatch::DispatchAfter(uint64_t _seconds)
{
    g_Log->Error("DispatchAfter not implemented yet on WIN32, launching immediately");
	// Not implemented, just launch now
	m_Func();

	// TODO: TMP disabled this to stop the downloads during debugging 
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
