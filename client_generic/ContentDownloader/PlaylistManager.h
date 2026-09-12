//
//  PlaylistManager.hpp
//  All
//
//  Created by Guillaume Louel on 13/08/2024.
//

#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include "CacheManager.h"
#include "Dream.h"


// Playback modes
enum class PlaybackMode {
    Normal,   // Sequential playback
    Repeat,   // Repeat current dream
    Shuffle   // Random selection
};

// Convert PlaybackMode to string
inline constexpr const char* to_string(PlaybackMode mode) {
    constexpr const char* names[] = {"sequential", "repeat", "shuffle"};
    return names[static_cast<int>(mode)];
}

// This represent one item in our playlist. Includes dream uuid and keyframes
struct PlaylistEntry {
    std::string uuid;
    std::optional<std::string> startKeyframe;  // Optional start keyframe UUID
    std::optional<std::string> endKeyframe;    // Optional end keyframe UUID

    // Constructor for easy creation from just a UUID
    explicit PlaylistEntry(std::string uuid_) : uuid(std::move(uuid_)) {}

    // Constructor for full initialization
    PlaylistEntry(std::string uuid_,
                 std::optional<std::string> start = std::nullopt,
                 std::optional<std::string> end = std::nullopt)
        : uuid(std::move(uuid_))
        , startKeyframe(std::move(start))
        , endKeyframe(std::move(end)) {}
};

class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager();


    
    // Initialize the playlist with it's uuid and a list of dream UUIDs
    bool initializePlaylist(const std::string& playlistUUID, bool fetchPlaylist);

    void setOfflineMode(bool offline);
    bool isOfflineMode() const {
        std::lock_guard lock(m_stateMutex);
        return m_offlineMode;
    }
    
    std::vector<std::string> getCurrentPlaylistUUIDs() const;
    
    std::vector<PlaylistEntry> filterActiveAndProcessedDreams(const std::vector<PlaylistEntry>& entries) const;
    std::vector<PlaylistEntry> filterUncachedDreams(const std::vector<PlaylistEntry>& entries) const;
    
    // Get the next uncached dream in the playlist (thread-safe)
    std::optional<std::string> getNextUncachedDream() const;
    
    // Count how many dreams are cached ahead of the current position
    size_t countCachedDreamsAhead() const;
    
    // Get the lookahead limit for downloading
    size_t getDownloadLookaheadLimit() const {
        std::lock_guard lock(m_stateMutex);
        return m_downloadLookaheadLimit;
    }
    void setDownloadLookaheadLimit(size_t limit) {
        std::lock_guard lock(m_stateMutex);
        m_downloadLookaheadLimit = limit;
    }
    
    // Check if any dream in the playlist has keyframes
    bool hasKeyframes() const;
   
    
    // Get a dream by its UUID, set position if found in playlist, return nullopt if not in playlist
    struct DreamLookupResult {
        std::shared_ptr<const Cache::Dream> dream;
        std::optional<std::string> startKeyframe;
        std::optional<std::string> endKeyframe;
    };

    // Different kinds of transitions we use
    enum class TransitionType {
        None,
        StandardCrossfade,  // 5 second fade
        QuickCrossfade,     // 1 second fade
        Seamless            // No fade, keyframe based
    };
    
    // Structure to hold next dream decision
    struct NextDreamDecision {
        size_t position;            // Position we'll move to
        TransitionType transition;  // How we should transition
        std::shared_ptr<const Cache::Dream> dream;  // The dream metadata (owning handle)
        std::optional<std::string> startKeyframe;
        std::optional<std::string> endKeyframe;
    };
    

    
    std::optional<DreamLookupResult> getDreamByUUID(const std::string& dreamUUID);

    // Calculate what will be played next without changing state
    // canStream: if true, allows selecting dreams that aren't cached (will stream)
    //            if false, only considers cached dreams
    // forceNext: if true, advances to next dream even in repeat mode (for explicit user "next" command)
    std::optional<NextDreamDecision> preflightNextDream(bool canStream = true, bool forceNext = false) const;

    // Helper to find keyframe matches
    std::optional<size_t> findKeyframeMatch(const PlaylistEntry& currentEntry, bool canStream) const;

    // Check if a dream is a loop (same start and end keyframe)
    bool isLoopingDream(const PlaylistEntry& entry) const;

    // Loop iteration control
    int getLoopIterations() const {
        std::lock_guard lock(m_stateMutex);
        return m_loopIterations;
    }
    void setLoopIterations(int iterations) {
        std::lock_guard lock(m_stateMutex);
        m_loopIterations = iterations;
    }
    int getCurrentLoopCount() const {
        std::lock_guard lock(m_stateMutex);
        return m_currentLoopCount;
    }
    std::string getCurrentDreamUUID() const;

    // Actually move to the next dream based on preflight decision
    std::shared_ptr<const Cache::Dream> moveToNextDream(const NextDreamDecision& decision);

    
