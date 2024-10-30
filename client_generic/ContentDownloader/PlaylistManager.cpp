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
    : m_currentPosition(0), m_started(false), m_shouldTerminate(false), m_offlineMode(false),  m_cacheManager(Cache::CacheManager::getInstance()) {}

PlaylistManager::~PlaylistManager() {
    stopPeriodicChecking();
}

void PlaylistManager::setOfflineMode(bool offline) {
    m_offlineMode = offline;
}

// MARK: Init
bool PlaylistManager::initializePlaylist(const std::string& playlistUUID) {
    m_initializeInProgress = true;
    m_currentPlaylistUUID = playlistUUID;
    

    if (!m_offlineMode) {
        if (!EDreamClient::FetchPlaylist(playlistUUID)) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", playlistUUID.c_str());
            m_initializeInProgress = false;
            return false;
        }
    }
    
    if (!parsePlaylist(playlistUUID)) {
        if (m_offlineMode) {
            g_Log->Warning("Failed to parse playlist in offline mode. Falling back to offline playlist.");
            initializeOfflinePlaylist();
        } else {
            m_initializeInProgress = false;
            return false;
        }
    }

    if (m_playlist.empty() && m_offlineMode) {
        g_Log->Warning("Parsed playlist is empty in offline mode. Falling back to offline playlist.");
        initializeOfflinePlaylist();
    }
    
    m_currentPosition = 0;
    m_started = false;
    m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0];

    // Start periodic checking if it's not already running. Don't in offline mode though!
    if (!m_isCheckingActive && !m_offlineMode) {
        startPeriodicChecking();
    }

    m_initializeInProgress = false;
    return true;
}

void PlaylistManager::initializeOfflinePlaylist() {
    g_Log->Info("Initializing offline playlist");
    m_playlist.clear();
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    // Get all cached dreams
    auto cachedDreams = cm.getAllCachedDreams();
    
    for (const auto& dream : cachedDreams) {
        if (isDreamProcessed(dream.uuid)) {
            m_playlist.push_back(dream.uuid);
        }
    }
    
    g_Log->Info("Offline playlist initialized with %zu dreams", m_playlist.size());
    
    m_currentPosition = 0;
    m_started = false;
    m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0];
    
    // Set offline playlist metadata
    m_currentPlaylistName = "Offline Playlist";
    m_currentPlaylistArtist = "Local";
    m_isPlaylistNSFW = false;
    m_playlistTimestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}


