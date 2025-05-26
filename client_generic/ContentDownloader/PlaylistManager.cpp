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
: m_started(false), m_offlineMode(false), m_currentPosition(0), m_cacheManager(Cache::CacheManager::getInstance()),  m_shouldTerminate(false) {}

PlaylistManager::~PlaylistManager() {
    stopPeriodicChecking();
}

void PlaylistManager::setOfflineMode(bool offline) {
    m_offlineMode = offline;
}

// MARK: Init
bool PlaylistManager::initializePlaylist(const std::string& playlistUUID, bool fetchPlaylist = true) {
    m_initializeInProgress = true;
    m_currentPlaylistUUID = playlistUUID;
    

    if (!m_offlineMode && fetchPlaylist) {
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
    m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0].uuid;

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
    m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0].uuid;
    
    // Set offline playlist metadata
    m_currentPlaylistName = "Offline Playlist";
    m_currentPlaylistArtist = "Local";
    m_isPlaylistNSFW = false;
    m_playlistTimestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}


std::vector<std::string> PlaylistManager::getCurrentPlaylistUUIDs() const {
    std::vector<std::string> uuids;
    uuids.reserve(m_playlist.size());
    for (const auto& entry : m_playlist) {
        uuids.push_back(entry.uuid);
    }
    return uuids;
}

bool PlaylistManager::parsePlaylist(const std::string& playlistUUID) {
    // Parse the playlist
    std::vector<PlaylistEntry> entries = EDreamClient::ParsePlaylist(playlistUUID);
    
    if (entries.empty()) {
        g_Log->Error("Failed to parse playlist or playlist is empty. UUID: %s", playlistUUID.c_str());
        return false;
    }

    // Get the playlist metadata
    auto [playlistName, playlistArtist, isNSFW, timestamp] = EDreamClient::ParsePlaylistMetadata(playlistUUID);

    // Filter out evicted UUIDs and unprocessed dreams
    if (!m_offlineMode) {
        m_playlist = filterActiveAndProcessedDreams(entries);
    } else {
        m_playlist = filterUncachedDreams(entries);
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

std::vector<PlaylistEntry> PlaylistManager::filterActiveAndProcessedDreams(const std::vector<PlaylistEntry>& entries) const {
    std::vector<PlaylistEntry> filteredEntries;
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    for (const auto& entry : entries) {
        if (!cm.isUUIDEvicted(entry.uuid) && isDreamProcessed(entry.uuid)) {
            filteredEntries.push_back(entry);
        }
    }
    return filteredEntries;
}

std::vector<PlaylistEntry> PlaylistManager::filterUncachedDreams(const std::vector<PlaylistEntry>& entries) const {
    std::vector<PlaylistEntry> filteredEntries;
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    
    for (const auto& entry : entries) {
        if (cm.hasDiskCachedItem(entry.uuid)) {
            filteredEntries.push_back(entry);
        }
    }
    return filteredEntries;
}

size_t PlaylistManager::countCachedDreamsAhead() const {
    if (m_playlist.empty() || !m_started) {
        return 0;
    }
    
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    size_t cachedCount = 0;
    
    // Start from the position after current
    size_t startPos = m_currentPosition + 1;
    
    // Count cached dreams from current position onwards
    for (size_t i = startPos; i < m_playlist.size(); ++i) {
        if (cm.hasDiskCachedItem(m_playlist[i].uuid)) {
            cachedCount++;
        } else {
            // Stop counting at the first uncached dream
            break;
        }
    }
    
    return cachedCount;
}


bool PlaylistManager::hasKeyframes() const {
    // Check if any dream in the playlist has keyframes (start or end)
    for (const auto& entry : m_playlist) {
        if (entry.startKeyframe.has_value() || entry.endKeyframe.has_value()) {
            return true;
        }
    }
    return false;
}


std::optional<std::string> PlaylistManager::getNextUncachedDream() const {
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    auto& downloader = g_ContentDownloader().m_gDownloader;
    
    // Check if playlist has keyframes and randomly return an uncached dream 10% of the time
    if (hasKeyframes()) {
        // Generate random number between 0 and 99
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 99);
        
        if (dis(gen) < 10) {  // 10% chance
            // Collect all uncached dreams
            std::vector<std::string> uncachedDreams;
            for (const auto& entry : m_playlist) {
                if (!cm.hasDiskCachedItem(entry.uuid) && 
                    !downloader.IsDreamBeingDownloaded(entry.uuid)) {
                    uncachedDreams.push_back(entry.uuid);
                }
            }
            
            // Return a random uncached dream if any exist
            if (!uncachedDreams.empty()) {
                std::uniform_int_distribution<> dreamDis(0, uncachedDreams.size() - 1);
                g_Log->Info("Randomly selecting uncached dream for keyframe playlist");
                return uncachedDreams[dreamDis(gen)];
            }
        }
    }
    
    // For all other cases (90% of keyframe playlists or no keyframes)
    // Build a list of successor dreams based on cached dreams
    std::vector<std::string> successors;
    
    for (const auto& entry : m_playlist) {
        // Only consider cached dreams as starting points
        if (!cm.hasDiskCachedItem(entry.uuid)) {
            continue;
        }
        
        if (entry.endKeyframe.has_value()) {
            // This dream has an end keyframe, find all dreams with matching start keyframe
            for (const auto& succ : m_playlist) {
                if (succ.startKeyframe.has_value() && 
                    *succ.startKeyframe == *entry.endKeyframe &&
                    !cm.hasDiskCachedItem(succ.uuid) &&
                    !downloader.IsDreamBeingDownloaded(succ.uuid)) {
                    // Found a potential keyframe-based successor
                    successors.push_back(succ.uuid);
                }
            }
        } else {
            // No end keyframe, so next dream is the sequential successor
            auto currentIt = std::find_if(m_playlist.begin(), m_playlist.end(),
                [&entry](const PlaylistEntry& e) { return e.uuid == entry.uuid; });
            
            if (currentIt != m_playlist.end()) {
                // Calculate next index with wraparound
                size_t currentIdx = std::distance(m_playlist.begin(), currentIt);
                size_t nextIdx = (currentIdx + 1) % m_playlist.size();
                const auto& succ = m_playlist[nextIdx];
                
                if (!cm.hasDiskCachedItem(succ.uuid) &&
                    !downloader.IsDreamBeingDownloaded(succ.uuid)) {
                    successors.push_back(succ.uuid);
                }
            }
        }
    }
    
    // Remove duplicates from successors
    std::sort(successors.begin(), successors.end());
    successors.erase(std::unique(successors.begin(), successors.end()), successors.end());
    
    // If we have successors, randomly select one
    if (!successors.empty()) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, successors.size() - 1);
        std::string selected = successors[dis(gen)];
        g_Log->Info("Selected successor dream from %zu candidates", successors.size());
        return selected;
    }
    
    // No successors found, try to return any random uncached dream
    std::vector<std::string> allUncachedDreams;
    for (const auto& entry : m_playlist) {
        if (!cm.hasDiskCachedItem(entry.uuid) && 
            !downloader.IsDreamBeingDownloaded(entry.uuid)) {
            allUncachedDreams.push_back(entry.uuid);
        }
    }
    
    if (!allUncachedDreams.empty()) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, allUncachedDreams.size() - 1);
        std::string selected = allUncachedDreams[dis(gen)];
        g_Log->Info("No successors found, returning random uncached dream");
        return selected;
    }
    
    // Everything is cached
    return std::nullopt;
}

