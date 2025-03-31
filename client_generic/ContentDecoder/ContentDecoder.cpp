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

#include <boost/filesystem.hpp> // TODO: We need to unify to std::fs someday
#include <string>
#include <sys/stat.h>
#include <algorithm>
#include <filesystem>
#include <future>

#include "ContentDecoder.h"
#include "Log.h"
#include "PlatformUtils.h"
#include "PathManager.h"
#include "EDreamClient.h"

using namespace boost;
namespace fs = std::filesystem;

namespace ContentDecoder
{

static void AVCodecLogCallback(void* /*_avcl*/, int _level, const char* _fmt,
                               va_list _vl)
{
    // Format the log message using vsnprintf
    char buf[1024];
    vsnprintf(buf, sizeof(buf), _fmt, _vl);
    if (_level == AV_LOG_ERROR)
    {
        g_Log->Error("FFmpeg error: %s", buf);
    }
    else if (_level == AV_LOG_WARNING)
    {
        g_Log->Warning("FFmpeg warning: %s", buf);
    }
    else
    {
        // g_Log->Info("FFmpeg info: %s", buf);
    }
}
/*
 CContentDecoder.
 
 */
CContentDecoder::CContentDecoder(const uint32_t _queueLenght,
                                 AVPixelFormat _wantedFormat)
{
    g_Log->Info("CContentDecoder()");
    //	We want errors!
    av_log_set_level(AV_LOG_ERROR);
    m_pScaler = nullptr;
    m_ScalerWidth = 0;
    m_ScalerHeight = 0;
    m_pDecoderThread = nullptr;
    m_FrameQueue.setMaxQueueElements(_queueLenght);
    m_WantedPixelFormat = _wantedFormat;
    m_bStop = true;
    m_CurrentVideoInfo = nullptr;
    av_log_set_callback(AVCodecLogCallback);
}

CContentDecoder::~CContentDecoder()
{
    Stop();

    // Only wait for futures if we're not in a shutdown state
    if (!m_isShuttingDown.load()) {
        g_Log->Info("Ensuring futures are closed normally");
        for (auto& future : m_futures) {
            future.wait();
        }
        g_Log->Info("Futures are closed");
    } else {
        g_Log->Info("Shutdown detected, aborting futures");
        abortFutures();
    }
    
    Destroy();
}

void CContentDecoder::Destroy()
{
    g_Log->Info("Destroy()");
    if (m_pScaler)
    {
        g_Log->Info("deleting m_pScalar");
        sws_freeContext(m_pScaler);
        m_pScaler = nullptr;
    }
    if (m_CurrentVideoInfo && m_CurrentVideoInfo->m_pVideoCodecContext)
    {
        avcodec_free_context(&m_CurrentVideoInfo->m_pVideoCodecContext);
        av_bsf_free(&m_CurrentVideoInfo->m_pBsfContext);
        m_CurrentVideoInfo->m_pVideoCodecContext = nullptr;
    }
}

void CContentDecoder::abortFutures() {
    // First mark all operations as needing to stop
    m_HasEnded.store(true);
    
    // Close cache file immediately if open
    CloseCacheFile();
    
    // Clear any pending futures
    m_futures.clear();
}


int CContentDecoder::DumpError(int _err)
{
    if (_err < 0)
    {
        switch (_err)
        {
            case AVERROR_INVALIDDATA:
                g_Log->Error("FFmpeg error %s: Error while parsing header",
                             UNFFERRTAG(_err));
                break;
            case AVERROR(EIO):
                g_Log->Error(
                             "FFmpeg error %s: I/O error occured. Usually that means "
                             "that input file is truncated and/or corrupted.",
                             UNFFERRTAG(_err));
                break;
            case AVERROR(ENOMEM):
                g_Log->Error("FFmpeg error %s: Memory allocation error occured",
                             UNFFERRTAG(_err));
                break;
            case AVERROR(ENOENT):
                g_Log->Error("FFmpeg error %s: ENOENT", UNFFERRTAG(_err));
                break;
            default:
                g_Log->Error("FFmpeg error %s: Error while opening file",
                             UNFFERRTAG(_err));
                break;
        }
    }
    return _err;
}

bool CContentDecoder::IsURL(const std::string& path)
{
    return path.substr(0, 7) == "http://" || path.substr(0, 8) == "https://";
}

bool CContentDecoder::Open()
{
    auto ovi = m_CurrentVideoInfo.get();
    if (ovi == nullptr)
        return false;
    
    std::string _filename = ovi->m_Path;
    ovi->m_TotalFrameCount = 0;
    g_Log->Info("Opening: %s", _filename.c_str());
    
    avformat_network_init();  // Initialize network
    
    int ret;
    if (IsURL(_filename))
    {
        m_IsStreaming = true;
        m_CacheWritePosition = 0;
        
        // Generate a cache file path
        std::string cacheFileName = GenerateCacheFileName();
        SetCachePath(cacheFileName);
        
        if (!OpenCacheFile())
        {
            g_Log->Warning("Failed to open cache file for writing");
            m_IsStreaming = false;
        }
        
        // For URLs, we use avio_open2
        ret = avio_open2(&m_pIOContext, _filename.c_str(), AVIO_FLAG_READ, nullptr, nullptr);
        if (ret < 0)
        {
            g_Log->Warning("Failed to open URL %s...", _filename.c_str());
            return false;
        }

        // Create a new AVIOContext that wraps the original one
        constexpr int kIOBufferSize = 32768;
        unsigned char* buffer = static_cast<unsigned char*>(av_malloc(kIOBufferSize));
        if (!buffer) {
            g_Log->Error("Failed to allocate I/O buffer");
            return false;
        }

        AVIOContext* custom_io = avio_alloc_context(
            buffer, kIOBufferSize, 0, this, &CContentDecoder::ReadPacket, nullptr, &CContentDecoder::SeekPacket);

        if (!custom_io) {
            g_Log->Error("Failed to allocate custom I/O context");
            av_free(buffer);
            return false;
        }

        ovi->m_pFormatContext = avformat_alloc_context();
        ovi->m_pFormatContext->pb = custom_io;
        ovi->m_pFormatContext->flags |= AVFMT_FLAG_CUSTOM_IO;
    } else {
        m_IsStreaming = false;
    }
    
    // Open input, whether it's a file or URL
    AVDictionary* options = nullptr;
    av_dict_set(&options, "probesize", "10000000", 0);  //
    av_dict_set(&options, "analyzeduration", "0", 0);
    if (DumpError(avformat_open_input(&ovi->m_pFormatContext, m_IsStreaming ? nullptr : _filename.c_str(), nullptr, &options)) < 0)
    {
        g_Log->Warning("Failed to open %s...", _filename.c_str());
        return false;
    }

    av_dict_free(&options);

    if (DumpError(avformat_find_stream_info(ovi->m_pFormatContext, nullptr)) < 0)
    {
        g_Log->Error("av_find_stream_info failed with %s...", _filename.c_str());
        return false;
    }

    //	Find video stream;
    ovi->m_VideoStreamID = -1;
    for (uint32_t i = 0; i < ovi->m_pFormatContext->nb_streams; i++)
    {
        if (ovi->m_pFormatContext->streams[i]->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO)
        {
            ovi->m_pVideoStream = ovi->m_pFormatContext->streams[i];
            ovi->m_VideoStreamID = static_cast<int32_t>(i);
            break;
        }
    }
    if (ovi->m_VideoStreamID == -1)
    {
        g_Log->Error("Could not find video stream in %s", _filename.c_str());
        return false;
    }
    const auto& codecPar =
    ovi->m_pFormatContext->streams[ovi->m_VideoStreamID]->codecpar;
    ovi->m_pVideoCodec = avcodec_find_decoder(codecPar->codec_id);
    ovi->m_pVideoCodecContext = avcodec_alloc_context3(ovi->m_pVideoCodec);
    if (codecPar->codec_id == AV_CODEC_ID_H264)
    {
        const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
        if (!bsf)
        {
            g_Log->Error("FFmpeg error: av_bsf_get_by_name() failed");
            return false;
        }
        if (DumpError(av_bsf_alloc(bsf, &ovi->m_pBsfContext)))
        {
            g_Log->Error("FFmpeg error: av_bsf_alloc() failed");
            return false;
        }
        avcodec_parameters_copy(ovi->m_pBsfContext->par_in, codecPar);
        if (DumpError(av_bsf_init(ovi->m_pBsfContext)))
        {
            g_Log->Error("FFmpeg error: av_bsf_init() failed");
            return false;
        }
    } else if (codecPar->codec_id == AV_CODEC_ID_HEVC) {
        const AVBitStreamFilter* bsf = av_bsf_get_by_name("hevc_mp4toannexb");
        
        if (!bsf)
        {
            g_Log->Error("FFmpeg error: av_bsf_get_by_name() failed");
            return false;
        }
        if (DumpError(av_bsf_alloc(bsf, &ovi->m_pBsfContext)))
        {
            g_Log->Error("FFmpeg error: av_bsf_alloc() failed");
            return false;
        }
        avcodec_parameters_copy(ovi->m_pBsfContext->par_in, codecPar);
        if (DumpError(av_bsf_init(ovi->m_pBsfContext)))
        {
            g_Log->Error("FFmpeg error: av_bsf_init() failed");
            return false;
        }
        
    } else {
        printf("unknown codec?");
        g_Log->Error("unknown codec? ");
        return false;
    }
    if (USE_HW_ACCELERATION)
    {
        AVHWDeviceType hw_type = av_hwdevice_find_type_by_name("videotoolbox");
        if (hw_type != AV_HWDEVICE_TYPE_NONE)
        {
            ovi->m_pVideoCodecContext->hw_device_ctx =
            av_hwdevice_ctx_alloc(hw_type);
            av_hwdevice_ctx_init(ovi->m_pVideoCodecContext->hw_device_ctx);
        }
        else
        {
            g_Log->Error("Hardware acceleration unsupported.");
        }
    }
    // Initialize the codec context
    ovi->m_pFormatContext->flags |= AVFMT_FLAG_IGNIDX;   //	Ignore index.
    ovi->m_pFormatContext->flags |= AVFMT_FLAG_NONBLOCK; //	Do not
    // block when reading packets from input.
    if (DumpError(avcodec_open2(ovi->m_pVideoCodecContext, ovi->m_pVideoCodec,
                                nullptr)) < 0)
    {
        g_Log->Error("avcodec_open failed for %s", _filename.c_str());
        return false;
    }
    ovi->m_pFrame = av_frame_alloc();
    if (ovi->m_pVideoStream->nb_frames > 0)
        ovi->m_TotalFrameCount =
        static_cast<uint32_t>(ovi->m_pVideoStream->nb_frames);
    else
        ovi->m_TotalFrameCount = uint32_t((
                                           (((double)ovi->m_pFormatContext->duration / (double)AV_TIME_BASE)) /
                                           av_q2d(ovi->m_pVideoStream->avg_frame_rate) +
                                           .5));
    
    ovi->m_ReadingTrailingFrames = false;
    g_Log->Info("Open done()");
    
    return true;
}

/*
 */
void CContentDecoder::Close()
{
    g_Log->Info("Closing...");
    Stop();
    ClearQueue();
    FinalizeCacheFile();
    
    if (m_CurrentVideoInfo && m_CurrentVideoInfo->m_pFormatContext)
    {
        if (m_IsStreaming && m_CurrentVideoInfo->m_pFormatContext->pb)
        {
            av_freep(&m_CurrentVideoInfo->m_pFormatContext->pb->buffer);
            avio_context_free(&m_CurrentVideoInfo->m_pFormatContext->pb);
        }
        avformat_close_input(&m_CurrentVideoInfo->m_pFormatContext);
    }


    CloseCacheFile();
    m_CacheWritePosition = 0;

    Destroy();

    
    if (m_pIOContext)
    {
        avio_close(m_pIOContext);
        m_pIOContext = nullptr;
    }
    
    if (m_pIOBuffer)
    {
        av_free(m_pIOBuffer);
        m_pIOBuffer = nullptr;
    }
    
    avformat_network_deinit();
    g_Log->Info("closed...");
}

int CContentDecoder::ReadPacket(void* opaque, uint8_t* buf, int buf_size)
{
    CContentDecoder* decoder = static_cast<CContentDecoder*>(opaque);
    int bytesRead = avio_read(decoder->m_pIOContext, buf, buf_size);
    
    if (bytesRead > 0 && decoder->m_IsStreaming) {
        int64_t currentPos = avio_tell(decoder->m_pIOContext) - bytesRead;
        decoder->WriteToCache(buf, bytesRead, currentPos);
    }
    return bytesRead;
}

int64_t CContentDecoder::SeekPacket(void* opaque, int64_t offset, int whence)
{
    CContentDecoder* decoder = static_cast<CContentDecoder*>(opaque);
    if (whence & AVSEEK_SIZE) {
        return avio_size(decoder->m_pIOContext);
    }
    int64_t ret = avio_seek(decoder->m_pIOContext, offset, whence);
    if (ret >= 0) {
        std::lock_guard<std::mutex> lock(decoder->m_CacheMutex);
        decoder->m_CacheWritePosition = ret;
    }
    return ret;
}

CVideoFrame* CContentDecoder::ReadOneFrame()
{
    sOpenVideoInfo* ovi = m_CurrentVideoInfo.get();
    if (ovi == nullptr)
        return nullptr;
    
    AVFormatContext* pFormatContext = ovi->m_pFormatContext;
    
    if (!pFormatContext)
        return nullptr;
    
    AVRational timeBase =
    pFormatContext->streams[ovi->m_VideoStreamID]->time_base;
    int64_t frameRate = (int64_t)av_q2d(
                                        pFormatContext->streams[ovi->m_VideoStreamID]->avg_frame_rate);
    AVPacket* packet;
    AVPacket* filteredPacket;
    int frameDecoded = 0;
    AVFrame* pFrame = ovi->m_pFrame;
    AVCodecContext* pVideoCodecContext = ovi->m_pVideoCodecContext;
    CVideoFrame* pVideoFrame = nullptr;
    int64_t frameNumber = 0;
    
    while (true)
    {
        packet = av_packet_alloc();
        filteredPacket = av_packet_alloc();
        
        if (!ovi->m_ReadingTrailingFrames)
        {
            if (av_read_frame(pFormatContext, packet) < 0)
            {
                ovi->m_ReadingTrailingFrames = true;
                av_packet_free(&packet);
                av_packet_free(&filteredPacket);
                continue;
            }
        }
        
        int ret = 0;
        AVPacket* packetToSend = nullptr;
        if (ovi->m_pBsfContext)
        {
            DumpError(av_bsf_send_packet(ovi->m_pBsfContext, packet));
            ret = av_bsf_receive_packet(ovi->m_pBsfContext, filteredPacket);
            if (ret < 0)
            {
                if (ret == AVERROR_EOF)
                {
                    av_packet_free(&packet);
                    av_packet_free(&filteredPacket);
                    m_HasEnded.exchange(true);
                    ovi->m_CurrentFrameIndex = ovi->m_TotalFrameCount - 1;
                    return nullptr;
                }
                g_Log->Error(
                             "Error receiving packet from bit stream filter: %s",
                             UNFFERRTAG(ret));
            }
            if (filteredPacket->size)
            {
                packetToSend = filteredPacket;
            }
            else
            {
                av_packet_free(&packet);
                av_packet_free(&filteredPacket);
                continue;
            }
        }
        else
        {
            packetToSend = packet;
        }
        
        if (packetToSend)
        {
            ret = avcodec_send_packet(pVideoCodecContext, packetToSend);
        }
        
        if (packet) {
            if (packet->stream_index != ovi->m_VideoStreamID)
            {
                g_Log->Error("FFmpeg Mismatching stream ID");
                break;
            }
        }
            
        
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                av_packet_free(&packet);
                av_packet_free(&filteredPacket);
                // Only mark as ended if we've already processed the last frame
                if (ovi->m_CurrentFrameIndex >= ovi->m_TotalFrameCount - 1) {
                    m_HasEnded.exchange(true);
                    return nullptr;
                }
                
                // Otherwise, try to read the last frame
                ovi->m_ReadingTrailingFrames = true;
                continue;
                /*m_HasEnded.exchange(true);
                ovi->m_CurrentFrameIndex = ovi->m_TotalFrameCount - 1;
                return nullptr;*/
            }
            g_Log->Error("FFmpeg Error sending packet for decoding: %i:%s", ret,
                         UNFFERRTAG(ret));
        }
        if (ret >= 0)
        {
            ret = avcodec_receive_frame(pVideoCodecContext, pFrame);
            
            frameNumber =
            (int64_t)((pFrame->pts * frameRate) * av_q2d(timeBase));
            ovi->m_CurrentFrameIndex = frameNumber;

            g_Log->Info("Read : %d ret : %d", frameNumber, ret);

            if (ret == AVERROR(EAGAIN))
            {
                av_packet_free(&packet);
                av_packet_free(&filteredPacket);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                continue;
            }
            if (ret == AVERROR_EOF)
            {
                if (ovi->m_CurrentFrameIndex < ovi->m_TotalFrameCount - 1) {
                    g_Log->Info("Got EOF but expecting more frames, trying to flush decoder");
                    ovi->m_ReadingTrailingFrames = true;
                    av_packet_free(&packet);
                    av_packet_free(&filteredPacket);
                    
                    // Send a NULL packet to flush the decoder
                    avcodec_send_packet(pVideoCodecContext, nullptr);
                    continue;
                } else {
                    // Normal EOF handling
                    m_HasEnded.exchange(true);
                    return nullptr;
                }

                /*
                av_packet_free(&packet);
                av_packet_free(&filteredPacket);
                return nullptr; // the codec has been fully flushed, and there will
                // be no more output frames */
            }
            else if (ret < 0)
            {
                g_Log->Error("FFmpeg Error decoding: %s", UNFFERRTAG(ret));
            }
            frameDecoded = 1;
        }
        av_packet_unref(packet);
        av_packet_unref(filteredPacket);
        
