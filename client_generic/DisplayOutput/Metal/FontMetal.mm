#include "FontMetal.h"
#include "TextMetal.h"

namespace DisplayOutput
{

/*
        CFontMetal().

*/
CFontMetal::CFontMetal(CFontDescription& _desc)
    : CBaseFont()
{
    m_typeFace = _desc.TypeFace();
}

bool CFontMetal::Create()
{
    return true;
}

/*
        ~CFontMetal().

*/
CFontMetal::~CFontMetal()
{
}

}; // namespace DisplayOutput
