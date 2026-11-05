#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Network
{

// Progressive HTTPS download into a seekable on-disk store for FFmpeg AVIO.
// Background libcurl fills the cache file; Read/Seek block until the requested
// bytes are present. Far seeks / moov-at-end trigger HTTP Range GETs; nearby
// forward reads wait on the active sequential transfer instead of aborting it.
// Several readers may share one download, each through its own Cursor.
class ProgressiveHttpStream
{
  public:
    using AbortFn = std::function<bool()>;

    // One reader's position. interrupt unblocks that reader's Read/Seek
    // (e.g. its decoder stopped) without touching the download.
    struct Cursor
    {
        int64_t pos = 0;
        AbortFn interrupt;
    };

    ProgressiveHttpStream();
    ~ProgressiveHttpStream();

    ProgressiveHttpStream(const ProgressiveHttpStream&) = delete;
    ProgressiveHttpStream& operator=(const ProgressiveHttpStream&) = delete;

    // Opens cachePath (wb+) and starts a background download of url.
    // downloadAbort: cancel the curl worker (and fail every reader).
    bool Start(const std::string& url, const std::string& cachePath,
               AbortFn downloadAbort);
    // Aborts the transfer, joins the worker and closes the cache file.
    void Stop();

    // Once Finished(): move the cache file to finalPath and keep serving
    // readers from there. Closes and reopens under the lock, because Windows
    // cannot rename a file that is open.
    bool CommitTo(const std::string& finalPath);

    // Block until the background transfer finishes (success, failure, or abort).
    // Polls so a downloadAbort that does not notify still wakes promptly.
    void WaitUntilFinished();

    // AVIO-style read from the cursor's position.
    int Read(Cursor& cursor, uint8_t* buf, int size);
    // whence: SEEK_SET / SEEK_CUR / SEEK_END, or AVSEEK_SIZE (0x10000).
    int64_t Seek(Cursor& cursor, int64_t offset, int whence);

    int64_t Size() const;
    int64_t BytesDownloaded() const;
    bool Failed() const { return m_failed.load(); }
    bool Finished() const { return m_finished.load(); }
    // The worker has exited: finished, failed, or aborted.
    bool Done() const { return m_done.load(); }

    // Clips reading a shared download (counted by DownloadRegistry::Attach).
    void AddReader() { ++m_readers; }
    void RemoveReader() { --m_readers; }
    int Readers() const { return m_readers.load(); }

    // "<cache file name>@<this>" for logs, so lines from different streams
    // of the same dream can be told apart (client#728).
    const std::string& Label() const { return m_label; }

    struct TransferState
    {
        ProgressiveHttpStream* self = nullptr;
        int64_t writePos = 0;
        // Status of the response currently being received (reset on each
        // status line, so redirect and 1xx headers are never trusted).
        long httpStatus = 0;
    };

  private:
    friend size_t ProgressiveHttpStream_Write(char*, size_t, size_t, void*);
    friend size_t ProgressiveHttpStream_Header(char*, size_t, size_t, void*);
    friend int ProgressiveHttpStream_Progress(void*);

    void WorkerMain();
    bool RunTransfer(int64_t startOffset);
    void RequestOffset(int64_t offset);
    void AddFilled(int64_t start, int64_t end);
    int64_t ContiguousEndLocked() const;
    int64_t AvailableFromLocked(int64_t pos) const;
    bool ShouldAbortDownload() const;
    bool ShouldInterruptRead(const Cursor& cursor) const;
    // Which ShouldAbortDownload() condition holds right now, or "none".
    const char* AbortReason() const;

    std::string m_url;
    std::string m_cachePath;
    std::string m_label;
    AbortFn m_downloadAbort;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    FILE* m_file = nullptr;
    std::map<int64_t, int64_t> m_filled; // [start, end) merged intervals
    int64_t m_contentLength = -1;
    int64_t m_bytesDownloaded = 0;
    int64_t m_urgentOffset = -1; // seek target not yet covered
    int64_t m_activeTransferStart = -1;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_failed{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_restart{false};
    std::atomic<bool> m_done{false};
    std::atomic<int> m_readers{0};

    std::thread m_worker;
};

// Process-wide map of in-flight dream downloads, keyed by dream UUID, so the
// prefetcher and the player never fetch the same dream twice (client#706).
// Shareable entries belong to the prefetcher, which always finishes and
// caches them; a player clip attaches to one with its own Cursor. Private
// entries are player streams, which abort when their clip stops.
class DownloadRegistry
{
  public:
    static DownloadRegistry& Instance();

    // Reserve id for a shareable download before its URL is known.
    // False if a download of id is already in flight.
    bool Claim(const std::string& id);
    // Attach the started stream to a Claim.
    void Publish(const std::string& id,
                 std::shared_ptr<ProgressiveHttpStream> stream);
    // Register a private download. False if id is already in flight.
    bool AddPrivate(const std::string& id,
                    std::shared_ptr<ProgressiveHttpStream> stream);
    // Drop id if its entry is still stream (nullptr: an unpublished Claim).
    void Remove(const std::string& id, const ProgressiveHttpStream* stream);

    bool Contains(const std::string& id) const;
    // The shareable stream for id, waiting while it is claimed but not yet
    // published. nullptr if there is none, or interrupt fires. Counts the
    // caller as a reader; it calls RemoveReader() when done.
    std::shared_ptr<ProgressiveHttpStream>
    Attach(const std::string& id, const ProgressiveHttpStream::AbortFn& interrupt);

  private:
    struct Entry
    {
        std::shared_ptr<ProgressiveHttpStream> stream;
        bool shareable = false;
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<std::string, Entry> m_entries;
};

} // namespace Network
