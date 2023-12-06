#pragma once

#include "electricsheepguiMyDialog2.h"
#include <wx/wx.h>

class wxWidgetsApp : public wxApp
{
public:
  wxWidgetsApp();
  virtual ~wxWidgetsApp();
  virtual bool OnInit();

private:
  electricsheepguiMyDialog2 *m_dialog;
};

DECLARE_APP(wxWidgetsApp)