        if (frameDecoded != 0 || ovi->m_ReadingTrailingFrames)
        {
            if (ovi->m_CurrentFrameIndex >= ovi->m_SeekTargetFrame)
            {
                break;
            }
            else
            {
                av_frame_unref(pFrame);
            }
        }
        
        av_packet_free(&packet);
        av_packet_free(&filteredPacket);
    }
    
    //	Do we have a fresh frame?
    if (frameDecoded != 0)
    {
        // g_Log->Info( "frame decoded" );
        
        if (USE_HW_ACCELERATION)
        {
            pVideoFrame = new CVideoFrame(pFrame, frameNumber,
                                          std::string(pFormatContext->url));
        }
        else
        {
            //    If the decoded video has a different resolution, delete the
            //    scaler to trigger it to be recreated.
            if (m_ScalerWidth != (uint32_t)pVideoCodecContext->width ||
                m_ScalerHeight != (uint32_t)pVideoCodecContext->height)
            {
                g_Log->Info("size doesn't match, recreating");
                
                if (m_pScaler)
                {
                    g_Log->Info("deleting m_pScalar");
                    sws_freeContext(m_pScaler);
                    m_pScaler = nullptr;
                }
            }
            //    Make sure scaler is created.
            if (m_pScaler == nullptr)
            {
                g_Log->Info("creating m_pScaler");
                
                m_pScaler = sws_getContext(
                                           pVideoCodecContext->width, pVideoCodecContext->height,
                                           pVideoCodecContext->pix_fmt, pVideoCodecContext->width,
                                           pVideoCodecContext->height, m_WantedPixelFormat,
                                           SWS_BICUBIC, nullptr, nullptr, nullptr);
                
                //    Store width & height now...
                m_ScalerWidth =
                static_cast<uint32_t>(pVideoCodecContext->width);
                m_ScalerHeight = (uint32_t)pVideoCodecContext->height;
                
                if (m_pScaler == nullptr)
                    g_Log->Warning("scaler == null");
            }
            pVideoFrame =
            new CVideoFrame(pVideoCodecContext, m_WantedPixelFormat,
                            std::string(pFormatContext->url));
            AVFrame* pDest = pVideoFrame->Frame();
            
            // printf( "calling sws_scale()" );
            sws_scale(m_pScaler, pFrame->data, pFrame->linesize, 0,
                      pVideoCodecContext->height, pDest->data, pDest->linesize);
        }
        
        av_frame_unref(pFrame);
        
        pVideoFrame->SetMetaData_DecodeFps(ovi->m_DecodeFps);
        pVideoFrame->SetMetaData_IsSeam(ovi->m_NextIsSeam);
        pVideoFrame->SetMetaData_FrameIdx((uint32_t)ovi->m_CurrentFrameIndex);
        pVideoFrame->SetMetaData_MaxFrameIdx(ovi->m_TotalFrameCount);
    }
    
    av_packet_free(&packet);
    av_packet_free(&filteredPacket);
    
    g_Log->Info("Decoder produced frame %d/%d for %s",
                (uint32_t)ovi->m_CurrentFrameIndex,
                ovi->m_TotalFrameCount,
                ovi->m_Path.c_str());
    
    return pVideoFrame;
}

