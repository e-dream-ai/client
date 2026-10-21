#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace Network
{

// Progressive HTTPS download into a seekable on-disk store for FFmpeg AVIO.
// Background libcurl fills the cache file; Read/Seek block until the requested
// bytes are present. Far seeks / moov-at-end trigger HTTP Range GETs; nearby
// forward reads wait on the active sequential transfer instead of aborting it.
class ProgressiveHttpStream
{
  public:
    using AbortFn = std::function<bool()>;

    ProgressiveHttpStream();
    ~ProgressiveHttpStream();

    ProgressiveHttpStream(const ProgressiveHttpStream&) = delete;
    ProgressiveHttpStream& operator=(const ProgressiveHttpStream&) = delete;

    // Opens cachePath (wb+) and starts a background download of url.
    // readInterrupt: unblock AVIO Read/Seek (e.g. decoder stop).
    // downloadAbort: cancel the curl worker (defaults to readInterrupt).
    bool Start(const std::string& url, const std::string& cachePath,
               AbortFn readInterrupt, AbortFn downloadAbort = {});
    // Aborts the transfer and joins the worker.
    void Stop();

    // Block until the background transfer finishes (success, failure, or abort).
    // Polls so a downloadAbort that does not notify still wakes promptly.
    void WaitUntilFinished();

    // AVIO-style read from the current read position.
    int Read(uint8_t* buf, int size);
    // whence: SEEK_SET / SEEK_CUR / SEEK_END, or AVSEEK_SIZE (0x10000).
    int64_t Seek(int64_t offset, int whence);

    int64_t Size() const;
    int64_t BytesDownloaded() const;
    bool Failed() const { return m_failed.load(); }
    bool Finished() const { return m_finished.load(); }

    struct TransferState
    {
        ProgressiveHttpStream* self = nullptr;
        int64_t writePos = 0;
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
    bool ShouldInterruptRead() const;

    std::string m_url;
    AbortFn m_readInterrupt;
    AbortFn m_downloadAbort;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    FILE* m_file = nullptr;
    std::map<int64_t, int64_t> m_filled; // [start, end) merged intervals
    int64_t m_contentLength = -1;
    int64_t m_readPos = 0;
    int64_t m_bytesDownloaded = 0;
    int64_t m_urgentOffset = -1; // seek target not yet covered
    int64_t m_activeTransferStart = -1;

    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_failed{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_restart{false};

    std::thread m_worker;
};

} // namespace Network
