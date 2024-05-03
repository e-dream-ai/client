#include "PlatformUtils.h"
#include <Windows.h>
#include <iostream>
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>
#include "Log.h"

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

void PlatformUtils::DispatchOnMainThread(std::function<void()> _func)
{
	// Not implemented
    g_Log->Error("DispatchOnMainThread not implemented yet on WIN32");
}

CDelayedDispatch::CDelayedDispatch(std::function<void()> _func)
    : m_DispatchTime(0), m_Func(_func)
{
}

void CDelayedDispatch::Cancel()
{
	m_DispatchTime = 0;
}

void CDelayedDispatch::DispatchAfter(uint64_t _seconds)
{
    g_Log->Error("DispatchAfter not implemented yet on WIN32");
	// Not implemented, just launch now
	m_Func();
}