// MARK: Read Frames Thread
void CContentDecoder::ReadFramesThread()
{
    try
    {
        PlatformUtils::SetThreadName("ReadFrames");
        g_Log->Info("Main video frame reading thread started for %s", m_Metadata.dreamData.uuid.c_str());
        
        // Force decoder to start at frame 0
        m_CurrentVideoInfo->m_CurrentFrameIndex = 0;
        //m_CurrentVideoInfo->m_SeekTargetFrame = 0;
        
        // Force a seek to beginning to ensure we get frame 0
        avformat_seek_file(m_CurrentVideoInfo->m_pFormatContext,
                           m_CurrentVideoInfo->m_VideoStreamID, 0, 0, 0, 0);
        avcodec_flush_buffers(m_CurrentVideoInfo->m_pVideoCodecContext);

        // tmp
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        while (m_CurrentVideoInfo->m_CurrentFrameIndex <
               m_CurrentVideoInfo->m_TotalFrameCount - 2)
        {
            // Check for shutdown
            if (m_isShuttingDown.load()) {
                g_Log->Info("Shutdown detected in read frames thread");
                return;
            }

            this_thread::interruption_point();
            
            PROFILER_BEGIN("Main Video Decoder Frame");
            
            float skipTime = m_SkipForward.load();
            
            if (fpclassify(skipTime) != FP_ZERO)
            {
                m_CurrentVideoInfo->m_SeekTargetFrame =
                m_CurrentVideoInfo->m_CurrentFrameIndex +
                (int64_t)(skipTime * 20);
                m_CurrentVideoInfo->m_SeekTargetFrame =
                std::max(m_CurrentVideoInfo->m_SeekTargetFrame, (int64_t)0);

                // never go beyond last frame, keep 20 frames so decoder doesn't stall
                m_CurrentVideoInfo->m_SeekTargetFrame = std::min(m_CurrentVideoInfo->m_SeekTargetFrame, (int64_t)m_CurrentVideoInfo->m_TotalFrameCount - 20);
                m_SkipForward.exchange(0.f);
            }

            if (m_CurrentVideoInfo->m_SeekTargetFrame != -1)
            {
                m_FrameQueueMutex.lock();
                ClearQueue();
                AVRational timeBase =
                m_CurrentVideoInfo->m_pFormatContext
                ->streams[m_CurrentVideoInfo->m_VideoStreamID]
                ->time_base;
                int64_t frameRate = (int64_t)av_q2d(
                                                    m_CurrentVideoInfo->m_pFormatContext
                                                    ->streams[m_CurrentVideoInfo->m_VideoStreamID]
                                                    ->avg_frame_rate);
                

                
                // Calculate target timestamp in stream time base
                int64_t targetTimestamp =
                (int64_t)(m_CurrentVideoInfo->m_SeekTargetFrame /
                          (frameRate * av_q2d(timeBase)));
                
                // Seek to the target timestamp
                int seek =
                avformat_seek_file(m_CurrentVideoInfo->m_pFormatContext,
                                   m_CurrentVideoInfo->m_VideoStreamID, 0,
                                   targetTimestamp, targetTimestamp, 0);
                avcodec_flush_buffers(m_CurrentVideoInfo->m_pVideoCodecContext);
                if (seek < 0)
                {
                    g_Log->Error("Error seeking:%i", seek);
                }
            }
            CVideoFrame* pMainVideoFrame = ReadOneFrame();
            PROFILER_END("Main Video Decoder Frame");
            if (pMainVideoFrame)
            {
                m_FrameQueue.push(pMainVideoFrame);
            }
            else
            {
                // We've reached the end of the stream
                m_HasEnded.store(true);
                break;
            }
            
            if (m_CurrentVideoInfo->m_SeekTargetFrame != -1)
                m_FrameQueueMutex.unlock();
            //m_CurrentVideoInfo->m_SeekTargetFrame = -1;
        }
        
        // Only try grabbing the remainder of the video if we have a md5 to verify it
        if (m_IsStreaming && !m_Metadata.dreamData.md5.empty()) {
            // Try to read any remaining data
            unsigned char buffer[4096];
            int bytesRead;
            while ((bytesRead = avio_read(m_pIOContext, buffer, sizeof(buffer))) > 0) {
                WriteToCache(buffer, bytesRead, avio_tell(m_pIOContext) - bytesRead);
            }
        }

        m_HasEnded.store(true);
        
        FinalizeCacheFile();

        g_Log->Info("Ending main video frame reading thread for %s", m_Metadata.dreamData.uuid.c_str());
    }
    catch (thread_interrupted const&)
    {
        g_Log->Info("Ending main video frame reading thread for %s", m_Metadata.dreamData.uuid.c_str());

        // Before opening a thread to grab the remainder of the video ,make sure we intend to save it
        if (!m_isShuttingDown.load() && m_IsStreaming && !m_Metadata.dreamData.md5.empty()) {
            g_Log->Info("Read frames thread interrupted, trying to fetch remaining data asynchronously.");
            m_futures.emplace_back(std::async(std::launch::async, [this]() {
                // Try to read any remaining data
                unsigned char buffer[4096];
                int bytesRead;
                while ((bytesRead = avio_read(m_pIOContext, buffer, sizeof(buffer))) > 0) {
                    WriteToCache(buffer, bytesRead, avio_tell(m_pIOContext) - bytesRead);
                }
                
                m_HasEnded.store(true);
                FinalizeCacheFile();
            }));
        }
    }
    catch (std::exception const& e)
    {
        g_Log->Error("Exception in read frames thread: %s", e.what());
    }
}

