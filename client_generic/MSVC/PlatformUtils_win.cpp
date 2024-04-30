#include "PlatformUtils.h"
#include <Windows.h>
#include <iostream>
#include <windows.h>
#include <wininet.h>
#include <shellapi.h>

bool PlatformUtils::IsInternetReachable()
{ return true; }


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
}

void PlatformUtils::OpenURLExternally(std::string_view _url)
{
	ShellExecuteA(nullptr, "open", _url.data(), nullptr, nullptr, SW_SHOWNORMAL);
}

void PlatformUtils::SetThreadName(std::string_view _name)
{
	// Not implemented
}

void PlatformUtils::DispatchOnMainThread(std::function<void()> _func)
{
	// Not implemented
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
	// Not implemented, just launch now
	m_Func();
}