//
//  PlaylistManager.cpp
//  All
//
//  Created by Guillaume Louel on 13/08/2024.
//

#include "PlaylistManager.h"
#include "EDreamClient.h"
#include "Log.h"
#include "Player.h"
#include "PlatformUtils.h"
#include <algorithm>
#include <random>

PlaylistManager::PlaylistManager()
    : m_currentPosition(0), m_started(false),  m_cacheManager(Cache::CacheManager::getInstance()) {}

PlaylistManager::~PlaylistManager() {
    stopPeriodicChecking();
}

// MARK: Init
bool PlaylistManager::initializePlaylist(const std::string& playlistUUID) {
    m_currentPlaylistUUID = playlistUUID;
    
    if (!EDreamClient::FetchPlaylist(playlistUUID)) {
        g_Log->Error("Failed to fetch playlist. UUID: %s", playlistUUID.c_str());
        return false;
    }
    
    if (!parsePlaylist(playlistUUID)) {
        return false;
    }

    m_currentPosition = 0;
    m_started = false;
    m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0];

    // Start periodic checking if it's not already running
    if (!m_isCheckingActive) {
        startPeriodicChecking();
    }
    
    return true;
}

bool PlaylistManager::parsePlaylist(const std::string& playlistUUID) {
    // Parse the playlist
    std::vector<std::string> dreamUUIDs = EDreamClient::ParsePlaylist(playlistUUID);
    
    if (dreamUUIDs.empty()) {
        g_Log->Error("Failed to parse playlist or playlist is empty. UUID: %s", playlistUUID.c_str());
        return false;
    }

    // Get the playlist metadata
    auto [playlistName, playlistArtist, isNSFW, timestamp] = EDreamClient::ParsePlaylistMetadata(playlistUUID);

    // Filter out evicted UUIDs
    m_playlist = filterEvictedUUIDs(dreamUUIDs);

    // Update member variables
    m_currentPlaylistName = playlistName;
    m_currentPlaylistArtist = playlistArtist;
    m_isPlaylistNSFW = isNSFW;
    m_playlistTimestamp = timestamp;

    g_Log->Info("Updated playlist: %s by %s (UUID: %s, NSFW: %s, Timestamp: %lld) with %zu dreams",
                m_currentPlaylistName.c_str(), m_currentPlaylistArtist.c_str(),
                m_currentPlaylistUUID.c_str(), m_isPlaylistNSFW ? "Yes" : "No",
                m_playlistTimestamp, m_playlist.size());

    return true;
}

std::vector<std::string> PlaylistManager::filterEvictedUUIDs(const std::vector<std::string>& dreamUUIDs) const {
    std::vector<std::string> filteredUUIDs;
    for (const auto& uuid : dreamUUIDs) {
        if (!Cache::CacheManager::getInstance().isUUIDEvicted(uuid)) {
            filteredUUIDs.push_back(uuid);
        }
    }
    return filteredUUIDs;
}

void PlaylistManager::removeCurrentDream() {
    if (m_playlist.empty()) {
        return; // Nothing to remove
    }

    // Remove the current dream
    m_playlist.erase(m_playlist.begin() + m_currentPosition);

    // Adjust the current position if necessary
    if (m_currentPosition >= m_playlist.size()) {
        m_currentPosition = m_playlist.empty() ? 0 : m_playlist.size() - 1;
    }

    g_Log->Info("Removed dream at position %zu. New playlist size: %zu",
                m_currentPosition, m_playlist.size());

    // If the playlist is now empty, reset the started flag
    if (m_playlist.empty()) {
        m_started = false;
    }
}

// MARK: Getters
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

    m_currentDreamUUID = m_playlist[m_currentPosition];
    
    g_Log->Info("getNextDream : %s (pos: %zu)", m_currentDreamUUID.c_str() , m_currentPosition);
    
    return getDreamMetadata(m_currentDreamUUID);
}