spCVideoFrame CContentDecoder::PopVideoFrame()
{
    std::scoped_lock l(m_FrameQueueMutex);
    CVideoFrame* tmp = nullptr;
    g_Log->Info("FrameQueue : %d", m_FrameQueue.size());

    m_FrameQueue.pop(tmp, false);
   
    if (tmp == nullptr) {
        g_Log->Info("can't pop");
    }
    
    return spCVideoFrame{tmp};
}

bool CContentDecoder::Start(const sClipMetadata& metadata, int64_t _seekFrame)
{
    if (m_HasStarted.load()) {
        g_Log->Info("decoder already started");
        return true;
    }
    
    m_HasStarted.exchange(true);
    
    m_Metadata = metadata;
    m_CurrentVideoInfo = std::make_unique<sOpenVideoInfo>();
    m_CurrentVideoInfo->m_Path = metadata.path;
    m_CurrentVideoInfo->m_SeekTargetFrame = _seekFrame;
    m_HasEnded.exchange(false);
    
    if (!Open())
        return false;
    
    //	Start by opening, so we have a context to work with.
    m_bStop = false;
    m_pDecoderThread =
    new thread(bind(&CContentDecoder::ReadFramesThread, this));
    
    return true;
}

void CContentDecoder::Stop()
{
    m_bStop = true;
    
    if (m_pDecoderThread)
    {
        m_pDecoderThread->interrupt();
        m_pDecoderThread->join();
        SAFE_DELETE(m_pDecoderThread);
    }
}

