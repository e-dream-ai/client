/*
 *  AlignedBuffer.h
 *
 *	Implements page aligned buffer
 *
 *  Created by dasvo on 2/24/09.
 *
 */

#ifndef _ALIGNEDBUFFER_H_
#define _ALIGNEDBUFFER_H_

#include <cstdint>
#ifndef WIN32
#include <unistd.h>
#endif
#include "Singleton.h"
#include "SmartPtr.h"
#include "base.h"
#include <boost/thread.hpp>

#ifdef LINUX_GNU
#include <cstdio>
#endif

#define BUFFER_CACHE_SIZE 20

namespace Base
{

MakeSmartPointers(CReusableAlignedBuffers);

// idea proposed by F-D. Cami
class CReusableAlignedBuffers : public CSingleton<CReusableAlignedBuffers>
{
    typedef struct
    {
        uint32_t seed;

        uint32_t size;

        uint8_t* ptr;
    } BufferElement;

    BufferElement m_BufferCache[BUFFER_CACHE_SIZE];

    uint32_t m_Seed;

    static uint32_t s_PageSize;

    boost::mutex m_CacheLock;

  public:
    CReusableAlignedBuffers();

    ~CReusableAlignedBuffers();

    uint8_t* Allocate(uint32_t size);

    void Free(uint8_t* ptr, uint32_t size);

    static void RealFree(uint8_t* ptr, uint32_t size);

    uint8_t* Reallocate(uint8_t* ptr, uint32_t size);

    static inline uint32_t GetPageSize(void)
    {
        if (s_PageSize == 0)
        {
#if defined(WIN32) && WIN32
            SYSTEM_INFO system_info;
            GetSystemInfo(&system_info);
            s_PageSize = (uint32_t)system_info.dwPageSize;
#else
            s_PageSize = static_cast<uint32_t>(getpagesize());
#endif
        }

        return s_PageSize;
    }

    bool Shutdown(void) { return true; }

    const char* Description() { return "CReusableAlignedBuffers"; }

    //	Provides singleton access.
    static CReusableAlignedBuffers* Instance(const char* /*_pFileStr*/,
                                             const uint32_t /*_line*/,
                                             const char* /*_pFunc*/)
    {
        static CReusableAlignedBuffers rab;

        if (rab.SingletonActive() == false)
        {
            printf("Trying to access shutdown singleton %s\n",
                   rab.Description());
            return NULL;
        }

        return (&rab);
    }
};

#define g_ReusableAlignedBuffers                                               \
    Base::CReusableAlignedBuffers::Instance(__FILE__, __LINE__, __FUNCTION__)

/*
        CAlignedBuffer.

*/
MakeSmartPointers(CAlignedBuffer);

class CAlignedBuffer
{
    uint8_t* m_Buffer;

    uint8_t* m_BufferAlignedStart;

    uint32_t m_Size;

  public:
    CAlignedBuffer();

    CAlignedBuffer(uint32_t size);

    ~CAlignedBuffer();

    bool Allocate(uint32_t size);

    bool Reallocate(uint32_t size);

    void Free(void);

    uint8_t* GetBufferPtr(void) const;

    bool IsValid() const { return (m_Buffer != NULL); }

    uint32_t Size(void) { return m_Size; }
};

}; // namespace Base

#endif
