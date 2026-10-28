#include "ProgressiveHttpStream.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

#include <curl/curl.h>

#include "Log.h"
#include "Networking.h"
#include "PlatformUtils.h"

namespace Network
{
namespace
{
constexpr int64_t kAvSeekSize = 0x10000;
// Wait for the active sequential fill when the read is within this of the tip.
constexpr int64_t kFarJumpBytes = 512 * 1024;
// Treat seeks into the last N bytes as moov-at-end (always allow Range).
constexpr int64_t kMoovTailBytes = 2 * 1024 * 1024;

void MergeInterval(std::map<int64_t, int64_t>& intervals, int64_t start,
                   int64_t end)
{
    if (end <= start)
        return;

    auto it = intervals.lower_bound(start);
    if (it != intervals.begin())
    {
        auto prev = std::prev(it);
        if (prev->second >= start)
            it = prev;
    }

    int64_t mergedStart = start;
    int64_t mergedEnd = end;
    while (it != intervals.end() && it->first <= mergedEnd)
    {
        mergedStart = std::min(mergedStart, it->first);
        mergedEnd = std::max(mergedEnd, it->second);
        it = intervals.erase(it);
    }
    intervals[mergedStart] = mergedEnd;
}
} // namespace

size_t ProgressiveHttpStream_Write(char* ptr, size_t size, size_t nmemb,
                                   void* userdata)
{
    auto* state =
        static_cast<ProgressiveHttpStream::TransferState*>(userdata);
    ProgressiveHttpStream* self = state->self;
    const size_t nbytes = size * nmemb;
    if (nbytes == 0)
        return 0;

    // Only 200/206 bodies are video bytes; anything else (an error page that
    // slipped past FAILONERROR) must never land in the cache file.
    if (state->httpStatus != 200 && state->httpStatus != 206)
        return 0;

    std::lock_guard<std::mutex> lock(self->m_mutex);
    if (!self->m_file || self->ShouldAbortDownload())
        return 0;

    if (fseek(self->m_file, state->writePos, SEEK_SET) != 0)
        return 0;
    const size_t written = fwrite(ptr, 1, nbytes, self->m_file);
    if (written != nbytes)
        return 0;
    // Required before another thread fseek/fread on the same FILE*.
    fflush(self->m_file);

    self->AddFilled(state->writePos,
                    state->writePos + static_cast<int64_t>(written));
    state->writePos += static_cast<int64_t>(written);
    self->m_cv.notify_all();
    return written;
}

size_t ProgressiveHttpStream_Header(char* buffer, size_t size, size_t nitems,
                                    void* userdata)
{
    auto* state =
        static_cast<ProgressiveHttpStream::TransferState*>(userdata);
    ProgressiveHttpStream* self = state->self;
    const size_t nbytes = size * nitems;
    std::string line(buffer, nbytes);

    // Status line starts a new response (curl reports every hop of a
    // redirect chain here).
    if (line.compare(0, 5, "HTTP/") == 0)
    {
        state->httpStatus = 0;
        auto sp = line.find(' ');
        if (sp != std::string::npos)
        {
            try
            {
                state->httpStatus = std::stol(line.substr(sp + 1));
            }
            catch (...)
            {
            }
        }
        // A server that ignores Range sends the whole file from byte 0.
        if (state->httpStatus == 200)
            state->writePos = 0;
        return nbytes;
    }

    if (state->httpStatus != 200 && state->httpStatus != 206)
        return nbytes;

    auto colon = line.find(':');
    if (colon == std::string::npos)
        return nbytes;

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.erase(value.begin());
    while (!value.empty() &&
           (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();

    for (char& c : name)
        c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));

    std::lock_guard<std::mutex> lock(self->m_mutex);
    // On a 206 Content-Length is only the remaining range; the total comes
    // from Content-Range instead.
    if (name == "content-length" && state->httpStatus == 200)
    {
        try
        {
            self->m_contentLength = std::stoll(value);
            self->m_cv.notify_all();
        }
        catch (...)
        {
        }
    }
    else if (name == "content-range")
    {
        auto slash = value.find('/');
        if (slash != std::string::npos && value[slash + 1] != '*')
        {
            try
            {
                const int64_t total = std::stoll(value.substr(slash + 1));
                if (total > 0)
                {
                    self->m_contentLength = total;
                    self->m_cv.notify_all();
                }
            }
            catch (...)
            {
            }
        }
    }
    return nbytes;
}

int ProgressiveHttpStream_Progress(void* clientp)
{
    auto* self = static_cast<ProgressiveHttpStream*>(clientp);
    if (self->ShouldAbortDownload() || self->m_restart.load())
        return 1;
    return 0;
}

namespace
{
int XferInfoCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t,
                     curl_off_t)
{
    return ProgressiveHttpStream_Progress(clientp);
}
} // namespace

