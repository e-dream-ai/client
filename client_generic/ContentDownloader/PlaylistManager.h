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
#include "CacheManager.h"
#include "Dream.h"

class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager();

    // Initialize the playlist with a list of dream UUIDs
    void initializePlaylist(const std::vector<std::string>& dreamUUIDs);

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

    // Clear the current playlist
    void clearPlaylist();

    // Shuffle the playlist
    void shufflePlaylist();

    // Get playlist information (e.g., name, artist)
    std::pair<std::string, std::string> getPlaylistInfo() const;

private:
    std::vector<std::string> m_playlist;
    size_t m_currentPosition;
    bool m_started;
    std::string m_currentPlaylistUUID;
    Cache::CacheManager& m_cacheManager;

    // Helper function to get dream metadata
    Cache::Dream getDreamMetadata(const std::string& dreamUUID) const;
};

#endif // PLAYLIST_MANAGER_H
