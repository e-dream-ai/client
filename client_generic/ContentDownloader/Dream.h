//
//  Dream.h
//  e-dream
//
//  Created by Guillaume Louel on 31/07/2024.
//

#ifndef DREAM_H
#define DREAM_H

#include <string>
#include <cmath>

namespace Cache {

struct Dream {
    std::string uuid;
    std::string name;
    std::string artist;
    long long size;
    std::string status;
    std::string fps;
    int frames;
    std::string thumbnail;
    int upvotes;
    int downvotes;
    bool nsfw;
    std::string frontendUrl;
    long long video_timestamp;
    long long timestamp;
    float activityLevel = 1.f;
    mutable std::string streamingUrl;
    
    // Possibly useful helpers
    double getDuration() const {
        return frames / std::stod(fps);
    }

    std::string getFormattedSize() const {
        const double KB = 1024.0;
        const double MB = KB * 1024.0;
        const double GB = MB * 1024.0;

        if (size > GB) {
            return std::to_string(size / GB) + " GB";
        } else if (size > MB) {
            return std::to_string(size / MB) + " MB";
        } else if (size > KB) {
            return std::to_string(size / KB) + " KB";
        } else {
            return std::to_string(size) + " bytes";
        }
    }
    
    // Those are implemented in CacheManager.h
    std::string getCachedPath() const;
    bool isCached() const;
    
    std::string getStreamingUrl() const {
        return streamingUrl;
    }

    void setStreamingUrl(const std::string& url) const {
        streamingUrl = url;
    }
};

} // namespace Cache

#endif // DREAM_H