std::vector<std::string> PlaylistManager::getCurrentPlaylistUUIDs() const {
    return m_playlist;
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

    // Filter out evicted UUIDs and unprocessed dreams
    if (!m_offlineMode) {
        m_playlist = filterActiveAndProcessedDreams(dreamUUIDs);
    } else {
        m_playlist = filterUncachedDreams(dreamUUIDs);
    }

    
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

std::vector<std::string> PlaylistManager::filterActiveAndProcessedDreams(const std::vector<std::string>& dreamUUIDs) const {
    std::vector<std::string> filteredUUIDs;
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    for (const auto& uuid : dreamUUIDs) {
        if (!cm.isUUIDEvicted(uuid) && isDreamProcessed(uuid)) {
            filteredUUIDs.push_back(uuid);
        }
    }
    return filteredUUIDs;
}

std::vector<std::string> PlaylistManager::filterUncachedDreams(const std::vector<std::string>& dreamUUIDs) const {
    std::vector<std::string> filteredUUIDs;
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    for (const auto& uuid : dreamUUIDs) {
        if (cm.hasDiskCachedItem(uuid)) {
            filteredUUIDs.push_back(uuid);
        }
    }
    return filteredUUIDs;
}

bool PlaylistManager::isDreamProcessed(const std::string& uuid) const {
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    if (cm.hasDream(uuid)) {
        const Cache::Dream* dream = cm.getDream(uuid);
        if (dream) {
            return dream->status == "processed";
        }
    }
    return false;
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
const Cache::Dream* PlaylistManager::getNextDream() {
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
    
    // make sure we have a playlist, bail if not
    if (m_playlist.size() <= 0)
    {
        return nullptr;
    }
    
    // Check if our cache is already filled up for the current playlist
    if (m_cacheManager.isPlaylistFillingCache(m_playlist)) {
        g_Log->Info("Cache is already filled up with current playlist, thrashing protection");
        // Find next cached dream
        size_t startPosition = m_currentPosition;
        do {
            if (m_cacheManager.hasDiskCachedItem(m_playlist[m_currentPosition])) {
                break;
            }
            
            if (m_currentPosition < m_playlist.size() - 1) {
                m_currentPosition++;
            } else {
                m_currentPosition = 0;
            }
        } while (m_currentPosition != startPosition);
        
        // If we couldn't find any cached dream, return to original position
        if (m_currentPosition == startPosition && !m_cacheManager.hasDiskCachedItem(m_playlist[m_currentPosition])) {
            g_Log->Warning("No cached dreams found in playlist after full scan");
        }
    }
    
        
    m_currentDreamUUID = m_playlist[m_currentPosition];
    m_currentDream = m_cacheManager.getDream(m_currentDreamUUID);
    
    g_Log->Info("getNextDream : %s (pos: %zu)", m_currentDreamUUID.c_str(), m_currentPosition);
    
    return m_currentDream;
}

std::optional<const Cache::Dream*> PlaylistManager::getDreamByUUID(const std::string& dreamUUID) {
    auto it = std::find(m_playlist.begin(), m_playlist.end(), dreamUUID);
    if (it == m_playlist.end()) {
        g_Log->Error("Dream isn't in the playlist %s", dreamUUID.c_str());
        return std::nullopt;
    }

    // Playlist is now playing
    m_started = true;
    
    // Dream is in the playlist, update the position
    size_t newPosition = std::distance(m_playlist.begin(), it);
    setCurrentPosition(newPosition);

    // Return metadata pointer
    m_currentDream = m_cacheManager.getDream(m_playlist[m_currentPosition]);
    return m_currentDream;
}

const Cache::Dream* PlaylistManager::getPreviousDream() {
    if (m_playlist.empty()) {
        return nullptr;
    }
    
    if (m_currentPosition > 0) {
        m_currentPosition--;
    } else {
        m_currentPosition = m_playlist.size() - 1; // Loop to the end
    }

    if (m_cacheManager.isPlaylistFillingCache(m_playlist)) {
        g_Log->Info("Cache is already filled up with current playlist, thrashing protection");

        size_t startPosition = m_currentPosition;
        do {
            if (m_cacheManager.hasDiskCachedItem(m_playlist[m_currentPosition])) {
                break;
            }
            
            if (m_currentPosition > 0) {
                m_currentPosition--;
            } else {
                m_currentPosition = m_playlist.size() - 1;
            }
        } while (m_currentPosition != startPosition);
        
        if (m_currentPosition == startPosition && !m_cacheManager.hasDiskCachedItem(m_playlist[m_currentPosition])) {
            g_Log->Warning("No cached dreams found in playlist after full scan");
        }
    }
    
    
    g_Log->Info("getPreviousDream : %s (pos: %zu)", m_playlist[m_currentPosition].c_str(), m_currentPosition);

    m_currentDreamUUID = m_playlist[m_currentPosition];
    m_currentDream = m_cacheManager.getDream(m_currentDreamUUID);
    return m_currentDream;
}

bool PlaylistManager::hasMoreDreams() const {
    return !m_playlist.empty();
}

const Cache::Dream* PlaylistManager::getCurrentDream() const {
    if (m_playlist.empty()) {
        g_Log->Error("EMPTY PLAYLIST");
        return nullptr;
    }

    g_Log->Info("getCurrentDream : %s %s (pos: %zu)", m_playlist[m_currentPosition].c_str(), m_currentDreamUUID.c_str(), m_currentPosition);

    return m_currentDream;
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

const Cache::Dream* PlaylistManager::getDreamMetadata(const std::string& dreamUUID) const {
    auto it = std::find_if(m_playlist.begin(), m_playlist.end(),
        [&dreamUUID](const std::string& uuid) {
            return uuid == dreamUUID;
        });

    if (it != m_playlist.end()) {
        const Cache::Dream* dream = m_cacheManager.getDream(dreamUUID);
        if (dream) {
            return dream;
        }
    }

    return nullptr;
}

// MARK: Periodic playlist checks
void PlaylistManager::startPeriodicChecking() {
    if (!m_isCheckingActive.exchange(true)) {
        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            m_shouldTerminate = false;
        }
        updateNextCheckTime();

        m_checkingThread = std::thread(&PlaylistManager::periodicCheckThread, this);
    }
}

void PlaylistManager::stopPeriodicChecking() {
    if (m_isCheckingActive.exchange(false)) {
        {
            std::lock_guard<std::mutex> lock(m_cvMutex);
            m_shouldTerminate = true;
        }

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
    
    while (!m_shouldTerminate) {
        updateNextCheckTime();
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            if (m_cv.wait_for(lock, m_checkInterval, [this] { return m_shouldTerminate; })) {
                // If m_shouldTerminate is true, exit the thread
                break;
            }
        }
        
        if (!m_isCheckingActive.load()) {
            break;
        }
        
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
