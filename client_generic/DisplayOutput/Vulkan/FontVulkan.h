#ifndef _FONTVULKAN_H_
#define _FONTVULKAN_H_

#include "Font.h"
#include "Text.h"
#include <string>

namespace DisplayOutput
{

/*
    CFontVulkan — minimal stub font for Phase 2.
    Full text rendering can be added in a later phase using FreeType.
*/
class CFontVulkan : public CBaseFont
{
  public:
    CFontVulkan() {}
    virtual ~CFontVulkan() {}
    virtual bool Create() override { return true; }
};

MakeSmartPointers(CFontVulkan);

/*
    CTextVulkan — holds text string; rendering is a no-op stub.
*/
class CTextVulkan : public CBaseText
{
    std::string m_text;
    bool m_enabled = true;

  public:
    CTextVulkan(spCBaseFont /*font*/, const std::string& text) : m_text(text) {}
    virtual ~CTextVulkan() {}

    virtual void SetText(const std::string& _text) override { m_text = _text; }
    virtual Base::Math::CVector2 GetExtent() override
    {
        return Base::Math::CVector2(0, 0);
    }
    virtual void SetEnabled(bool _enabled) override { m_enabled = _enabled; }

    const std::string& Text() const { return m_text; }
};

MakeSmartPointers(CTextVulkan);

} // namespace DisplayOutput

#endif
