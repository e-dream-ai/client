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
#ifndef _FRAME_H_
#define _FRAME_H_

#include "AlignedBuffer.h"
#include "SmartPtr.h"
#include "base.h"
#include "linkpool.h"

namespace ContentDecoder
{
class CVideoFrame;

MakeSmartPointers(CVideoFrame);

struct sMetaData
{
    float m_Fade;
    std::string m_FileName;
    std::string m_Name;
    std::string m_Author;
    uint32_t m_SheepID;
    uint32_t m_SheepGeneration;
    time_t m_LastAccessTime;
    bool m_IsEdge;
    spCVideoFrame m_SecondFrame;
    bool m_IsSeam;
    float m_TransitionProgress;
    uint32_t m_FrameIdx;
    uint32_t m_MaxFrameIdx;
};

/*
        CVideoFrame.
        Base class for a decoded video frame.
        Will converts itself to specified format if needed.
*/
class CVideoFrame
{
  protected:
    uint32_t m_Width;
    uint32_t m_Height;
    double m_Pts;

    sMetaData m_MetaData;

    Base::spCAlignedBuffer m_spBuffer;
    AVFrame* m_pFrame;

  public:
    CVideoFrame(AVCodecContext* _pCodecContext, AVPixelFormat _format,
                std::string _filename)
        : m_pFrame(NULL)
    {
        assert(_pCodecContext);
        if (_pCodecContext == NULL)
            g_Log->Info("_pCodecContext == NULL");

        m_MetaData.m_Fade = 1.f;
        m_MetaData.m_FileName = _filename;
        m_MetaData.m_LastAccessTime = 0;
        m_MetaData.m_SheepID = 0;
        m_MetaData.m_SheepGeneration = 0;
        m_MetaData.m_Name = "";
        m_MetaData.m_Author = "";
        m_MetaData.m_IsEdge = false;
        m_MetaData.m_IsSeam = false;
        m_MetaData.m_SecondFrame = NULL;
        m_MetaData.m_TransitionProgress = 0.f;

        m_Width = static_cast<uint32_t>(_pCodecContext->width);
        m_Height = static_cast<uint32_t>(_pCodecContext->height);

        m_pFrame = av_frame_alloc();

        if (m_pFrame != NULL)
        {
            int numBytes = av_image_get_buffer_size(
                _format, _pCodecContext->width, _pCodecContext->height, 1);
            m_spBuffer = std::make_shared<Base::CAlignedBuffer>(
                static_cast<uint32_t>(numBytes) * sizeof(uint8_t));
            uint8_t* buffer = m_spBuffer->GetBufferPtr();
            int width = _pCodecContext->width;
            int height = _pCodecContext->height;

            int ret = av_image_fill_arrays(m_pFrame->data, m_pFrame->linesize,
                                           buffer, _format, width, height, 1);
            if (ret < 0)
                g_Log->Error("av_image_copy_to_buffer error %i", ret);
        }
        else
        {
            g_Log->Error("m_pFrame == NULL");
            m_spBuffer = nullptr;
        }
    }

    CVideoFrame(const AVFrame* _pFrame, std::string _filename)
    {
        m_pFrame = av_frame_alloc();
        av_frame_ref(m_pFrame, _pFrame);
        m_MetaData.m_Fade = 1.f;
        m_MetaData.m_FileName = _filename;
        m_MetaData.m_LastAccessTime = 0;
        m_MetaData.m_SheepID = 0;
        m_MetaData.m_SheepGeneration = 0;
        m_MetaData.m_Name = "";
        m_MetaData.m_Author = "";
        m_MetaData.m_IsEdge = false;
        m_MetaData.m_IsSeam = false;
        m_MetaData.m_SecondFrame = NULL;
        m_MetaData.m_TransitionProgress = 0.f;
        m_Width = static_cast<uint32_t>(_pFrame->width);
        m_Height = static_cast<uint32_t>(_pFrame->height);
        m_spBuffer = nullptr;

        if (m_pFrame == NULL)
        {
            g_Log->Error("m_pFrame == NULL");
        }
    }

    virtual ~CVideoFrame()
    {
        if (m_pFrame)
        {
            av_frame_unref(m_pFrame);
            av_frame_free(&m_pFrame);
        }
        m_spBuffer = nullptr;
    }

    inline void GetMetaData(sMetaData& _metadata) { _metadata = m_MetaData; }

    inline void SetMetaData_Fade(float _fade) { m_MetaData.m_Fade = _fade; }

    inline void SetMetaData_FileName(std::string _filename)
    {
        m_MetaData.m_FileName = _filename;
    }

    inline void SetMetaData_SheepID(uint32_t _sheepid)
    {
        m_MetaData.m_SheepID = _sheepid;
    }

    inline void SetMetaData_SheepGeneration(uint32_t _sheepgeneration)
    {
        m_MetaData.m_SheepGeneration = _sheepgeneration;
    }

    inline void SetMetaData_DreamName(const std::string& _name)
    {
        m_MetaData.m_Name = _name;
    }

    inline void SetMetaData_DreamAuthor(const std::string& _author)
    {
        m_MetaData.m_Author = _author;
    }

    inline void SetMetaData_IsEdge(bool _isedge)
    {
        m_MetaData.m_IsEdge = _isedge;
    }

    inline void SetMetaData_atime(time_t _atime)
    {
        m_MetaData.m_LastAccessTime = _atime;
    }

    inline void SetMetaData_SecondFrame(CVideoFrame* pSecondFrame)
    {
        m_MetaData.m_SecondFrame = spCVideoFrame{pSecondFrame};
    }

    inline void SetMetaData_IsSeam(bool bIsSeam)
    {
        m_MetaData.m_IsSeam = bIsSeam;
    }

    inline void SetMetaData_TransitionProgress(float progress)
    {
        m_MetaData.m_TransitionProgress = progress;
    }

    inline void SetMetaData_FrameIdx(uint32_t idx)
    {
        m_MetaData.m_FrameIdx = idx;
    }

    inline void SetMetaData_MaxFrameIdx(uint32_t idx)
    {
        m_MetaData.m_MaxFrameIdx = idx;
    }

    inline void Pts(const double _pts) { m_Pts = _pts; };
    inline double Pts(void) { return m_Pts; };
    inline uint32_t Width() { return m_Width; };
    inline uint32_t Height() { return m_Height; };

    inline AVFrame* Frame() { return m_pFrame; };

    virtual inline uint8_t* Data()
    {
        if (!m_pFrame)
            return NULL;

        return m_pFrame->data[0];
    };

    virtual inline Base::spCAlignedBuffer& StorageBuffer()
    {
        return m_spBuffer;
    };

    virtual void CopyBuffer()
    {
        if (!m_spBuffer)
            return;
        Base::CAlignedBuffer* newBuffer =
            new Base::CAlignedBuffer(m_spBuffer->Size());

        memcpy(newBuffer->GetBufferPtr(), m_spBuffer->GetBufferPtr(),
               m_spBuffer->Size());

        m_spBuffer = Base::spCAlignedBuffer(newBuffer);
    };

    virtual inline int32_t Stride()
    {
        if (!m_pFrame)
            return 0;

        return m_pFrame->linesize[0];
    };

    // POOLED( CVideoFrame, Memory::CLinkPool );
};

} // namespace ContentDecoder

#endif
