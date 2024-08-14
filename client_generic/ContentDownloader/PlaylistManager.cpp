//
//  PlaylistManager.cpp
//  All
//
//  Created by Guillaume Louel on 13/08/2024.
//

#include "PlaylistManager.h"
#include "EDreamClient.h"
#include <algorithm>
#include <random>

PlaylistManager::PlaylistManager()
    : m_currentPosition(0), m_started(false),  m_cacheManager(Cache::CacheManager::getInstance()) {}

PlaylistManager::~PlaylistManager() = default;

void PlaylistManager::initializePlaylist(const std::vector<std::string>& dreamUUIDs) {
    m_playlist = dreamUUIDs;
    m_currentPosition = 0;
    m_started = false;
}

Cache::Dream PlaylistManager::getNextDream() {
    if (!m_started) {
        // This path is called the first time we ask for a dream. Make sure we give the first one
        m_started = true;
        m_currentPosition = 0;
    } else {
        if (m_currentPosition < m_playlist.size() - 1) {
            m_currentPosition++;
        } else {
            m_currentPosition = 0; // Loop back to the beginning
        }
    }

    return getDreamMetadata(m_playlist[m_currentPosition]);
}

std::optional<Cache::Dream> PlaylistManager::getDreamByUUID(const std::string& dreamUUID) {
    // First, check if the dream is in the playlist
    auto it = std::find(m_playlist.begin(), m_playlist.end(), dreamUUID);
    if (it == m_playlist.end()) {
        // Dream is not in the playlist
        return std::nullopt;
    }

    // Playlist is now playing
    m_started = true;
    
    // Dream is in the playlist, update the position
    size_t newPosition = std::distance(m_playlist.begin(), it);
    setCurrentPosition(newPosition);

    // Return metatada
    return getDreamMetadata(m_playlist[m_currentPosition]);
}


Cache::Dream PlaylistManager::getPreviousDream() {
    if (m_currentPosition > 0) {
        m_currentPosition--;
    } else {
        m_currentPosition = m_playlist.size() - 1; // Loop to the end
    }
    return getDreamMetadata(m_playlist[m_currentPosition]);
}

bool PlaylistManager::hasMoreDreams() const {
    return !m_playlist.empty();
}

Cache::Dream PlaylistManager::getCurrentDream() const {
    if (m_playlist.empty()) {
        return Cache::Dream(); // Return an empty dream if playlist is empty
    }

    return getDreamMetadata(m_playlist[m_currentPosition]);
}

void PlaylistManager::setCurrentPosition(size_t position) {
    if (position < m_playlist.size()) {
        m_currentPosition = position;
    }
}

size_t PlaylistManager::getPlaylistSize() const {
    return m_playlist.size();
}

void PlaylistManager::clearPlaylist() {
    m_playlist.clear();
    m_currentPosition = 0;
}

void PlaylistManager::shufflePlaylist() {
    auto rng = std::default_random_engine {};
    std::shuffle(std::begin(m_playlist), std::end(m_playlist), rng);
    m_currentPosition = 0;
}

std::pair<std::string, std::string> PlaylistManager::getPlaylistInfo() const {
    return EDreamClient::ParsePlaylistCredits(m_currentPlaylistUUID);
}

Cache::Dream PlaylistManager::getDreamMetadata(const std::string& dreamUUID) const {
    Cache::Dream dream;
    if (!m_cacheManager.hasDream(dreamUUID) || m_cacheManager.needsMetadata(dreamUUID, 0)) {
        EDreamClient::FetchDreamMetadata(dreamUUID);
        m_cacheManager.reloadMetadata(dreamUUID);
    }
    dream = *m_cacheManager.getDream(dreamUUID);
    return dream;
}

