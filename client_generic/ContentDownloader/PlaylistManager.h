//
//  PlaylistManager.hpp
//  All
//
//  Created by Guillaume Louel on 13/08/2024.
//

#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include "CacheManager.h"
#include "Dream.h"

class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager();

    // Initialize the playlist with it's uuid and a list of dream UUIDs
    bool initializePlaylist(const std::string& playlistUUID);

    // Get a dream by its UUID, set position if found in playlist, return nullopt if not in playlist
    std::optional<Cache::Dream> getDreamByUUID(const std::string& dreamUUID);

    
    // Get the next dream in the playlist
    Cache::Dream getNextDream();

    // Get the previous dream in the playlist
    Cache::Dream getPreviousDream();

    // Check if there are more dreams in the playlist
    bool hasMoreDreams() const;

    // Get the current dream without advancing the playlist
    Cache::Dream getCurrentDream() const;

    // Set the current position in the playlist
    void setCurrentPosition(size_t position);

    // Get the total number of dreams in the playlist
    size_t getPlaylistSize() const;

    // Get various metadata of the current playlist
    std::string getPlaylistName() const;
    std::string getPlaylistUUID() const;
    bool isPlaylistNSFW() const { return m_isPlaylistNSFW; }
    int64_t getPlaylistTimestamp() const { return m_playlistTimestamp; }

    // Clear the current playlist
    void clearPlaylist();

    // Shuffle the playlist
    void shufflePlaylist();

    // Get playlist information (e.g., name, artist)
    std::tuple<std::string, std::string, bool, int64_t> getPlaylistInfo() const;

    void startPeriodicChecking();
    void stopPeriodicChecking();
    
    std::chrono::seconds getTimeUntilNextCheck() const;
private:
    std::vector<std::string> m_playlist;
    bool m_started;

    std::string m_currentPlaylistUUID;
    std::string m_currentPlaylistName;
    std::string m_currentPlaylistArtist;
    bool m_isPlaylistNSFW;
    int64_t m_playlistTimestamp;
    
    size_t m_currentPosition;
    std::string m_currentDreamUUID;  // Store the UUID of the currently playing dream

    Cache::CacheManager& m_cacheManager;

    // Helper function to get dream metadata
    Cache::Dream getDreamMetadata(const std::string& dreamUUID) const;
    
    std::atomic<bool> m_isCheckingActive;
    std::thread m_checkingThread;
    std::chrono::minutes m_checkInterval{2};
    std::condition_variable m_cv;
    std::mutex m_cvMutex;
    
    
    void periodicCheckThread();
    bool checkForPlaylistChanges();
    bool updatePlaylist(bool alreadyFetched = false);

    bool parsePlaylist(const std::string& playlistUUID);
    size_t findPositionOfDream(const std::string& dreamUUID) const;
    
    std::atomic<std::chrono::steady_clock::time_point> m_nextCheckTime;

    void updateNextCheckTime();
};

#endif // PLAYLIST_MANAGER_H
