﻿#ifndef __electricsheepguiMyDialog2__
#define __electricsheepguiMyDialog2__

/**
@file
Subclass of MyDialog2, which is generated by wxFormBuilder.
*/

#include "config.h"

//// end generated include
#ifndef LINUX_GNU
#include <windows.h>
#include <wx/msw/winundef.h>
#else
#include <limits.h>
#define MAX_PATH PATH_MAX
#endif
#include <string>
#include <wx/thread.h>
#include <wx/wx.h>

BEGIN_DECLARE_EVENT_TYPES()
DECLARE_EVENT_TYPE(EVT_TYPE_LOGINSTATUSUPDATE, 1)
DECLARE_EVENT_TYPE(EVT_TYPE_LOGININFOUPDATE, 2)
END_DECLARE_EVENT_TYPES()

class LoginStatusUpdateEvent : public wxEvent
{
  public:
    LoginStatusUpdateEvent(const wxString& text = wxString());

    wxEvent* Clone() const { return new LoginStatusUpdateEvent(*this); }

    const wxString& getText() { return m_text; }

    DECLARE_DYNAMIC_CLASS(LoginStatusUpdateEvent)

  private:
    wxString m_text;
};

typedef void (wxEvtHandler::*LoginStatusUpdateEventFun)(
    LoginStatusUpdateEvent&);

#define EVT_UPDATE_LOGIN_STATUS(fun)                                           \
    DECLARE_EVENT_TABLE_ENTRY(                                                 \
        EVT_TYPE_LOGINSTATUSUPDATE, -1, -1,                                    \
        (wxObjectEventFunction)(LoginStatusUpdateEventFun) & fun,              \
        static_cast<wxObject*>(NULL)),

class LoginInfoUpdateEvent : public wxEvent
{
  public:
    LoginInfoUpdateEvent(const wxString& text = wxString());

    wxEvent* Clone() const { return new LoginInfoUpdateEvent(*this); }

    const wxString& getText() const { return m_text; }

    DECLARE_DYNAMIC_CLASS(LoginInfoUpdateEvent)

  private:
    wxString m_text;
};

typedef void (wxEvtHandler::*LoginInfoUpdateEventFun)(LoginInfoUpdateEvent&);

#define EVT_UPDATE_LOGIN_INFO(fun)                                             \
    DECLARE_EVENT_TABLE_ENTRY(                                                 \
        EVT_TYPE_LOGININFOUPDATE, -1, -1,                                      \
        (wxObjectEventFunction)(LoginInfoUpdateEventFun) & fun,                \
        static_cast<wxObject*>(NULL)),

class LoginThread : public wxThread
{
  public:
    LoginThread(wxThreadKind kind = wxTHREAD_JOINABLE) : wxThread(kind){};
    virtual ~LoginThread(){};
    void* Entry();
};

/** Implementing MyDialog2 */
class electricsheepguiMyDialog2 : public MyDialog2
{
    DECLARE_EVENT_TABLE()

  protected:
    // Handlers for MyDialog2 events.
    void OnDialogClose(wxCloseEvent& event);
    void OnIdle(wxIdleEvent& event);
    void OnRunClick(wxCommandEvent& event);
    void OnHelpClick(wxCommandEvent& event);
    void OnTextLeftUp(wxMouseEvent& event);
    void OnTextSetFocus(wxFocusEvent& event);
    void OnDrupalNameTextEnter(wxCommandEvent& event);
    void OnDrupalPasswordTextEnter(wxCommandEvent& event);
    void OnTestAccountButtonClick(wxCommandEvent& event);
    void OnCreateClick(wxCommandEvent& event);
    void OnUnlimitedCacheCheck(wxCommandEvent& event);
    void OnGoldUnlimitedCacheCheck(wxCommandEvent& event);
    void OnDecodeFpsKillFocus(wxFocusEvent& event);
    void OnDecodeFpsTextUpdated(wxCommandEvent& event);
    void OnPlayerFpsKillFocus(wxFocusEvent& event);
    void OnPlayerFpsTextUpdated(wxCommandEvent& event);
    void OnProxyTextEnter(wxCommandEvent& event);
    void OnProxyUserNameEnter(wxCommandEvent& event);
    void OnProxyPasswordEnter(wxCommandEvent& event);
    void OnContentDirChanged(wxFileDirPickerEvent& event);
    void OnOpenClick(wxCommandEvent& event);
    void OnAboutUrl(wxTextUrlEvent& event);
    void OnClickOk(wxCommandEvent& event);
    void OnCancelClick(wxCommandEvent& event);
    void DeleteListXml();
    void OnLoginStatusUpdate(LoginStatusUpdateEvent& event);
    void OnLoginInfoUpdate(LoginInfoUpdateEvent& event);

  public:
    /** Constructor */
    electricsheepguiMyDialog2(wxWindow* parent);
    //// end generated class members
    bool m_TestLogin;
    void Login();
    virtual ~electricsheepguiMyDialog2();

    std::string m_Response;
    std::string m_Role;

  private:
    char szPath[MAX_PATH];
    void SaveSettings();
    void LoadSettings();
    inline void FireLoginStatusUpdateEvent(const wxString& text);
    inline void FireLoginInfoUpdateEvent(const wxString& text);
    std::string m_UniqueId;
    bool m_ForceWindowedDX;
    LoginThread* m_LoginThread;
    bool m_NewFocus;
};

#endif // __electricsheepguiMyDialog2__
