#ifndef	__TEXT_H_
#define	__TEXT_H_

#include	<string>
#include	"base.h"
#include	"SmartPtr.h"
#include    "Rect.h"
#include    "Vector2.h"

namespace	DisplayOutput
{

/*
 CBaseText.

*/
class	CBaseText
{
    Base::Math::CRect m_Rect;
public:
    CBaseText();
    virtual ~CBaseText();
    virtual void SetText(const std::string& _text) = PureVirtual;
    virtual Base::Math::CVector2 GetExtent() = PureVirtual;
    virtual void SetRect(const Base::Math::CRect& _rect) { m_Rect = _rect; }
    virtual Base::Math::CRect GetRect() const { return m_Rect; }
    virtual void SetEnabled(bool _enabled) = PureVirtual;
};

MakeSmartPointers( CBaseText );

}

#endif /*__TEXT_H_*/
