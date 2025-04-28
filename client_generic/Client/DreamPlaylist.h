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
#include "PathManager.h"
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

    std::queue<std::string> m_List;
    std::queue<std::string> m_FreshList;
    
    // Infos fetched from remote playlist
public:
    std::string playlistId = "";
    std::string playlistName;
    std::string playlistUserName;
    


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

    virtual bool Next(std::string& _result, bool& _bEnoughSheep,
                      uint32_t _curID, bool& _playFreshSheep,
                      const bool _bRebuild = false, bool _bStartByRandom = true)
    {
        std::scoped_lock locker(m_Lock);

        // Rebuild if the list is empty...
        if (_bRebuild || m_List.empty())
        {
            m_Clock = m_Timer.Time();
            
            playlistId = g_Settings()->Get("settings.content.current_playlist_uuid", std::string(""));
            
            // if playlistID is empty, we use the default playlist file (playlist_0.json)
            auto dreams = EDreamClient::ParsePlaylist(playlistId);

            auto [name, userName, nsfw, timestamp] = EDreamClient::ParsePlaylistMetadata(playlistId);
            playlistName = name;
            playlistUserName = userName;
            
            for (auto dream : dreams) {
                auto fileName = Cache::PathManager::getInstance().mp4Path() / (dream.uuid + ".mp4");
                
                if (exists(fileName))
                    m_List.push(fileName.string());
            }
        }

        if (m_List.empty())
            return false;

        _result = m_List.front();
        m_List.pop();

        //_bEnoughSheep = false;
        _bEnoughSheep = true;

        return true;
    }

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
    

    void SetPlaylist(std::string_view uuid) {
        std::scoped_lock locker(m_Lock);
        playlistId = uuid;
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
        Cache::CacheManager& cm = Cache::CacheManager::getInstance();
        cm.deleteDream(std::string(_uuid));
    }
};

MakeSmartPointers(CDreamPlaylist);

} // namespace ContentDecoder

#endif //_DREAMPLAYLIST_H
