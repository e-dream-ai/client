#ifndef _FONTMETAL_H_
#define _FONTMETAL_H_

#import <CoreFoundation/CoreFoundation.h>

#include <boost/thread.hpp>

#include "DisplayOutput.h"
#include "Font.h"
#include "SmartPtr.h"
#include "base.h"

namespace DisplayOutput
{

/*
        CFontMetal.

*/
class CFontMetal : public CBaseFont
{
    std::string m_typeFace;

  public:
    CFontMetal(CFontDescription& _desc);
    virtual ~CFontMetal();
    virtual bool Create();
};

MakeSmartPointers(CFontMetal);

}; // namespace DisplayOutput

#endif