void CContentDecoder::ClearQueue(uint32_t leave)
{
    while (m_FrameQueue.size() > leave)
    {
        CVideoFrame* vf;
        
        if (m_FrameQueue.pop(vf, false, false))
        {
            delete vf;
        }
    }
}

uint32_t CContentDecoder::QueueLength()
{
    return (uint32_t)m_FrameQueue.size();
}

sOpenVideoInfo::~sOpenVideoInfo()
{
    if (m_pVideoCodecContext)
    {
        avcodec_close(m_pVideoCodecContext);
        m_pVideoCodecContext = nullptr;
    }
    
    if (m_pFormatContext)
    {
        avformat_close_input(&m_pFormatContext);
    }
    
    if (m_pFrame)
    {
        av_frame_free(&m_pFrame);
        m_pFrame = nullptr;
    }
    if (m_pBsfContext)
    {
        av_bsf_flush(m_pBsfContext);
        av_bsf_free(&m_pBsfContext);
    }
}
// MARK: - Stream caching
void CContentDecoder::SetCachePath(const std::string& path) {
    m_CachePath = path;
}

bool CContentDecoder::OpenCacheFile() {
    if (m_CachePath.empty()) {
        return false;
    }
    m_CacheFile = fopen(m_CachePath.c_str(), "wb");
    return m_CacheFile != nullptr;
}

