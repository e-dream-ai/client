#include <cassert>
#include <inttypes.h>
#include <string>

#include "DisplayOutput.h"
#include "Exception.h"
#include "Log.h"
#include "MathBase.h"
#include "TextureFlat.h"
#include "base.h"

namespace DisplayOutput
{

/*
 */
CTextureFlat::CTextureFlat(const uint32_t _flags)
    : CTexture(_flags), m_spImage(NULL), m_bDirty(false),
      m_texRect(Base::Math::CRect(1, 1))
{
}

/*
 */
CTextureFlat::~CTextureFlat() {}

} // namespace DisplayOutput
