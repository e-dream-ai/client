#include "pool.h"
#include <inttypes.h>
#include <new>
#include <string>

namespace Memory
{

/*
        AllocSys().

*/
void *CPoolBase::AllocSys(size_t _size)
{
  void *pData = ::operator new(_size, std::nothrow);
  if (!pData)
  {
    //	Do whatever you want.
  }

  return pData;
}

/*
        DeallocSys().

*/
void CPoolBase::DeallocSys(void *_pData)
{
  if (_pData)
    delete (char *)_pData;
}

} // namespace Memory