void CContentDecoder::CloseCacheFile() {
    if (m_CacheFile) {
        fclose(m_CacheFile);
        m_CacheFile = nullptr;
    }
}

void CContentDecoder::WriteToCache(const uint8_t* buf, int buf_size, int64_t position)
{
    if (m_CacheFile && buf && buf_size > 0) {
        std::lock_guard<std::mutex> lock(m_CacheMutex);
        if (position == m_CacheWritePosition) {
            fseek(m_CacheFile, m_CacheWritePosition, SEEK_SET);
            fwrite(buf, 1, buf_size, m_CacheFile);
            fflush(m_CacheFile);
            m_CacheWritePosition += buf_size;
        }
    }
}

std::string CContentDecoder::GenerateCacheFileName() {
    std::string uuid = m_Metadata.dreamData.uuid;
    fs::path mp4Path = Cache::PathManager::getInstance().mp4Path();
    return (mp4Path / (uuid + ".tmp")).string();
}

bool CContentDecoder::IsDownloadComplete() const
{
    if (!m_IsStreaming || !m_CurrentVideoInfo)
        return false;

    // Check if we've reached the end of the stream, avio_size can be innacurate in some cases
    int64_t totalSize = avio_size(m_pIOContext);
    constexpr int64_t threshold = 16 * 1024;  // 16 KB threshold

    //g_Log->Info("POS : %lld %lld", m_CacheWritePosition, totalSize);

    return m_HasEnded.load() && (m_CacheWritePosition >= totalSize - threshold);
}

