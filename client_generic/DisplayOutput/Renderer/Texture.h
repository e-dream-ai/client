#ifndef _TEXTURE_H
#define _TEXTURE_H

#include "base.h"

namespace DisplayOutput
{

/**
        CTexture.

*/
class CTexture
{
  protected:
    enum eTextureFlags
    {
        TEXTURE_NONE = 0,
        TEXTURE_YUV = 1 << 0,
    };
    uint32_t m_Flags;

  public:
    CTexture(const uint32_t _flags = 0);
    virtual ~CTexture();

    virtual bool Bind(const uint32_t _index) = PureVirtual;
    virtual bool Unbind(const uint32_t _index) = PureVirtual;

    virtual bool Dirty(void) { return false; };
    virtual bool IsYUVTexture() const
    {
        return m_Flags & eTextureFlags::TEXTURE_YUV;
    }
};

MakeSmartPointers(CTexture);

} // namespace DisplayOutput

#endif