ProgressiveHttpStream::ProgressiveHttpStream() = default;

ProgressiveHttpStream::~ProgressiveHttpStream()
{
    Stop();
}

bool ProgressiveHttpStream::Start(const std::string& url,
                                  const std::string& cachePath,
                                  AbortFn downloadAbort)
{
    Stop();

    if (url.empty() || cachePath.empty())
        return false;

    FILE* file = fopen(cachePath.c_str(), "wb+");
    if (!file)
    {
        g_Log->Error("ProgressiveHttpStream: failed to open cache %s",
                     cachePath.c_str());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_url = url;
        m_cachePath = cachePath;
        m_downloadAbort = std::move(downloadAbort);
        m_file = file;
        m_filled.clear();
        m_contentLength = -1;
        m_bytesDownloaded = 0;
        m_urgentOffset = -1;
        m_activeTransferStart = -1;
        m_stop.store(false);
        m_failed.store(false);
        m_finished.store(false);
        m_restart.store(false);
        m_done.store(false);
    }

    m_worker = std::thread(&ProgressiveHttpStream::WorkerMain, this);
    return true;
}

void ProgressiveHttpStream::Stop()
{
    m_stop.store(true);
    m_restart.store(true);
    m_cv.notify_all();
    if (m_worker.joinable())
        m_worker.join();

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file)
    {
        fflush(m_file);
        fclose(m_file);
        m_file = nullptr;
    }
}

bool ProgressiveHttpStream::CommitTo(const std::string& finalPath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_file || !m_finished.load())
        return false;

    fflush(m_file);
    fclose(m_file);
    m_file = nullptr;

    std::error_code ec;
    std::filesystem::rename(m_cachePath, finalPath, ec);
    if (ec)
    {
        g_Log->Error("ProgressiveHttpStream: rename %s -> %s failed: %s",
                     m_cachePath.c_str(), finalPath.c_str(),
                     ec.message().c_str());
        // Keep serving readers from the tmp; the caller discards it.
        m_file = fopen(m_cachePath.c_str(), "rb");
        return false;
    }

    m_cachePath = finalPath;
    m_file = fopen(finalPath.c_str(), "rb");
    if (!m_file)
        g_Log->Warning("ProgressiveHttpStream: could not reopen %s for readers",
                       finalPath.c_str());
    m_cv.notify_all();
    return true;
}

void ProgressiveHttpStream::WaitUntilFinished()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    // Poll: downloadAbort may become true without notifying m_cv (e.g. m_bStop).
    while (!m_finished.load() && !m_failed.load() && !m_stop.load() &&
           !ShouldAbortDownload())
    {
        m_cv.wait_for(lock, std::chrono::milliseconds(100));
    }
}

bool ProgressiveHttpStream::ShouldAbortDownload() const
{
    if (m_stop.load())
        return true;
    if (m_downloadAbort && m_downloadAbort())
        return true;
    if (g_NetworkManager && g_NetworkManager->SingletonActive() &&
        g_NetworkManager->IsAborted())
        return true;
    return false;
}

