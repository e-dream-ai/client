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
: m_started(false), m_playbackMode(PlaybackMode::Normal), m_offlineMode(false), m_currentPosition(0), m_cacheManager(Cache::CacheManager::getInstance()), m_isCheckingActive(false), m_shouldTerminate(false) {
}

PlaylistManager::~PlaylistManager() {
    stopPeriodicChecking();
}

void PlaylistManager::setOfflineMode(bool offline) {
    std::lock_guard lock(m_stateMutex);
    m_offlineMode = offline;
}

std::string PlaylistManager::getCurrentDreamUUID() const {
    std::lock_guard lock(m_stateMutex);
    return m_currentDreamUUID;
}

size_t PlaylistManager::getCurrentPosition() const {
    std::lock_guard lock(m_stateMutex);
    return m_currentPosition;
}

void PlaylistManager::setPlaybackMode(PlaybackMode mode) {
    std::lock_guard lock(m_stateMutex);
    m_playbackMode = mode;
}

PlaybackMode PlaylistManager::getPlaybackMode() const {
    std::lock_guard lock(m_stateMutex);
    return m_playbackMode;
}

// MARK: Init
bool PlaylistManager::initializePlaylist(const std::string& playlistUUID, bool fetchPlaylist = true) {
    uint64_t generation = 0;
    bool offline = false;
    {
        std::lock_guard lock(m_stateMutex);
        m_initializeInProgress = true;
        m_currentPlaylistUUID = playlistUUID;
        generation = ++m_playlistGeneration;
        offline = m_offlineMode;
    }
    

    if (!offline && fetchPlaylist) {
        if (!EDreamClient::FetchPlaylist(playlistUUID)) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", playlistUUID.c_str());
            std::lock_guard lock(m_stateMutex);
            m_initializeInProgress = false;
            return false;
        }
    }
    
    if (!parsePlaylist(playlistUUID, generation)) {
        if (offline) {
            g_Log->Warning("Failed to parse playlist in offline mode. Falling back to offline playlist.");
            initializeOfflinePlaylist();
        } else {
            std::lock_guard lock(m_stateMutex);
            m_initializeInProgress = false;
            return false;
        }
    }

    {
        std::lock_guard lock(m_stateMutex);
        if (m_playlist.empty() && m_offlineMode) {
            g_Log->Warning("Parsed playlist is empty in offline mode. Falling back to offline playlist.");
            initializeOfflinePlaylist();
        }

        m_currentPosition = 0;
        m_started = false;
        m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0].uuid;
        resetPlayHistory();
        m_initializeInProgress = false;
    }

    // Start periodic checking if it's not already running. Don't in offline mode though!
    if (!m_isCheckingActive && !offline) {
        startPeriodicChecking();
    }

    return true;
}

void PlaylistManager::initializeOfflinePlaylist() {
    std::lock_guard lock(m_stateMutex);
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
    {
        std::lock_guard lock(m_stateMutex);
        m_currentDreamUUID = m_playlist.empty() ? "" : m_playlist[0].uuid;
    }

    // Clear play history when initializing offline playlist
    resetPlayHistory();

    // Set offline playlist metadata
    m_currentPlaylistName = "Offline Playlist";
    m_currentPlaylistArtist = "Local";
    m_isPlaylistNSFW = false;
    m_playlistTimestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    m_loopIterations = 0;
    m_currentLoopCount = 0;
}


std::vector<std::string> PlaylistManager::getCurrentPlaylistUUIDs() const {
    std::lock_guard lock(m_stateMutex);
    std::vector<std::string> uuids;
    uuids.reserve(m_playlist.size());
    for (const auto& entry : m_playlist) {
        uuids.push_back(entry.uuid);
    }
    return uuids;
}