std::optional<Cache::Dream> PlaylistManager::getDreamByUUID(const std::string& dreamUUID) {
    // First, check if the dream is in the playlist
    auto it = std::find(m_playlist.begin(), m_playlist.end(), dreamUUID);
    if (it == m_playlist.end()) {
        g_Log->Error("Dream isn't in the playlist %s", dreamUUID.c_str());
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

    g_Log->Info("getPreviousDream : %s (pos: %zu)", m_playlist[m_currentPosition].c_str() , m_currentPosition);

    return getDreamMetadata(m_playlist[m_currentPosition]);
}

bool PlaylistManager::hasMoreDreams() const {
    return !m_playlist.empty();
}

Cache::Dream PlaylistManager::getCurrentDream() const {
    if (m_playlist.empty()) {
        g_Log->Error("EMPTY PLAYLIST");
        return Cache::Dream(); // Return an empty dream if playlist is empty
    }

    g_Log->Info("getCurrentDream : %s %s (pos: %zu)", m_playlist[m_currentPosition].c_str(), m_currentDreamUUID.c_str() , m_currentPosition);

    return getDreamMetadata(m_playlist[m_currentPosition]);
}

void PlaylistManager::setCurrentPosition(size_t position) {
    if (position < m_playlist.size()) {
        m_currentPosition = position;
    }
}

std::string PlaylistManager::getPlaylistName() const {
    return m_currentPlaylistName.empty() ? "No playlist loaded" : m_currentPlaylistName;
}

std::string PlaylistManager::getPlaylistUUID() const {
    return m_currentPlaylistUUID;
}

size_t PlaylistManager::getPlaylistSize() const {
    return m_playlist.size();
}

void PlaylistManager::clearPlaylist() {
    m_playlist.clear();
    m_currentPosition = 0;
}

// maybe someday?
void PlaylistManager::shufflePlaylist() {
    auto rng = std::default_random_engine {};
    std::shuffle(std::begin(m_playlist), std::end(m_playlist), rng);
    m_currentPosition = 0;
}

std::tuple<std::string, std::string, bool, int64_t> PlaylistManager::getPlaylistInfo() const {
    return {m_currentPlaylistName, m_currentPlaylistArtist, m_isPlaylistNSFW, m_playlistTimestamp};
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

// MARK: Periodic playlist checks
void PlaylistManager::startPeriodicChecking() {
    if (!m_isCheckingActive.exchange(true)) {
        updateNextCheckTime();

        m_checkingThread = std::thread(&PlaylistManager::periodicCheckThread, this);
    }
}

void PlaylistManager::stopPeriodicChecking() {
    if (m_isCheckingActive.exchange(false)) {
        m_cv.notify_one(); // Wake up the thread if it's sleeping
        if (m_checkingThread.joinable()) {
            m_checkingThread.join();
        }
    }
}

void PlaylistManager::updateNextCheckTime() {
    m_nextCheckTime.store(std::chrono::steady_clock::now() + m_checkInterval);
}

void PlaylistManager::periodicCheckThread() {
    PlatformUtils::SetThreadName("PeriodicPlaylistCheck");
    
    while (m_isCheckingActive) {
        updateNextCheckTime();
        // Use a condition variable to wait, allowing for interruption
        std::unique_lock<std::mutex> lock(m_cvMutex);
        m_cv.wait_for(lock, m_checkInterval, [this] { return !m_isCheckingActive; });
        
        checkForPlaylistChanges();
    }
}

bool PlaylistManager::checkForPlaylistChanges() {
    // First, fetch the playlist to ensure we have the latest version
    if (!EDreamClient::FetchPlaylist(m_currentPlaylistUUID)) {
        g_Log->Error("Failed to fetch playlist for checking changes. UUID: %s", m_currentPlaylistUUID.c_str());
        return false;
    }

    // Then parse the metadata to get the new timestamp
    auto [newName, newArtist, newNSFW, newTimestamp] = EDreamClient::ParsePlaylistMetadata(m_currentPlaylistUUID);
    
    g_Log->Info("Old timestamp: %lld, New timestamp: %lld",
                m_playlistTimestamp, newTimestamp);
    
    if (newTimestamp > m_playlistTimestamp) {
        g_Log->Info("Playlist change detected. Old timestamp: %lld, New timestamp: %lld",
                    m_playlistTimestamp, newTimestamp);
        
        // Update the playlist
        if (updatePlaylist()) {
            g_Log->Info("Playlist updated successfully");
            return true;
        } else {
            g_Log->Error("Failed to update playlist");
        }
    } else {
        g_Log->Info("No playlist changes detected");
    }
    
    return false;
}

bool PlaylistManager::updatePlaylist(bool alreadyFetched) {
    std::string currentDreamUUID = m_currentDreamUUID;
    size_t oldPosition = m_currentPosition;

    if (!alreadyFetched) {
        if (!EDreamClient::FetchPlaylist(m_currentPlaylistUUID)) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", m_currentPlaylistUUID.c_str());
            return false;
        }
    }

    if (!parsePlaylist(m_currentPlaylistUUID)) {
        return false;
    }

    // Try to find the position of the current dream in the updated playlist
    size_t newPosition = findPositionOfDream(currentDreamUUID);

    if (newPosition != std::string::npos) {
        // If found, update the current position
        m_currentPosition = newPosition;
    } else {
        // If not found, try to keep a similar relative position
        m_currentPosition = std::min(oldPosition, m_playlist.size() - 1);
    }

    // Update the current dream UUID
    m_currentDreamUUID = m_playlist[m_currentPosition];

    g_Log->Info("Playlist updated. New position: %zu, Current dream UUID: %s (old position: %zu, old uuid : %s)",
                m_currentPosition, m_currentDreamUUID.c_str(), oldPosition, currentDreamUUID.c_str());

    return true;
}


size_t PlaylistManager::findPositionOfDream(const std::string& dreamUUID) const {
    auto it = std::find(m_playlist.begin(), m_playlist.end(), dreamUUID);
    if (it != m_playlist.end()) {
        return std::distance(m_playlist.begin(), it);
    }
    return std::string::npos;
}

std::chrono::seconds PlaylistManager::getTimeUntilNextCheck() const {
    auto now = std::chrono::steady_clock::now();
    auto next = m_nextCheckTime.load();
    auto timeUntilNext = std::chrono::duration_cast<std::chrono::seconds>(next - now);
    return std::max(timeUntilNext, std::chrono::seconds(0));
}
