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
public:
    CBaseText();
    virtual ~CBaseText();
    virtual void SetText(const std::string& _text) = PureVirtual;
    virtual Base::Math::CVector2 GetExtent() const = PureVirtual;
};

MakeSmartPointers( CBaseText );

}

#endif /*__TEXT_H_*/