bool PlaylistManager::parsePlaylist(const std::string& playlistUUID,
                                    uint64_t expectedGeneration,
                                    bool preserveCurrent,
                                    const std::string& currentDreamUUID,
                                    size_t oldPosition) {
    // Parse the playlist
    std::vector<PlaylistEntry> entries = EDreamClient::ParsePlaylist(playlistUUID);
    
    if (entries.empty()) {
        g_Log->Error("Failed to parse playlist or playlist is empty. UUID: %s", playlistUUID.c_str());
        return false;
    }

    // Get the playlist metadata
    auto [playlistName, playlistArtist, isNSFW, timestamp, loops] = EDreamClient::ParsePlaylistMetadata(playlistUUID);

    bool offline = false;
    {
        std::lock_guard lock(m_stateMutex);
        offline = m_offlineMode;
    }
    auto filtered = offline ? filterUncachedDreams(entries)
                            : filterActiveAndProcessedDreams(entries);

    {
        std::lock_guard lock(m_stateMutex);
        if (m_playlistGeneration != expectedGeneration ||
            m_currentPlaylistUUID != playlistUUID) {
            g_Log->Info("Discarding stale playlist parse for %s", playlistUUID.c_str());
            return false;
        }

        if (preserveCurrent && filtered.empty())
            return false;

        size_t preservedPosition = 0;
        if (preserveCurrent)
        {
            const auto it = std::find_if(filtered.begin(), filtered.end(),
                [&currentDreamUUID](const PlaylistEntry& entry) {
                    return entry.uuid == currentDreamUUID;
                });
            preservedPosition = it != filtered.end()
                ? static_cast<size_t>(std::distance(filtered.begin(), it))
                : std::min(oldPosition, filtered.size() - 1);
        }

        m_playlist = std::move(filtered);
        m_currentPlaylistName = playlistName;
        m_currentPlaylistArtist = playlistArtist;
        m_isPlaylistNSFW = isNSFW;
        m_playlistTimestamp = timestamp;

        if (loops != m_loopIterations)
            m_currentLoopCount = 0;
        m_loopIterations = loops;

        if (preserveCurrent)
        {
            m_currentPosition = preservedPosition;
            m_currentDreamUUID = m_playlist[m_currentPosition].uuid;
        }

        g_Log->Info("Playlist loops: %d", m_loopIterations);
        g_Log->Info("Updated playlist: %s by %s (UUID: %s, NSFW: %s, Timestamp: %lld) with %zu dreams",
                    m_currentPlaylistName.c_str(), m_currentPlaylistArtist.c_str(),
                    m_currentPlaylistUUID.c_str(), m_isPlaylistNSFW ? "Yes" : "No",
                    m_playlistTimestamp, m_playlist.size());
    }

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
    std::lock_guard lock(m_stateMutex);
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
    std::lock_guard lock(m_stateMutex);
    // Check if any dream in the playlist has keyframes (start or end)
    for (const auto& entry : m_playlist) {
        if (entry.startKeyframe.has_value() || entry.endKeyframe.has_value()) {
            return true;
        }
    }
    return false;
}