size_t PlaylistManager::findPositionOfDream(const std::string& dreamUUID) const {
    auto it = std::find_if(m_playlist.begin(), m_playlist.end(),
                          [&dreamUUID](const PlaylistEntry& entry) {
                              return entry.uuid == dreamUUID;
                          });
    if (it != m_playlist.end()) {
        return std::distance(m_playlist.begin(), it);
    }
    return std::string::npos;
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
PlaylistManager::TransitionType PlaylistManager::determineTransitionType(const PlaylistEntry& current, const PlaylistEntry& next) const {
    // Check for keyframe match
    if (current.endKeyframe && next.startKeyframe &&
        *current.endKeyframe == *next.startKeyframe) {
        return TransitionType::Seamless;
    }
    
    return TransitionType::StandardCrossfade;
}

std::optional<PlaylistManager::NextDreamDecision> PlaylistManager::preflightNextDream() const {
    g_Log->Info("Preflight : start");

    if (m_playlist.empty()) {
        g_Log->Info("Preflight : no playlist");
        return std::nullopt;
    }

    NextDreamDecision decision;
    
    // If we haven't started yet, it would be the first dream
    if (!m_started) {
        const auto& firstEntry = m_playlist[0];
        decision = {
            0,  // Position
            TransitionType::StandardCrossfade,
            m_cacheManager.getDream(firstEntry.uuid),
            firstEntry.startKeyframe,
            firstEntry.endKeyframe
        };
        g_Log->Info("Preflight : startup with a crossfade");
        return decision;
    }

    // Check if current dream has an end keyframe
    const auto& currentEntry = m_playlist[m_currentPosition];
    if (currentEntry.endKeyframe) {
        g_Log->Info("Preflight : has endKeyframe, looking for match");

        // Look for dreams with matching start keyframe
        std::vector<size_t> candidates;
        for (size_t i = 0; i < m_playlist.size(); i++) {
            const auto& entry = m_playlist[i];
            if (i != m_currentPosition && entry.startKeyframe && *entry.startKeyframe == *currentEntry.endKeyframe
                && !isDreamPlayed(entry.uuid)) {
                candidates.push_back(i);
                g_Log->Info("Preflight : adding candidate : %s %zu (current: %zu)", entry.uuid.c_str(), i, m_currentPosition);

            }
        }


        if (!candidates.empty()) {
            g_Log->Info("Preflight : candidates : %zu", candidates.size());

            // Randomly select one of the candidates
            size_t nextPos = candidates[rand() % candidates.size()];
            const auto& nextEntry = m_playlist[nextPos];
            decision = {
                nextPos,
                determineTransitionType(currentEntry, nextEntry),
                m_cacheManager.getDream(nextEntry.uuid),
                nextEntry.startKeyframe,
                nextEntry.endKeyframe
            };
            return decision;
        }
    }

    g_Log->Info("Preflight : either no endkeyframe or no match found");

    // No keyframe match - find next unplayed dream
    if (hasUnplayedDreams()) {
        g_Log->Info("Preflight : has unplayed dreams");

        for (size_t i = 0; i < m_playlist.size(); i++) {
            if (i != m_currentPosition && !isDreamPlayed(m_playlist[i].uuid)) {
                const auto& nextEntry = m_playlist[i];
                decision = {
                    i,
                    determineTransitionType(currentEntry, nextEntry),
                    m_cacheManager.getDream(nextEntry.uuid),
                    nextEntry.startKeyframe,
                    nextEntry.endKeyframe
                };

                g_Log->Info("Preflight : returning first unplayed : %zu", i);
                return decision;
            }
        }
    }

    // If everything's been played, reset and start from beginning
    const auto& firstEntry = m_playlist[0];
    decision = {
        0,
        TransitionType::StandardCrossfade,  // Always crossfade when restarting playlist
        m_cacheManager.getDream(firstEntry.uuid),
        firstEntry.startKeyframe,
        firstEntry.endKeyframe
    };
    g_Log->Info("Preflight : fallback to first dream");

    return decision;
}

const Cache::Dream* PlaylistManager::moveToNextDream(const NextDreamDecision& decision) {
    if (m_playlist.empty()) {
        return nullptr;
    }

    // If this is the first play, mark as started
    if (!m_started) {
        m_started = true;
    } else {
        addToHistory(m_currentPosition);
    }

    // If we're starting over, reset history
    if (decision.position == 0 && !hasUnplayedDreams()) {
        resetPlayHistory();
    }

    // Update position
    m_currentPosition = decision.position;
    
    // Update current dream info
    m_currentDreamUUID = m_playlist[m_currentPosition].uuid;
    m_currentDream = decision.dream;
    
    g_Log->Info("moveToNextDream : %s (pos: %zu, transition: %d)",
                m_currentDreamUUID.c_str(), m_currentPosition,
                static_cast<int>(decision.transition));
    
    return m_currentDream;
}


std::optional<PlaylistManager::DreamLookupResult> PlaylistManager::getDreamByUUID(const std::string& dreamUUID) {
    auto it = std::find_if(m_playlist.begin(), m_playlist.end(),
                          [&dreamUUID](const PlaylistEntry& entry) {
                              return entry.uuid == dreamUUID;
                          });

    if (it == m_playlist.end()) {
        g_Log->Error("Dream isn't in the playlist %s", dreamUUID.c_str());
        return std::nullopt;
    }

    // Playlist is now playing
    m_started = true;
    
    // Dream is in the playlist, update the position
    m_currentPosition = std::distance(m_playlist.begin(), it);
    m_currentDreamUUID = dreamUUID;
    m_currentDream = m_cacheManager.getDream(dreamUUID);
    
    if (!m_currentDream) {
        g_Log->Error("Failed to get dream metadata for %s", dreamUUID.c_str());
        return std::nullopt;
    }
    
    // Return both the dream pointer and any keyframe information
    return DreamLookupResult{
        m_currentDream,
        it->startKeyframe,
        it->endKeyframe
    };
}

const Cache::Dream* PlaylistManager::getPreviousDream() {
    if (m_playlist.empty()) {
        return nullptr;
    }
    
    // Remove current dream from history if it's the last one
    if (!m_playHistory.empty() && m_playHistory.back() == m_currentPosition) {
        removeLastFromHistory();
    }
    
    // Get previous position from history
    if (!m_playHistory.empty()) {
        m_currentPosition = m_playHistory.back();
        g_Log->Info("Going back to previous dream in history: %s (pos: %zu)",
                    m_playlist[m_currentPosition].uuid.c_str(), m_currentPosition);
    } else {
        // If history is empty, go to end of playlist
        m_currentPosition = m_playlist.size() - 1;
        addToHistory(m_currentPosition);
        g_Log->Info("History empty, wrapping to end: %s (pos: %zu)",
                    m_playlist[m_currentPosition].uuid.c_str(), m_currentPosition);
    }

    if (m_cacheManager.isPlaylistFillingCache(getCurrentPlaylistUUIDs())) {
        g_Log->Info("Cache is already filled up with current playlist, thrashing protection");

        size_t startPosition = m_currentPosition;
        do {
            if (m_cacheManager.hasDiskCachedItem(m_playlist[m_currentPosition].uuid)) {
                break;
            }
            
            removeLastFromHistory();
            
            if (!m_playHistory.empty()) {
                m_currentPosition = m_playHistory.back();
            } else {
                m_currentPosition = m_playlist.size() - 1;
            }
        } while (m_currentPosition != startPosition);
        
        if (m_currentPosition == startPosition && !m_cacheManager.hasDiskCachedItem(m_playlist[m_currentPosition].uuid)) {
            g_Log->Warning("No cached dreams found in playlist after full scan");
        }
    }
    
    m_currentDreamUUID = m_playlist[m_currentPosition].uuid;
    m_currentDream = m_cacheManager.getDream(m_currentDreamUUID);
    
    g_Log->Info("getPreviousDream : %s (pos: %zu)", m_currentDreamUUID.c_str(), m_currentPosition);
    
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

    g_Log->Info("getCurrentDream : %s %s (pos: %zu)", m_playlist[m_currentPosition].uuid.c_str(), m_currentDreamUUID.c_str(), m_currentPosition);

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
        [&dreamUUID](const PlaylistEntry& entry) {
            return entry.uuid == dreamUUID;
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
    m_currentDreamUUID = m_playlist[m_currentPosition].uuid;

    g_Log->Info("Playlist updated. New position: %zu, Current dream UUID: %s (old position: %zu, old uuid : %s)",
                m_currentPosition, m_currentDreamUUID.c_str(), oldPosition, currentDreamUUID.c_str());

    return true;
}

std::chrono::seconds PlaylistManager::getTimeUntilNextCheck() const {
    auto now = std::chrono::steady_clock::now();
    auto next = m_nextCheckTime.load();
    auto timeUntilNext = std::chrono::duration_cast<std::chrono::seconds>(next - now);
    return std::max(timeUntilNext, std::chrono::seconds(0));
}

// MARK: History stack
void PlaylistManager::addToHistory(size_t position) {
    m_playHistory.push_back(position);
    g_Log->Info("Added to history: %s (position %zu)", m_playlist[position].uuid.c_str(), position);
}

void PlaylistManager::removeLastFromHistory() {
    if (!m_playHistory.empty()) {
        size_t lastPosition = m_playHistory.back();
        m_playHistory.pop_back();
        g_Log->Info("Removed from history: %s (position %zu)",
                    m_playlist[lastPosition].uuid.c_str(), lastPosition);
    }
}

void PlaylistManager::resetPlayHistory() {
    m_playHistory.clear();
    g_Log->Info("Play history reset");
}

bool PlaylistManager::isDreamPlayed(const std::string& uuid) const {
    return std::find_if(m_playHistory.begin(), m_playHistory.end(),
                       [this, &uuid](size_t pos) {
                            return pos < m_playlist.size() && m_playlist[pos].uuid == uuid;
                       }) != m_playHistory.end();
}

bool PlaylistManager::hasUnplayedDreams() const {
    return m_playHistory.size() < m_playlist.size();
}


size_t PlaylistManager::findFirstUnplayedPosition() const {
    for (size_t i = 0; i < m_playlist.size(); i++) {
        if (!isDreamPlayed(m_playlist[i].uuid)) {
            return i;
        }
    }
    return 0;  // Return 0 if all dreams played
}