bool ProgressiveHttpStream::ShouldInterruptRead(const Cursor& cursor) const
{
    if (ShouldAbortDownload())
        return true;
    if (cursor.interrupt && cursor.interrupt())
        return true;
    return false;
}

int64_t ProgressiveHttpStream::Size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_contentLength;
}

int64_t ProgressiveHttpStream::BytesDownloaded() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bytesDownloaded;
}

int64_t ProgressiveHttpStream::ContiguousEndLocked() const
{
    if (m_filled.empty())
        return 0;
    auto it = m_filled.begin();
    if (it->first != 0)
        return 0;
    return it->second;
}

int64_t ProgressiveHttpStream::AvailableFromLocked(int64_t pos) const
{
    if (pos < 0)
        return 0;
    for (const auto& range : m_filled)
    {
        if (pos >= range.first && pos < range.second)
            return range.second - pos;
        if (range.first > pos)
            break;
    }
    return 0;
}

void ProgressiveHttpStream::AddFilled(int64_t start, int64_t end)
{
    MergeInterval(m_filled, start, end);
    m_bytesDownloaded = 0;
    for (const auto& range : m_filled)
        m_bytesDownloaded += (range.second - range.first);
}

void ProgressiveHttpStream::RequestOffset(int64_t offset)
{
    if (offset < 0)
        return;
    if (AvailableFromLocked(offset) > 0)
        return;
    if (m_contentLength >= 0 && offset >= m_contentLength)
        return;

    const int64_t contig = ContiguousEndLocked();
    const bool nearEnd =
        m_contentLength > 0 && offset + kMoovTailBytes >= m_contentLength;
    const bool farJump = offset > contig + kFarJumpBytes;

    // Active GET writes forward from activeStart. Waiting reaches offset unless
    // this is a large seek ahead of a from-start download (or moov-at-end).
    if (m_activeTransferStart >= 0 && m_activeTransferStart <= offset)
    {
        const int64_t activeTip =
            m_activeTransferStart +
            std::max<int64_t>(0, AvailableFromLocked(m_activeTransferStart));
        const bool closeToActive =
            offset <= m_activeTransferStart + kFarJumpBytes ||
            offset <= activeTip + kFarJumpBytes;
        if (closeToActive)
            return;
        if (!farJump && !nearEnd)
            return;
    }
    else if (!farJump && !nearEnd)
    {
        // Sequential worker will catch up from ContiguousEnd — do not abort.
        return;
    }

    if (m_urgentOffset == offset && m_restart.load())
        return;

    m_urgentOffset = offset;
    m_restart.store(true);
}

bool ProgressiveHttpStream::RunTransfer(int64_t startOffset)
{
    if (!PlatformUtils::IsInternetReachable())
    {
        g_Log->Info("ProgressiveHttpStream: network unreachable");
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    TransferState state{this, startOffset};
    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeTransferStart = startOffset;
        m_restart.store(false);
    }

    std::string rangeHeader;
    struct curl_slist* headers = nullptr;
    if (startOffset > 0)
    {
        rangeHeader = "Range: bytes=" + std::to_string(startOffset) + "-";
        headers = curl_slist_append(headers, rangeHeader.c_str());
    }

    bool ok = true;
    auto set = [&](CURLcode code) {
        if (code != CURLE_OK)
            ok = false;
    };

    set(g_NetworkManager->Prepare(curl));
    set(curl_easy_setopt(curl, CURLOPT_URL, m_url.c_str()));
    set(curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L));
    set(curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L));
    set(curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L));
    set(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf));
    set(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &ProgressiveHttpStream_Write));
    set(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state));
    set(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,
                         &ProgressiveHttpStream_Header));
    set(curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state));
    // Fail on 4xx/5xx before any body is delivered.
    set(curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L));
    set(curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &XferInfoCallback));
    set(curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this));
    set(curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L));
    // Stall detection: abort transfers that make no progress (Wi-Fi drop, sleep).
    set(curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L)); // 1 KB/s
    set(curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L));    // 30 s
    set(curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L));
    if (headers)
        set(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));

    CURLcode rc = CURLE_OK;
    if (ok)
        rc = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_off_t cl = -1;
    if (curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl) ==
            CURLE_OK &&
        cl > 0 && httpCode == 200)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contentLength < 0)
            m_contentLength = static_cast<int64_t>(cl);
    }

    if (headers)
        curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_activeTransferStart = -1;
    }
    m_cv.notify_all();

    if (ShouldAbortDownload() || m_restart.load())
        return true;

    if (rc == CURLE_ABORTED_BY_CALLBACK)
        return true;

    if (rc != CURLE_OK)
    {
        g_Log->Warning("ProgressiveHttpStream: curl %s (%s)",
                       curl_easy_strerror(rc), errbuf);
        return false;
    }

    if (httpCode != 200 && httpCode != 206)
    {
        g_Log->Warning("ProgressiveHttpStream: HTTP %ld for %s", httpCode,
                       m_url.c_str());
        return false;
    }
    return true;
}