std::optional<std::string> PlaylistManager::getNextUncachedDream() const {
    std::lock_guard lock(m_stateMutex);
    Cache::CacheManager& cm = Cache::CacheManager::getInstance();
    auto& downloader = g_ContentDownloader().m_gDownloader;

    // In shuffle mode, playback order is random, so download order should be
    // random too - don't follow keyframe chains or sequential successors (issue #521).
    if (m_playbackMode == PlaybackMode::Shuffle) {
        std::vector<std::string> uncached;
        for (const auto& entry : m_playlist) {
            if (!cm.hasDiskCachedItem(entry.uuid) &&
                !downloader.IsDreamBeingDownloaded(entry.uuid)) {
                uncached.push_back(entry.uuid);
            }
        }
        if (uncached.empty())
            return std::nullopt;
        g_Log->Info("Shuffle mode: random uncached dream (%zu remaining)", uncached.size());
        return uncached[rand() % uncached.size()];
    }

    // Check if playlist has keyframes and randomly return an uncached dream 10% of the time
    if (hasKeyframes()) {
        // Generate random number between 0 and 99
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 99);
        
        if (dis(gen) < 2) {  // 2% chance
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
        
        // 50% chance: If LoopIterations is enabled, prioritize downloading uncached looping dreams
        // that share a keyframe with a cached dream (so we have loop variety available)
        if (m_loopIterations > 0 && dis(gen) < 50) {
            std::vector<std::string> uncachedLoops;

            for (const auto& entry : m_playlist) {
                if (!isLoopingDream(entry)) continue;
                if (cm.hasDiskCachedItem(entry.uuid)) continue;
                if (downloader.IsDreamBeingDownloaded(entry.uuid)) continue;

                // Check if any cached dream shares this loop's keyframe group
                for (const auto& other : m_playlist) {
                    if (other.startKeyframe.has_value() &&
                        *other.startKeyframe == *entry.startKeyframe &&
                        cm.hasDiskCachedItem(other.uuid)) {
                        uncachedLoops.push_back(entry.uuid);
                        break;
                    }
                }
            }

            if (!uncachedLoops.empty()) {
                std::uniform_int_distribution<> dreamDis(0, uncachedLoops.size() - 1);
                g_Log->Info("Selecting uncached looping dream for LoopIterations (%zu candidates)",
                           uncachedLoops.size());
                return uncachedLoops[dreamDis(gen)];
            }
        }

        // 75% chance: Look for cached dreams that have no cached successors
        if (dis(gen) < 75) {  // New independent 75% chance
            std::vector<std::string> orphanedSuccessors;
            
            for (const auto& entry : m_playlist) {
                // Only consider cached dreams with end keyframes
                if (cm.hasDiskCachedItem(entry.uuid) && entry.endKeyframe.has_value()) {
                    // Check if any successors with matching start keyframe are cached
                    bool hasAnyCachedSuccessor = false;
                    std::vector<std::string> uncachedSuccessors;
                    
                    for (const auto& succ : m_playlist) {
                        if (succ.uuid == entry.uuid) continue;  // Don't count a loop as its own successor
                        if (succ.startKeyframe.has_value() &&
                            *succ.startKeyframe == *entry.endKeyframe) {
                            // Found a dream with matching start keyframe
                            if (cm.hasDiskCachedItem(succ.uuid)) {
                                hasAnyCachedSuccessor = true;
                                break;
                            } else if (!downloader.IsDreamBeingDownloaded(succ.uuid)) {
                                uncachedSuccessors.push_back(succ.uuid);
                            }
                        }
                    }
                    
                    // If this cached dream has no cached successors, add its uncached successors
                    if (!hasAnyCachedSuccessor && !uncachedSuccessors.empty()) {
                        orphanedSuccessors.insert(orphanedSuccessors.end(), 
                                                uncachedSuccessors.begin(), 
                                                uncachedSuccessors.end());
                    }
                }
            }
            
            // Remove duplicates and return a random one
            if (!orphanedSuccessors.empty()) {
                std::sort(orphanedSuccessors.begin(), orphanedSuccessors.end());
                orphanedSuccessors.erase(std::unique(orphanedSuccessors.begin(), orphanedSuccessors.end()), 
                                       orphanedSuccessors.end());
                
                std::uniform_int_distribution<> dreamDis(0, orphanedSuccessors.size() - 1);
                g_Log->Info("Selecting successor of cached dream with no cached successors (%zu candidates)", 
                           orphanedSuccessors.size());
                return orphanedSuccessors[dreamDis(gen)];
            }
        }
        
        // 50% chance: Look for cached dreams that have no cached predecessors
        if (dis(gen) < 50) {  // New independent 50% chance
            std::vector<std::string> orphanedPredecessors;
            
            for (const auto& entry : m_playlist) {
                // Only consider uncached dreams with start keyframes
                if (!cm.hasDiskCachedItem(entry.uuid) && 
                    !downloader.IsDreamBeingDownloaded(entry.uuid) &&
                    entry.startKeyframe.has_value()) {
                    // Check if any predecessors with matching end keyframe are cached
                    bool hasAnyCachedPredecessor = false;
                    
                    for (const auto& pred : m_playlist) {
                        if (pred.endKeyframe.has_value() && 
                            *pred.endKeyframe == *entry.startKeyframe &&
                            cm.hasDiskCachedItem(pred.uuid)) {
                            hasAnyCachedPredecessor = true;
                            break;
                        }
                    }
                    
                    // If this uncached dream has no cached predecessors, add it
                    if (!hasAnyCachedPredecessor) {
                        orphanedPredecessors.push_back(entry.uuid);
                    }
                }
            }
            
            // Return a random orphaned dream
            if (!orphanedPredecessors.empty()) {
                std::uniform_int_distribution<> dreamDis(0, orphanedPredecessors.size() - 1);
                g_Log->Info("Selecting uncached dream with no cached predecessors (%zu candidates)", 
                           orphanedPredecessors.size());
                return orphanedPredecessors[dreamDis(gen)];
            }
        }
    }
    
    // For all other cases (remaining percentage or no keyframes)
    // Build a list of successor dreams based on cached dreams
    std::vector<std::string> successors;
    
    for (const auto& entry : m_playlist) {
        // Only consider cached dreams as starting points, 
        // but also consider current position even if it's streaming
        bool isCurrentPosition = (m_started && entry.uuid == m_currentDreamUUID);
        if (!cm.hasDiskCachedItem(entry.uuid) && !isCurrentPosition) {
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
        auto dream = cm.getDream(uuid);
        if (dream) {
            return dream->status == "processed";
        }
    }
    return false;
}

void PlaylistManager::removeCurrentDream() {
    std::lock_guard lock(m_stateMutex);
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

bool PlaylistManager::isLoopingDream(const PlaylistEntry& entry) const {
    return entry.startKeyframe.has_value() && entry.endKeyframe.has_value() &&
           *entry.startKeyframe == *entry.endKeyframe;
}

std::optional<size_t> PlaylistManager::findKeyframeMatch(const PlaylistEntry& currentEntry, bool canStream) const {
    std::lock_guard lock(m_stateMutex);
    if (!currentEntry.endKeyframe) {
        return std::nullopt;
    }

    std::vector<size_t> candidates;
    for (size_t i = 0; i < m_playlist.size(); i++) {
        const auto& entry = m_playlist[i];
        if (i != m_currentPosition && entry.startKeyframe &&
            *entry.startKeyframe == *currentEntry.endKeyframe &&
            !isDreamPlayed(entry.uuid)) {
            if (!canStream && !m_cacheManager.hasDiskCachedItem(entry.uuid)) {
                continue;
            }
            candidates.push_back(i);
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    // Randomly select one of the candidates
    return candidates[rand() % candidates.size()];
}

std::optional<PlaylistManager::NextDreamDecision> PlaylistManager::preflightNextDream(bool canStream, bool forceNext) const {
    std::lock_guard lock(m_stateMutex);
    g_Log->Info("Preflight : start (mode: %d, m_started: %d, m_currentPosition: %zu, forceNext: %d)",
                static_cast<int>(m_playbackMode), m_started, m_currentPosition, forceNext);

    if (m_playlist.empty()) {
        g_Log->Info("Preflight : no playlist");
        return std::nullopt;
    }

    NextDreamDecision decision;

    // Handle playback modes
    if (m_playbackMode == PlaybackMode::Normal && m_started) {
        // Normal mode: respect keyframes for seamless transitions
        const auto& currentEntry = m_playlist[m_currentPosition];

        // Loop iteration logic: if current dream is a loop and we haven't exhausted iterations,
        // pick a random matching loop (including self) and replay with seamless transition
        if (!forceNext && m_loopIterations > 0 && isLoopingDream(currentEntry) && m_currentLoopCount < m_loopIterations) {
            std::vector<size_t> loopCandidates;
            for (size_t i = 0; i < m_playlist.size(); i++) {
                const auto& entry = m_playlist[i];
                if (isLoopingDream(entry) &&
                    entry.startKeyframe == currentEntry.startKeyframe &&
                    (canStream || m_cacheManager.hasDiskCachedItem(entry.uuid))) {
                    loopCandidates.push_back(i);
                }
            }
            if (!loopCandidates.empty()) {
                size_t pick = loopCandidates[rand() % loopCandidates.size()];
                const auto& loopEntry = m_playlist[pick];
                g_Log->Info("Preflight : Loop iteration %d/%d - picking loop at position %zu",
                            m_currentLoopCount + 1, m_loopIterations, pick);
                decision = {
                    pick,
                    TransitionType::Seamless,
                    m_cacheManager.getDream(loopEntry.uuid),
                    loopEntry.startKeyframe,
                    loopEntry.endKeyframe
                };
                return decision;
            }
        }

        // When arriving at a keyframe via an edge, prefer entering loops at the destination
        if (!isLoopingDream(currentEntry) && m_loopIterations > 0 && currentEntry.endKeyframe.has_value()) {
            std::vector<size_t> loopCandidates;
            for (size_t i = 0; i < m_playlist.size(); i++) {
                const auto& entry = m_playlist[i];
                if (isLoopingDream(entry) &&
                    entry.startKeyframe.has_value() &&
                    *entry.startKeyframe == *currentEntry.endKeyframe &&
                    (canStream || m_cacheManager.hasDiskCachedItem(entry.uuid))) {
                    loopCandidates.push_back(i);
                }
            }
            if (!loopCandidates.empty()) {
                size_t pick = loopCandidates[rand() % loopCandidates.size()];
                const auto& loopEntry = m_playlist[pick];
                g_Log->Info("Preflight : Entering loop at destination keyframe %s - position %zu",
                            loopEntry.startKeyframe->c_str(), pick);
                decision = {
                    pick,
                    TransitionType::Seamless,
                    m_cacheManager.getDream(loopEntry.uuid),
                    loopEntry.startKeyframe,
                    loopEntry.endKeyframe
                };
                return decision;
            }
        }

        // Try to find keyframe match first
        auto keyframeMatch = findKeyframeMatch(currentEntry, canStream);
        if (keyframeMatch) {
            const auto& nextEntry = m_playlist[*keyframeMatch];
            g_Log->Info("Preflight : Normal mode - keyframe match at position %zu", *keyframeMatch);

            decision = {
                *keyframeMatch,
                determineTransitionType(currentEntry, nextEntry),
                m_cacheManager.getDream(nextEntry.uuid),
                nextEntry.startKeyframe,
                nextEntry.endKeyframe
            };
            return decision;
        }

        // No keyframe match - use sequential playback
        size_t nextPos = (m_currentPosition + 1) % m_playlist.size();
        const auto& nextEntry = m_playlist[nextPos];

        // If canStream is false, verify the next dream is cached
        if (!canStream && !m_cacheManager.hasDiskCachedItem(nextEntry.uuid)) {
            g_Log->Info("Preflight : Normal mode - next sequential dream not cached and canStream=false, falling through");
            // Fall through to the cache-only logic below
        } else if (isDreamPlayed(nextEntry.uuid)) {
            g_Log->Info("Preflight : Normal mode - next sequential dream already played, falling through");
            // Fall through to the unplayed-scan logic below
        } else {
            g_Log->Info("Preflight : Normal mode - sequential to position %zu", nextPos);

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
    else if (m_playbackMode == PlaybackMode::Repeat) {
        // Repeat mode: play same dream again, unless forceNext is true
        if (forceNext && m_started) {
            // User explicitly pressed "next" - advance to next dream but stay in repeat mode
            size_t nextPos = (m_currentPosition + 1) % m_playlist.size();
            const auto& currentEntry = m_playlist[m_currentPosition];
            const auto& nextEntry = m_playlist[nextPos];

            // If canStream is false, verify the next dream is cached
            if (!canStream && !m_cacheManager.hasDiskCachedItem(nextEntry.uuid)) {
                g_Log->Info("Preflight : Repeat mode with forceNext - next dream not cached and canStream=false");
                // Fall through to cache-only logic below
            } else {
                g_Log->Info("Preflight : Repeat mode with forceNext - advancing to position %zu", nextPos);

                decision = {
                    nextPos,
                    determineTransitionType(currentEntry, nextEntry),
                    m_cacheManager.getDream(nextEntry.uuid),
                    nextEntry.startKeyframe,
                    nextEntry.endKeyframe
                };
                return decision;
            }
        } else {
            // Normal repeat behavior - replay same dream
            const auto& currentEntry = m_playlist[m_currentPosition];

            g_Log->Info("Preflight : Repeat mode - replaying position %zu", m_currentPosition);

            decision = {
                m_currentPosition,
                determineTransitionType(currentEntry, currentEntry),
                m_cacheManager.getDream(currentEntry.uuid),
                currentEntry.startKeyframe,
                currentEntry.endKeyframe
            };
            return decision;
        }
    }
    else if (m_playbackMode == PlaybackMode::Shuffle && m_started) {
        // Shuffle mode: random selection from unplayed dreams
        std::vector<size_t> unplayedPositions;
        for (size_t i = 0; i < m_playlist.size(); i++) {
            if (!isDreamPlayed(m_playlist[i].uuid)) {
                // If canStream is false, only consider cached dreams
                if (!canStream && !m_cacheManager.hasDiskCachedItem(m_playlist[i].uuid)) {
                    continue;
                }
                unplayedPositions.push_back(i);
            }
        }

        // If all dreams have been played, reset history and start over
        if (unplayedPositions.empty()) {
            g_Log->Info("Preflight : Shuffle mode - all dreams played, resetting history");
            const_cast<PlaylistManager*>(this)->resetPlayHistory();
            for (size_t i = 0; i < m_playlist.size(); i++) {
                // If canStream is false, only consider cached dreams
                if (!canStream && !m_cacheManager.hasDiskCachedItem(m_playlist[i].uuid)) {
                    continue;
                }
                unplayedPositions.push_back(i);
            }
        }

        // If still no candidates after reset, fall through
        if (!unplayedPositions.empty()) {
            // Pick random from unplayed dreams
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> dist(0, unplayedPositions.size() - 1);
            size_t nextPos = unplayedPositions[dist(gen)];

            const auto& currentEntry = m_playlist[m_currentPosition];
            const auto& nextEntry = m_playlist[nextPos];

            g_Log->Info("Preflight : Shuffle mode - random position %zu (from %zu unplayed)", nextPos, unplayedPositions.size());

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

    // If we haven't started yet, it would be the first dream
    if (!m_started) {
        const auto& firstEntry = m_playlist[0];
        
        // If canStream is false, check if the first dream is cached
        /*if (!canStream && !m_cacheManager.hasDiskCachedItem(firstEntry.uuid)) {
            g_Log->Info("Preflight : first dream not cached and canStream=false");
            return std::nullopt;
        }*/
        
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
    auto keyframeMatch = findKeyframeMatch(currentEntry, canStream);
    if (keyframeMatch) {
        const auto& nextEntry = m_playlist[*keyframeMatch];
        g_Log->Info("Preflight : fallback - keyframe match at position %zu", *keyframeMatch);

        decision = {
            *keyframeMatch,
            determineTransitionType(currentEntry, nextEntry),
            m_cacheManager.getDream(nextEntry.uuid),
            nextEntry.startKeyframe,
            nextEntry.endKeyframe
        };
        return decision;
    }

    g_Log->Info("Preflight : either no endkeyframe or no match found");

    // No keyframe match - find next unplayed dream
    if (hasUnplayedDreams()) {
        g_Log->Info("Preflight : has unplayed dreams");

        // Track if we found any unplayed dreams that were skipped due to not being cached
        bool foundUnplayedButUncached = false;

        // Start scanning from a random position to avoid always picking low-numbered dreams
        size_t startPos = rand() % m_playlist.size();
        for (size_t j = 0; j < m_playlist.size(); j++) {
            size_t i = (startPos + j) % m_playlist.size();
            if (i != m_currentPosition && !isDreamPlayed(m_playlist[i].uuid)) {
                // If canStream is false, only consider cached dreams
                if (!canStream && !m_cacheManager.hasDiskCachedItem(m_playlist[i].uuid)) {
                    foundUnplayedButUncached = true;
                    continue;
                }

                const auto& nextEntry = m_playlist[i];
                decision = {
                    i,
                    determineTransitionType(currentEntry, nextEntry),
                    m_cacheManager.getDream(nextEntry.uuid),
                    nextEntry.startKeyframe,
                    nextEntry.endKeyframe
                };

                g_Log->Info("Preflight : returning unplayed at %zu (scan started at %zu)", i, startPos);
                return decision;
            }
        }
        
        // If we found unplayed dreams but none were cached, we need to reset
        if (foundUnplayedButUncached && !canStream) {
            g_Log->Info("Preflight : has unplayed dreams but none are cached, resetting play history");
            // Cast away const to reset - this is a special case where we need to modify state
            const_cast<PlaylistManager*>(this)->resetPlayHistory();
        }
    }

    // If everything's been played, reset and start from beginning
    // But if canStream is false, we need to find a cached dream
    if (!canStream) {
        g_Log->Info("Preflight : looking for any cached dream (canStream=false)");
        for (size_t i = 0; i < m_playlist.size(); i++) {
            if (m_cacheManager.hasDiskCachedItem(m_playlist[i].uuid)) {
                const auto& cachedEntry = m_playlist[i];
                decision = {
                    i,
                    TransitionType::StandardCrossfade,
                    m_cacheManager.getDream(cachedEntry.uuid),
                    cachedEntry.startKeyframe,
                    cachedEntry.endKeyframe
                };
                g_Log->Info("Preflight : found cached dream at position %zu", i);
                return decision;
            }
        }

        // No cached dreams available at all
        g_Log->Warning("Preflight : no cached dreams available and canStream=false");
        return std::nullopt;
    }

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

std::shared_ptr<const Cache::Dream> PlaylistManager::moveToNextDream(const NextDreamDecision& decision) {
    std::lock_guard lock(m_stateMutex);
    if (m_playlist.empty()) {
        return nullptr;
    }

    // Validate that the position is still valid
    if (decision.position >= m_playlist.size()) {
        g_Log->Error("moveToNextDream: position %zu is out of bounds (playlist size: %zu)",
                     decision.position, m_playlist.size());
        return nullptr;
    }

    size_t previousPosition = m_currentPosition;

    // Validate that the dream UUID at this position matches what we expect
    if (decision.dream && m_playlist[decision.position].uuid != decision.dream->uuid) {
        g_Log->Warning("moveToNextDream: dream UUID mismatch at position %zu (expected: %s, found: %s)",
                       decision.position, decision.dream->uuid.c_str(),
                       m_playlist[decision.position].uuid.c_str());
        // Try to find the dream in the current playlist
        auto it = std::find_if(m_playlist.begin(), m_playlist.end(),
                              [&decision](const PlaylistEntry& entry) {
                                  return entry.uuid == decision.dream->uuid;
                              });
        if (it != m_playlist.end()) {
            m_currentPosition = std::distance(m_playlist.begin(), it);
            g_Log->Info("moveToNextDream: found dream at new position %zu", m_currentPosition);
        } else {
            g_Log->Error("moveToNextDream: dream %s no longer in playlist", decision.dream->uuid.c_str());
            return nullptr;
        }
    } else {
        m_currentPosition = decision.position;
    }

    // Track loop iterations
    if (m_loopIterations > 0) {
        bool currentIsLoop = isLoopingDream(m_playlist[m_currentPosition]);
        bool continuingLoop = m_started &&
            previousPosition < m_playlist.size() &&
            isLoopingDream(m_playlist[previousPosition]) &&
            currentIsLoop &&
            m_playlist[m_currentPosition].startKeyframe == m_playlist[previousPosition].startKeyframe;

        if (continuingLoop) {
            m_currentLoopCount++;
            g_Log->Info("Loop iteration count: %d/%d", m_currentLoopCount, m_loopIterations);
        } else if (currentIsLoop) {
            // First arrival at a looping dream (from a non-loop or different keyframe group)
            m_currentLoopCount = 1;
            g_Log->Info("Loop iteration count: %d/%d (entering loop)", m_currentLoopCount, m_loopIterations);
        } else {
            if (m_currentLoopCount > 0) {
                g_Log->Info("Loop iterations reset (was %d)", m_currentLoopCount);
            }
            m_currentLoopCount = 0;
        }
    }

    // If this is the first play, mark as started
    if (!m_started) {
        m_started = true;
    } else {
        addToHistory(m_currentPosition);
    }

    // If we're starting over, reset history
    if (m_currentPosition == 0 && !hasUnplayedDreams()) {
        resetPlayHistory();
    }

    // Update current dream info
    {
        std::lock_guard lock(m_stateMutex);
        m_currentDreamUUID = m_playlist[m_currentPosition].uuid;
    }
    m_currentDream = decision.dream;
    
    g_Log->Info("moveToNextDream : %s (pos: %zu, transition: %d)",
                m_currentDreamUUID.c_str(), m_currentPosition,
                static_cast<int>(decision.transition));
    
    return m_currentDream;
}


std::optional<PlaylistManager::DreamLookupResult> PlaylistManager::getDreamByUUID(const std::string& dreamUUID) {
    std::lock_guard lock(m_stateMutex);
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
    {
        std::lock_guard lock(m_stateMutex);
        m_currentDreamUUID = dreamUUID;
    }
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

std::shared_ptr<const Cache::Dream> PlaylistManager::getPreviousDream() {
    std::lock_guard lock(m_stateMutex);
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
    
    {
        std::lock_guard lock(m_stateMutex);
        m_currentDreamUUID = m_playlist[m_currentPosition].uuid;
    }
    m_currentDream = m_cacheManager.getDream(m_currentDreamUUID);
    
    g_Log->Info("getPreviousDream : %s (pos: %zu)", m_currentDreamUUID.c_str(), m_currentPosition);
    
    return m_currentDream;
}

bool PlaylistManager::hasMoreDreams() const {
    std::lock_guard lock(m_stateMutex);
    return !m_playlist.empty();
}

std::shared_ptr<const Cache::Dream> PlaylistManager::getCurrentDream() const {
    std::lock_guard lock(m_stateMutex);
    if (m_playlist.empty()) {
        return nullptr;
    }

    return m_currentDream;
}

void PlaylistManager::setCurrentPosition(size_t position) {
    std::lock_guard lock(m_stateMutex);
    if (position < m_playlist.size()) {
        m_currentPosition = position;
    }
}

std::string PlaylistManager::getPlaylistName() const {
    std::lock_guard lock(m_stateMutex);
    return m_currentPlaylistName.empty() ? "No playlist loaded" : m_currentPlaylistName;
}

std::string PlaylistManager::getPlaylistUUID() const {
    std::lock_guard lock(m_stateMutex);
    return m_currentPlaylistUUID;
}

size_t PlaylistManager::getPlaylistSize() const {
    std::lock_guard lock(m_stateMutex);
    return m_playlist.size();
}

void PlaylistManager::clearPlaylist() {
    std::lock_guard lock(m_stateMutex);
    m_playlist.clear();
    m_currentPosition = 0;
}

// maybe someday?
void PlaylistManager::shufflePlaylist() {
    std::lock_guard lock(m_stateMutex);
    auto rng = std::default_random_engine {};
    std::shuffle(std::begin(m_playlist), std::end(m_playlist), rng);
    m_currentPosition = 0;
}

std::tuple<std::string, std::string, bool, int64_t, int> PlaylistManager::getPlaylistInfo() const {
    std::lock_guard lock(m_stateMutex);
    return {m_currentPlaylistName, m_currentPlaylistArtist, m_isPlaylistNSFW, m_playlistTimestamp, m_loopIterations};
}

std::shared_ptr<const Cache::Dream> PlaylistManager::getDreamMetadata(const std::string& dreamUUID) const {
    auto it = std::find_if(m_playlist.begin(), m_playlist.end(),
        [&dreamUUID](const PlaylistEntry& entry) {
            return entry.uuid == dreamUUID;
        });

    if (it != m_playlist.end()) {
        auto dream = m_cacheManager.getDream(dreamUUID);
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
    
    while (!m_shouldTerminate.load()) {
        updateNextCheckTime();
        {
            std::unique_lock<std::mutex> lock(m_cvMutex);
            if (m_cv.wait_for(lock, m_checkInterval,
                              [this] { return m_shouldTerminate.load(); })) {
                // If m_shouldTerminate is true, exit the thread
                break;
            }
        }
        
        if (!m_isCheckingActive.load() || m_shouldTerminate.load()) {
            break;
        }
        
        checkForPlaylistChanges();
        
        // Check again after network call in case termination was requested during it
        if (m_shouldTerminate.load()) {
            g_Log->Info("PlaylistManager::periodicCheckThread() - termination requested after check");
            break;
        }
    }
    g_Log->Info("PlaylistManager::periodicCheckThread() - exiting");
}

bool PlaylistManager::checkForPlaylistChanges() {
    std::string playlistUUID;
    int64_t oldTimestamp = 0;
    {
        std::lock_guard lock(m_stateMutex);
        playlistUUID = m_currentPlaylistUUID;
        oldTimestamp = m_playlistTimestamp;
    }

    // First, fetch the playlist to ensure we have the latest version
    if (!EDreamClient::FetchPlaylist(playlistUUID)) {
        g_Log->Error("Failed to fetch playlist for checking changes. UUID: %s", playlistUUID.c_str());
        return false;
    }

    // Then parse the metadata to get the new timestamp
    auto [newName, newArtist, newNSFW, newTimestamp, newLoops] = EDreamClient::ParsePlaylistMetadata(playlistUUID);
    
    g_Log->Info("Old timestamp: %lld, New timestamp: %lld",
                oldTimestamp, newTimestamp);
    
    if (newTimestamp > oldTimestamp) {
        g_Log->Info("Playlist change detected. Old timestamp: %lld, New timestamp: %lld",
                    oldTimestamp, newTimestamp);

        {
            std::lock_guard lock(m_stateMutex);
            if (m_currentPlaylistUUID != playlistUUID) {
                g_Log->Info("Ignoring periodic update for stale playlist %s",
                            playlistUUID.c_str());
                return false;
            }
        }
        
        // Update the playlist
        if (updatePlaylist(true)) {
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
    std::string currentDreamUUID;
    std::string playlistUUID;
    size_t oldPosition = 0;
    uint64_t generation = 0;
    {
        std::lock_guard lock(m_stateMutex);
        currentDreamUUID = m_currentDreamUUID;
        playlistUUID = m_currentPlaylistUUID;
        oldPosition = m_currentPosition;
        generation = m_playlistGeneration;
    }

    if (!alreadyFetched) {
        if (!EDreamClient::FetchPlaylist(playlistUUID)) {
            g_Log->Error("Failed to fetch playlist. UUID: %s", playlistUUID.c_str());
            return false;
        }
    }

    if (!parsePlaylist(playlistUUID, generation, true,
                       currentDreamUUID, oldPosition)) {
        return false;
    }

    g_Log->Info("Playlist updated while preserving dream %s at prior position %zu",
                currentDreamUUID.c_str(), oldPosition);

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
