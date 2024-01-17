#ifndef _DREAMPLAYLIST_H
#define _DREAMPLAYLIST_H

#include <sstream>
#include <sys/stat.h>
#include <boost/thread.hpp>
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include "Common.h"
#include "ContentDownloader.h"
#include "Log.h"
#include "PlayCounter.h"
#include "Playlist.h"
#include "Settings.h"
#include "SheepDownloader.h"
#include "Shepherd.h"
#include "Timer.h"

using boost::filesystem::directory_iterator;
using boost::filesystem::exists;
using boost::filesystem::extension;
using boost::filesystem::path;

namespace ContentDecoder
{

class CDreamPlaylist : public CPlaylist
{
    boost::mutex m_Lock;

    boost::mutex m_CurrentPlayingLock;

    //	Path to folder to monitor & update interval in seconds.
    path m_Path;
    double m_NormalInterval;
    double m_EmptyInterval;
    double m_Clock;

    Base::CTimer m_Timer;

    bool m_AutoMedian;
    bool m_RandomMedian;
    double m_MedianLevel;
    uint64_t m_FlockMBs;
    uint64_t m_FlockGoldMBs;
    std::queue<std::string> m_List;
    std::queue<std::string> m_FreshList;

    void AutoMedianLevel(uint64_t megabytes)
    {
        if (megabytes < 100)
        {
            g_Log->Info("Flock < 100 MBs AutoMedian = 1.");
            m_MedianLevel = 1.;
        }
        else if (megabytes > 1000)
        {
            g_Log->Info("Flock > 1000 MBs AutoMedian = .25");
            m_MedianLevel = .25;
        }
        else
        {
            m_MedianLevel = 13. / 12. - double(megabytes) / 1200.;
            if (m_MedianLevel > 1.)
                m_MedianLevel = 1.;
            if (m_MedianLevel < .25)
                m_MedianLevel = .25;
            g_Log->Info("Flock 100 - 1000 MBs AutoMedian = %f", m_MedianLevel);
        }
    }

    bool PlayFreshOnesFirst(std::string& _result)
    {
        if (!m_FreshList.empty())
        {
            _result = m_FreshList.front();
            m_FreshList.pop();
            return true;
        }
        return false;
    }

  public:
    CDreamPlaylist(const std::string& _watchFolder) : CPlaylist()
    {
        m_NormalInterval =
            double(g_Settings()->Get("settings.player.NormalInterval", 100));
        m_EmptyInterval = 10.0f;
        m_Clock = 0.0f;
        m_Path = _watchFolder.c_str();
    }

    //
    virtual ~CDreamPlaylist() {}

    //
    virtual bool Add(const std::string& _file)
    {
        boost::mutex::scoped_lock locker(m_Lock);
        m_FreshList.push(_file);
        return true;
    }

    virtual uint32_t Size()
    {
        boost::mutex::scoped_lock locker(m_Lock);
        uint32_t ret = 0;
        ret = static_cast<uint32_t>(m_List.size());
        return (uint32_t)ret;
    }

    virtual void Clear()
    {
        std::queue<std::string> empty;
        std::swap(m_List, empty);
    }

    virtual bool PopFreshlyDownloadedSheep(std::string& _result)
    {
        boost::mutex::scoped_lock locker(m_Lock);
        if (!m_FreshList.empty())
        {
            _result = m_FreshList.front();
            m_FreshList.pop();
            return true;
        }
        return false;
    }

    virtual bool HasFreshlyDownloadedSheep()
    {
        boost::mutex::scoped_lock locker(m_Lock);
        return !m_FreshList.empty();
    }

    virtual bool Next(std::string& _result, bool& _bEnoughSheep,
                      uint32_t _curID, bool& _playFreshSheep,
                      const bool _bRebuild = false, bool _bStartByRandom = true)
    {
        boost::mutex::scoped_lock locker(m_Lock);

        // if ((_playFreshSheep = PlayFreshOnesFirst(_result)))
        // return true;

        double interval = m_EmptyInterval;

        //	Update from directory if enough time has passed, or we're asked
        // to.
        if (_bRebuild) // || ((m_Timer.Time() - m_Clock) > interval) )
        {
            if (g_PlayCounter().ReadOnlyPlayCounts())
            {
                g_PlayCounter().ClosePlayCounts();
                m_FlockMBs =
                    ContentDownloader::Shepherd::GetFlockSizeMBsRecount(0);
                m_FlockGoldMBs =
                    ContentDownloader::Shepherd::GetFlockSizeMBsRecount(1);
            }
            m_Clock = m_Timer.Time();
            auto allSheep =
                ContentDownloader::SheepDownloader::getClientFlock();
            std::vector<ContentDownloader::sDreamMetadata*> sheepList;
            for (auto it = allSheep.begin(); it != allSheep.end(); ++it)
            {
                ContentDownloader::sDreamMetadata* sheep = *it;
                sheepList.push_back(sheep);
            }

            std::sort(sheepList.begin(), sheepList.end(),
                      [](ContentDownloader::sDreamMetadata* a,
                         ContentDownloader::sDreamMetadata* b)
                      { return a->writeTime > b->writeTime; });
            for (auto it = sheepList.begin(); it != sheepList.end(); ++it)
            {
                ContentDownloader::sDreamMetadata* sheep = *it;
                m_List.push(sheep->fileName);
            }
#ifdef DEBUG
            std::vector<std::string> testVideos = g_Settings()->Get(
                "settings.debug.test_playlist", std::vector<std::string>{});
            if (testVideos.size() > 0)
            {
                Clear();
                for (auto vid : testVideos)
                {
                    m_List.push(vid);
                }
            }
#endif
        }

        if (m_List.empty())
            return false;

        _result = m_List.front();
        m_List.pop();

        _bEnoughSheep = false;

        return true;
    }

    virtual bool
    GetDreamMetadata(std::string_view _filePath,
                     ContentDownloader::sDreamMetadata** _outDreamPtr)
    {
        auto allDreams = ContentDownloader::SheepDownloader::getClientFlock();
        for (auto it = allDreams.begin(); it != allDreams.end(); ++it)
        {
            ContentDownloader::sDreamMetadata* dream = *it;
            if (dream->fileName == _filePath)
            {
                *_outDreamPtr = dream;
                return true;
            }
        }
        return false;
    }

    //	Overrides the playlist to play _id next time.
    void Override(const uint32_t /*_id*/)
    {
        boost::mutex::scoped_lock locker(m_Lock);
        // m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Override",
        // "i", _id ) );
    }

    //	Queues _id to be deleted.
    void Delete(const uint32_t /*_id*/)
    {
        boost::mutex::scoped_lock locker(m_Lock);
        // m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Delete",
        // "i", _id ) );
    }
};

MakeSmartPointers(CDreamPlaylist);

} // namespace ContentDecoder

#endif //_DREAMPLAYLIST_H