void ProgressiveHttpStream::WorkerMain()
{
    g_Log->Info("ProgressiveHttpStream: worker start");

    constexpr int kMaxTransferRetries = 3;
    int consecutiveFailures = 0;

    while (!ShouldAbortDownload())
    {
        int64_t start = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_urgentOffset >= 0 && AvailableFromLocked(m_urgentOffset) <= 0)
            {
                start = m_urgentOffset;
                m_urgentOffset = -1;
            }
            else
            {
                start = ContiguousEndLocked();
                if (m_contentLength >= 0 && start >= m_contentLength)
                {
                    m_finished.store(true);
                    m_cv.notify_all();
                    break;
                }
            }
        }

        if (!RunTransfer(start))
        {
            if (ShouldAbortDownload())
                break;

            ++consecutiveFailures;
            if (consecutiveFailures >= kMaxTransferRetries)
            {
                g_Log->Warning(
                    "ProgressiveHttpStream: giving up after %d failed transfers",
                    consecutiveFailures);
                m_failed.store(true);
                m_cv.notify_all();
                break;
            }

            int64_t retryFrom = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                retryFrom = ContiguousEndLocked();
            }
            g_Log->Info(
                "ProgressiveHttpStream: transfer failed, retry %d/%d from offset %lld",
                consecutiveFailures, kMaxTransferRetries,
                static_cast<long long>(retryFrom));
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        consecutiveFailures = 0;

        if (m_restart.load() && !ShouldAbortDownload())
            continue;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const int64_t contig = ContiguousEndLocked();
            if (m_contentLength >= 0 && contig >= m_contentLength)
            {
                m_finished.store(true);
                m_cv.notify_all();
                break;
            }
            // Full GET completed with no Content-Length: contig is the size.
            if (m_contentLength < 0 && start == 0 && !m_restart.load() &&
                m_urgentOffset < 0 && contig > 0)
            {
                m_contentLength = contig;
                m_finished.store(true);
                m_cv.notify_all();
                break;
            }
        }
    }

    m_done.store(true);
    m_cv.notify_all();
    g_Log->Info("ProgressiveHttpStream: worker end (finished=%d failed=%d)",
                m_finished.load() ? 1 : 0, m_failed.load() ? 1 : 0);
}

