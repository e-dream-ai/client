///////////////////////////////////////////////////////////////////////////////
//
//    electricsheep for windows - collaborative screensaver
//    Copyright 2003 Nicholas Long <nlong@cox.net>
//	  electricsheep for windows is based of software
//	  written by Scott Draves <source@electricsheep.org>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
///////////////////////////////////////////////////////////////////////////////
#ifndef _CONTENTDECODER_H
#define _CONTENTDECODER_H

#include <queue>
#include <string>
#include <string_view>
#include <memory>
#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/xtime.hpp>
#include <future>

//	FFmpeg headers.
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavcodec/bsf.h"
#include "libavformat/avformat.h"
#include "libavutil/hwcontext.h"
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

#include "base.h"
#include "BlockingQueue.h"
#include "Frame.h"
#include "CacheManager.h"

namespace ContentDecoder
{

struct sClipMetadata
{
    std::string path;
    double decodeFps;
    Cache::Dream dreamData;
};

struct sOpenVideoInfo
{
    sOpenVideoInfo()
        : m_pFrame(NULL), m_pFormatContext(NULL), m_pVideoCodecContext(NULL),
          m_pVideoCodec(NULL), m_pVideoStream(NULL), m_VideoStreamID(-1),
          m_TotalFrameCount(0), m_CurrentFrameIndex(0), m_SeekTargetFrame(0),
          m_NextIsSeam(false), m_ReadingTrailingFrames(false)

    {
    }

    ~sOpenVideoInfo();

    void Reset() { m_pFormatContext = NULL; }

    bool IsOpen() { return (m_pFormatContext != NULL); }

    bool EqualsTo(sOpenVideoInfo* ovi) { return m_Path == ovi->m_Path; }

    AVFrame* m_pFrame;
    AVFormatContext* m_pFormatContext;
    AVCodecContext* m_pVideoCodecContext;
    AVBSFContext* m_pBsfContext = nullptr;
    const AVCodec* m_pVideoCodec;
    AVStream* m_pVideoStream;
    int32_t m_VideoStreamID;
    uint32_t m_TotalFrameCount;
    int64_t m_CurrentFrameIndex;
    int64_t m_SeekTargetFrame;
    float m_DecodeFps;
    std::string m_Path;
    bool m_NextIsSeam;
    bool m_ReadingTrailingFrames;
};

/*
        CContentDecoder.
        Video decoding wrapper for ffmpeg, influenced by glover.
*/
class CContentDecoder
{
    bool m_bStop;

    SwsContext* m_pScaler;
    uint32_t m_ScalerWidth;
    uint32_t m_ScalerHeight;

    boost::thread* m_pDecoderThread;
    void ReadFramesThread();

    //	Queue for decoded frames.
    Base::CBlockingQueue<CVideoFrame*> m_FrameQueue;
    std::mutex m_FrameQueueMutex;
    //	Codec context & working objects.
    std::unique_ptr<sOpenVideoInfo> m_CurrentVideoInfo;
    AVPixelFormat m_WantedPixelFormat;
    boost::atomic<float> m_SkipForward;
    boost::atomic<bool> m_HasEnded;

    void Destroy();
    bool Open();
    CVideoFrame* ReadOneFrame();
    static int DumpError(int _err);

    std::atomic<bool> m_isShuttingDown{false};
    void abortFutures();

  public:
    CContentDecoder(const uint32_t _queueLenght,
                    AVPixelFormat _wantedPixelFormat = AV_PIX_FMT_RGB24);
    virtual ~CContentDecoder();

    void signalShutdown() { m_isShuttingDown.store(true); abortFutures(); }
    
    void Close();
    bool Start(const sClipMetadata& metadata, int64_t _seekFrame = -1);
    void Stop();
    const sOpenVideoInfo* GetVideoInfo() const
    {
        return m_CurrentVideoInfo.get();
    }
    spCVideoFrame PopVideoFrame();
    bool HasEnded() const { return m_HasEnded.load() && !m_FrameQueue.size(); }
    bool Stopped() { return m_bStop; };
    uint32_t QueueLength();
    void ClearQueue(uint32_t leave = 0);
    void SkipTime(float _secondsForward)
    {
        m_SkipForward.exchange(_secondsForward);
    }
    
  private:
    std::vector<std::future<void>> m_futures;
    sClipMetadata m_Metadata;
    bool IsURL(const std::string& path);
    AVIOContext* m_pIOContext;
    unsigned char* m_pIOBuffer;
    const int kIOBufferSize = 32768;  // 32 KB buffer

    int64_t m_CacheWritePosition;
    std::mutex m_CacheMutex;
    
    std::string m_CachePath;
    FILE* m_CacheFile;
    bool m_IsStreaming;
    static int ReadPacket(void* opaque, uint8_t* buf, int buf_size);
    static int64_t SeekPacket(void* opaque, int64_t offset, int whence);
    void SetCachePath(const std::string& path);
    bool OpenCacheFile();
    void CloseCacheFile();
    void WriteToCache(const uint8_t* buf, int buf_size, int64_t position);
    std::string GenerateCacheFileName();
    
    bool IsDownloadComplete() const;
    void FinalizeCacheFile();
};

MakeSmartPointers(CContentDecoder);

} // namespace ContentDecoder

#endif
