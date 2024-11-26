#include "PlatformUtils.h"
#include <Windows.h>
#include <iostream>
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#include "Log.h"

#include <openssl/md5.h>
#include <fstream>
#include <sstream>
#include <iomanip>

bool PlatformUtils::IsInternetReachable()
{ 
	// TODO: Implement this
	return true; 
}


std::string PlatformUtils::GetBuildDate()
{
	return __DATE__;
}

std::string PlatformUtils::GetGitRevision()
{
	return "unknown";
}

std::string PlatformUtils::GetAppVersion()
{
	return "1.0";
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