int ProgressiveHttpStream::Read(Cursor& cursor, uint8_t* buf, int size)
{
    if (!buf || size <= 0)
        return 0;

    std::unique_lock<std::mutex> lock(m_mutex);
    while (true)
    {
        if (ShouldInterruptRead(cursor))
            return -1;

        if (m_failed.load() || !m_file)
            return -1;

        if (m_contentLength >= 0 && cursor.pos >= m_contentLength)
            return 0;

        const int64_t available = AvailableFromLocked(cursor.pos);
        if (available > 0)
        {
            const int toRead =
                static_cast<int>(std::min<int64_t>(available, size));
            if (fseek(m_file, cursor.pos, SEEK_SET) != 0)
                return -1;
            const size_t n = fread(buf, 1, toRead, m_file);
            if (n == 0)
                return -1;
            cursor.pos += static_cast<int64_t>(n);
            return static_cast<int>(n);
        }

        if (m_finished.load())
            return 0;

        RequestOffset(cursor.pos);
        m_cv.wait_for(lock, std::chrono::milliseconds(100));
    }
}

int64_t ProgressiveHttpStream::Seek(Cursor& cursor, int64_t offset, int whence)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (whence == kAvSeekSize)
    {
        if (m_contentLength < 0)
        {
            m_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                return m_contentLength >= 0 || m_finished.load() ||
                       m_failed.load() || ShouldInterruptRead(cursor);
            });
        }
        if (m_contentLength >= 0)
            return m_contentLength;
        if (m_finished.load())
            return ContiguousEndLocked();
        return -1;
    }

    int64_t newPos = cursor.pos;
    switch (whence)
    {
        case SEEK_SET:
            newPos = offset;
            break;
        case SEEK_CUR:
            newPos = cursor.pos + offset;
            break;
        case SEEK_END:
            if (m_contentLength < 0)
            {
                m_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                    return m_contentLength >= 0 || m_finished.load() ||
                           ShouldInterruptRead(cursor);
                });
            }
            if (m_contentLength < 0)
                return -1;
            newPos = m_contentLength + offset;
            break;
        default:
            return -1;
    }

    if (newPos < 0)
        return -1;
    if (m_contentLength >= 0 && newPos > m_contentLength)
        newPos = m_contentLength;

    cursor.pos = newPos;
    if (AvailableFromLocked(newPos) <= 0 && !m_finished.load())
        RequestOffset(newPos);
    return newPos;
}

// MARK: - DownloadRegistry

DownloadRegistry& DownloadRegistry::Instance()
{
    static DownloadRegistry s_instance;
    return s_instance;
}

bool DownloadRegistry::Claim(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.emplace(id, Entry{nullptr, true}).second;
}

void DownloadRegistry::Publish(const std::string& id,
                               std::shared_ptr<ProgressiveHttpStream> stream)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(id);
        if (it == m_entries.end() || !it->second.shareable || it->second.stream)
            return;
        it->second.stream = std::move(stream);
    }
    m_cv.notify_all();
}

bool DownloadRegistry::AddPrivate(const std::string& id,
                                  std::shared_ptr<ProgressiveHttpStream> stream)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.emplace(id, Entry{std::move(stream), false}).second;
}

void DownloadRegistry::Remove(const std::string& id,
                              const ProgressiveHttpStream* stream)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_entries.find(id);
        if (it == m_entries.end() || it->second.stream.get() != stream)
            return;
        m_entries.erase(it);
    }
    m_cv.notify_all();
}

bool DownloadRegistry::Contains(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.count(id) != 0;
}

std::shared_ptr<ProgressiveHttpStream>
DownloadRegistry::Attach(const std::string& id,
                         const ProgressiveHttpStream::AbortFn& interrupt)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    while (true)
    {
        auto it = m_entries.find(id);
        if (it == m_entries.end() || !it->second.shareable)
            return nullptr;
        if (it->second.stream)
        {
            // Under m_mutex, so an owner that Remove()s and then sees no
            // readers knows none can attach any more.
            it->second.stream->AddReader();
            return it->second.stream;
        }
        // Claimed: the prefetcher is fetching the signed URL. Poll so
        // interrupt is honored even without a notify.
        if (interrupt && interrupt())
            return nullptr;
        m_cv.wait_for(lock, std::chrono::milliseconds(100));
    }
}

} // namespace Network