/*    // Get the next dream in the playlist
    const Cache::Dream* getNextDream();
    // peek at what would come next with our logic, handles keyframe transitions
    std::optional<DreamLookupResult> peekNextDream() const;
*/
    // Get the previous dream in the playlist
    std::shared_ptr<const Cache::Dream> getPreviousDream();
    
private:
    // Keep track of preflight decision
    std::optional<NextDreamDecision> m_lastPreflight;

    // Helper to determine transition type
    TransitionType determineTransitionType(const PlaylistEntry& current, const PlaylistEntry& next) const;

public:
    // Check if there are more dreams in the playlist
    bool hasMoreDreams() const;

    // Get the current dream without advancing the playlist
    std::shared_ptr<const Cache::Dream> getCurrentDream() const;

    // Set the current position in the playlist
    void setCurrentPosition(size_t position);
    size_t getCurrentPosition() const;

    // Playback mode control
    void setPlaybackMode(PlaybackMode mode);
    PlaybackMode getPlaybackMode() const;
    
    // Get the total number of dreams in the playlist
    size_t getPlaylistSize() const;

    // Get various metadata of the current playlist
    std::string getPlaylistName() const;
    std::string getPlaylistUUID() const;
    bool isPlaylistNSFW() const {
        std::lock_guard lock(m_stateMutex);
        return m_isPlaylistNSFW;
    }
    int64_t getPlaylistTimestamp() const {
        std::lock_guard lock(m_stateMutex);
        return m_playlistTimestamp;
    }

    // Clear the current playlist
    void clearPlaylist();

    // Shuffle the playlist
    void shufflePlaylist();

    // Get playlist information (e.g., name, artist)
    std::tuple<std::string, std::string, bool, int64_t, int> getPlaylistInfo() const;

    void startPeriodicChecking();
    void stopPeriodicChecking();
    
    std::chrono::seconds getTimeUntilNextCheck() const;
    void removeCurrentDream();
    bool isReady() const {
        std::lock_guard lock(m_stateMutex);
        return !m_initializeInProgress;
    };

private:
    std::vector<PlaylistEntry> m_playlist;
    bool m_started;
    PlaybackMode m_playbackMode;

    bool m_initializeInProgress = false;
    bool m_offlineMode;
    
    std::string m_currentPlaylistUUID;
    std::string m_currentPlaylistName;
    std::string m_currentPlaylistArtist;
    bool m_isPlaylistNSFW;
    int64_t m_playlistTimestamp;
    
    size_t m_currentPosition;
    std::shared_ptr<const Cache::Dream> m_currentDream;
    std::string m_currentDreamUUID;  // Store the UUID of the currently playing dream
    mutable std::recursive_mutex m_stateMutex;

    Cache::CacheManager& m_cacheManager;

    // Helper function to get dream metadata
    std::shared_ptr<const Cache::Dream> getDreamMetadata(const std::string& dreamUUID) const;
    
    bool isDreamProcessed(const std::string& uuid) const;
    
    std::atomic<bool> m_isCheckingActive;
    std::atomic<bool> m_shouldTerminate;
    
    std::thread m_checkingThread;
    std::chrono::minutes m_checkInterval{60};
    std::condition_variable m_cv;
    std::mutex m_cvMutex;
    
    void initializeOfflinePlaylist();
    
    void periodicCheckThread();
    bool checkForPlaylistChanges();
    bool updatePlaylist(bool alreadyFetched = false);

    bool parsePlaylist(const std::string& playlistUUID, uint64_t expectedGeneration,
                       bool preserveCurrent = false,
                       const std::string& currentDreamUUID = {},
                       size_t oldPosition = 0);
    size_t findPositionOfDream(const std::string& dreamUUID) const;
    
    std::atomic<std::chrono::steady_clock::time_point> m_nextCheckTime;

    void updateNextCheckTime();
    
    // History stack
    // Just keep track of the play order using positions
    std::vector<size_t> m_playHistory;  // Positions in playlist order of play
    
    // Helper methods for history management
    void addToHistory(size_t position);
    void removeLastFromHistory();
    void resetPlayHistory();
    bool isDreamPlayed(const std::string& uuid) const;
    bool hasUnplayedDreams() const;
    size_t findFirstUnplayedPosition() const;

    // Loop iteration settings
    int m_loopIterations = 0;           // 0 = no special loop handling, N > 0 = play loops N times
    mutable int m_currentLoopCount = 0; // Tracks how many times we've looped at current keyframe group

    // Download lookahead configuration
    size_t m_downloadLookaheadLimit = 5;
    uint64_t m_playlistGeneration = 0;
};


#endif // PLAYLIST_MANAGER_H
