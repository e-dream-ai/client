#ifndef _DREAMPLAYLIST_H
#define _DREAMPLAYLIST_H

#include <sstream>
#include <sys/stat.h>
#include <boost/thread.hpp>
/*
#include <boost/filesystem/convenience.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
*/
#include <boost/filesystem.hpp>
#include "Common.h"
#include "ContentDownloader.h"
#include "Log.h"
#include "PlayCounter.h"
#include "Playlist.h"
#include "Settings.h"
#include "SheepDownloader.h"
#include "Shepherd.h"
#include "Timer.h"
#include "StringFormat.h"
#include "EDreamClient.h"
#include "CacheManager.h"

// TODOWINDOWS POSIX ONLY
#include <unistd.h>

using boost::filesystem::directory_iterator;
using boost::filesystem::exists;
//using boost::filesystem::extension;
using boost::filesystem::path;
using Shepherd = ContentDownloader::Shepherd;
using path = boost::filesystem::path;

namespace ContentDecoder
{

class CDreamPlaylist : public CPlaylist
{
    std::mutex m_Lock;

    std::mutex m_CurrentPlayingLock;

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
    
    // Infos fetched from remote playlist
public:
    int playlistId = 0;
    std::string playlistName;
    std::string playlistUserName;
    
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

    // @TODO: UNUSED
    bool PlayFreshOnesFirst(std::string& _result)
    {
        // Disable fresh if playlist mode
        if (g_Settings()->Get("settings.content.current_playlist", 0) > 0)
            return false;
        
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
        std::scoped_lock locker(m_Lock);
        m_FreshList.push(_file);
        return true;
    }

    virtual uint32_t Size()
    {
        std::scoped_lock locker(m_Lock);
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
        std::scoped_lock locker(m_Lock);
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
        std::scoped_lock locker(m_Lock);
        return !m_FreshList.empty();
    }

    virtual bool Next(std::string& _result, bool& _bEnoughSheep,
                      uint32_t _curID, bool& _playFreshSheep,
                      const bool _bRebuild = false, bool _bStartByRandom = true)
    {
        std::scoped_lock locker(m_Lock);

        if ((_playFreshSheep = PlayFreshOnesFirst(_result)))
            return true;

        double interval = m_EmptyInterval;

        //	Update from directory if enough time has passed, or we're asked
        // to.
        // Also rebuild if the list is empty...
        if (_bRebuild || m_List.empty()) // || ((m_Timer.Time() - m_Clock) > interval) )
        {
            if (g_PlayCounter().ReadOnlyPlayCounts())
            {
                g_PlayCounter().ClosePlayCounts();
                m_FlockMBs =
                    ContentDownloader::Shepherd::GetFlockSizeMBsRecount(0);
                m_FlockGoldMBs =
                    ContentDownloader::Shepherd::GetFlockSizeMBsRecount(1);
                
                // @TODO: gold ??
            }
            m_Clock = m_Timer.Time();
            
            
            // Are we in playlist mode ? If so we add that list, sorted default
            playlistId = g_Settings()->Get("settings.content.current_playlist", 0);
            if (playlistId >= 0) {
                auto uuids = EDreamClient::ParsePlaylist(playlistId);
 
                auto [name, userName] = EDreamClient::ParsePlaylistCredits(playlistId);
                playlistName = name;
                playlistUserName = userName;
                
                for (auto uuid : uuids) {
                    std::string fileName{
                        string_format("%s%s.mp4", Shepherd::mp4Path(), uuid.c_str())};
                    
                    if (exists(fileName))
                        m_List.push(fileName);
                }
            } /*else {
                ContentDownloader::SheepArray allSheep;
                Shepherd::getClientFlock(&allSheep);    // Grab a truly fresh list
                
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
                    if (exists(sheep->fileName)) {
                        m_List.push(sheep->fileName);
                    }
                }
            }*/
            
#ifdef DEBUG
/*            std::vector<std::string> testVideos = g_Settings()->Get(
                "settings.debug.test_playlist", std::vector<std::string>{});
            if (testVideos.size() > 0)
            {
                Clear();
                for (auto vid : testVideos)
                {
                    m_List.push(vid);
                }
            }*/
#endif
        }
        //printf("ML COUNT %zu", m_List.size());

        if (m_List.empty())
            return false;

        _result = m_List.front();
        m_List.pop();

        //_bEnoughSheep = false;
        _bEnoughSheep = true;

        return true;
    }

    // TODO : use CacheManager instead to get metadata
    /*virtual bool GetDreamMetadata(std::string_view _filePath,
                                  ContentDownloader::sDreamMetadata* _outDream)
    {
        auto allDreams = ContentDownloader::SheepDownloader::getClientFlock();
        for (auto it = allDreams.begin(); it != allDreams.end(); ++it)
        {
            ContentDownloader::sDreamMetadata* dream = *it;
            if (dream->fileName == _filePath)
            {
                *_outDream = *dream;
                return true;
            }
        }
        return false;
    }*/
    
    virtual bool GetDreamMetadata(std::string_view _filePath, Cache::Dream* _outDream)
    {
        Cache::CacheManager& cm = Cache::CacheManager::getInstance();

        // Create a path object to extract the UUID from the path
        std::string pathStr(_filePath);
        boost::filesystem::path path(pathStr);
        
        auto dream = cm.getDream(path.stem().string());
        
        if (dream) {
            *_outDream = *dream;
            return true;
        }
        
        return false;
    }
    

    void SetPlaylist(int id) {
        std::scoped_lock locker(m_Lock);
        playlistId = id;
    }
    
    //	Overrides the playlist to play _id next time.
    void Override(const uint32_t /*_id*/)
    {
        std::scoped_lock locker(m_Lock);
        // m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Override",
        // "i", _id ) );
    }

    //	Queues _id to be deleted.
    void Delete(std::string_view _uuid)
    {
        std::scoped_lock locker(m_Lock);
        ContentDownloader::SheepDownloader::deleteSheep(_uuid);

        // m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Delete",
        // "i", _id ) );
    }
};

MakeSmartPointers(CDreamPlaylist);

} // namespace ContentDecoder

#endif //_DREAMPLAYLIST_H
