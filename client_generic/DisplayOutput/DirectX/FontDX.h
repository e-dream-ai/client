/*
        FONTDX9.H
        Author: Stef.

        DX9 Font.
*/
#ifndef _FONTDX_H_
#define _FONTDX_H_

#include "Font.h"
#include "SmartPtr.h"
#include "base.h"

namespace DisplayOutput
{

/*
        CFontDX.

*/
class CFontDX : public CBaseFont
{
    IDirect3DDevice9 *m_pDevice;
    ID3DXFont *m_pDXFont;

  public:
    CFontDX(IDirect3DDevice9 *_pDevice);
    virtual ~CFontDX();

    bool Create();

    ID3DXFont *GetDXFont(void) { return (m_pDXFont); };
};

MakeSmartPointers(CFontDX);

}; // namespace DisplayOutput

#endif