void CContentDecoder::FinalizeCacheFile()
{
    // Don't try to finalize during shutdown
    if (m_isShuttingDown.load()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_CacheMutex);

    if (m_IsStreaming && IsDownloadComplete() && !m_CachePath.empty())
    {
        CloseCacheFile();  // Close the current .tmp file

        fs::path tmpPath(m_CachePath);
        fs::path finalPath = tmpPath.parent_path() / (tmpPath.stem().string() + ".mp4");

        // Only proceed with verification if we have an MD5 hash
        if (!m_Metadata.dreamData.md5.empty()) {
            std::string downloadedMd5 = PlatformUtils::CalculateFileMD5(tmpPath.string());
            if (downloadedMd5 != m_Metadata.dreamData.md5) {
                g_Log->Error("Streaming MD5 mismatch for %s. Expected: %s, Got: %s",
                             m_Metadata.dreamData.uuid.c_str(),
                             m_Metadata.dreamData.md5.c_str(),
                             downloadedMd5.c_str());
                EDreamClient::ReportMD5Failure(m_Metadata.dreamData.uuid, downloadedMd5, true);
                fs::remove(tmpPath);
                m_CachePath.clear();
                return;
            }
        } else {
            // No MD5 available, don't save the file
            g_Log->Info("No md5 available for verification, discarding stream.");
            fs::remove(tmpPath);
            m_CachePath.clear();
            return;
        }
        
        try
        {
            if (fs::exists(tmpPath) && !fs::exists(finalPath))
            {
                fs::rename(tmpPath, finalPath);
                g_Log->Info("Successfully renamed cache file from %s to %s", tmpPath.string().c_str(), finalPath.string().c_str());
                m_CachePath.clear();  // Clear the cache path to indicate we've finalized
                
                // Now we let our cache know the file is there
                Cache::CacheManager::DiskCachedItem newDiskItem;
                newDiskItem.uuid = m_Metadata.dreamData.uuid;
                newDiskItem.version = m_Metadata.dreamData.video_timestamp;
                newDiskItem.downloadDate = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

                Cache::CacheManager& cm = Cache::CacheManager::getInstance();
                cm.addDiskCachedItem(newDiskItem);
            }
        }
        catch (const fs::filesystem_error& e)
        {
            g_Log->Error("Failed to rename cache file: %s", e.what());
        }
    }
}

} // namespace ContentDecoder
