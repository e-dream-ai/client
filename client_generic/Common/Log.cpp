
#include <inttypes.h>

#ifdef MAC
#include <os/signpost.h>
#endif

#ifdef WIN32
#include <io.h> // Import read/write in msvc
#endif

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#include <stdio.h>
#include <sstream>
#include <cstdarg>
#include <string>
#include <time.h>
#include <thread>
#include <filesystem>
#include <system_error>
#include <iostream>
#include <regex>
#include <chrono>
#include <vector>
#include <algorithm>

#include <boost/log/utility/setup/formatter_parser.hpp>
#include <boost/log/utility/exception_handler.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "Log.h"
#include "base.h"
#include "PlatformUtils.h"

#define BUFFER_SIZE 1024 * 128

namespace Base
{

/*
        PruneOldLogs().
        Deletes the client's own logs (YYYY_MM_DD.log and YYYY_MM_DD_pidNNN.log)
        that are older than kLogRetention, then, if the rest still add up to more
        than kLogDirSizeCap, the oldest ones until they don't (issue #740).

        Another process may be writing to one of these files: the app and
        legacyScreenSaver share the day's log, and a second instance writes a
        _pid log. On Windows deleting an open file just fails, which we ignore.
        Elsewhere it would succeed and the writer would carry on into an
        unlinked file, so files written to within kRecentlyWritten are never
        deleted, and neither is keepName, the file this process is about to use.

        Never throws: a full or unwritable disk must not stop logging (#664).
*/
static const auto kLogRetention = std::chrono::hours(24 * 14);
static const std::uintmax_t kLogDirSizeCap = 256ull * 1024 * 1024;
static const auto kRecentlyWritten = std::chrono::hours(1);

LogPruneResult PruneOldLogs(const std::filesystem::path& dir,
                            const std::string& keepName) noexcept
{
    namespace fs = std::filesystem;
    LogPruneResult result;
    try
    {
        static const std::regex logName(R"(\d{4}_\d{2}_\d{2}(_pid\d+)?\.log)");

        struct Entry
        {
            fs::path path;
            fs::file_time_type mtime;
            std::uintmax_t size;
        };
        std::vector<Entry> candidates;
        std::uintmax_t total = 0;

        const auto now = fs::file_time_type::clock::now();
        std::error_code ec;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
             it.increment(ec))
        {
            const fs::path& p = it->path();
            const std::string name = p.filename().string();
            if (!std::regex_match(name, logName))
                continue;
            std::error_code fec;
            if (!it->is_regular_file(fec) || fec)
                continue;
            const std::uintmax_t size = it->file_size(fec);
            if (fec)
                continue;
            const fs::file_time_type mtime = it->last_write_time(fec);
            if (fec)
                continue;

            total += size;
            if (name == keepName || now - mtime < kRecentlyWritten)
                continue;
            candidates.push_back({p, mtime, size});
        }

        // Oldest first.
        std::sort(candidates.begin(), candidates.end(),
                  [](const Entry& a, const Entry& b)
                  { return a.mtime < b.mtime; });

        for (const Entry& e : candidates)
        {
            if (now - e.mtime < kLogRetention && total <= kLogDirSizeCap)
                break;
            std::error_code rec;
            if (fs::remove(e.path, rec) && !rec)
            {
                total -= e.size;
                ++result.removedFiles;
                result.removedBytes += e.size;
            }
        }
    }
    catch (...)
    {
        // Best effort only.
    }
    return result;
}

/*
        CLog().

*/
CLog::CLog()
: m_bActive(false), m_pFile(NULL), m_OriginalSTDOUT(fileno(stdout)), m_PipeReader(0),
    m_pPipeReaderThread(nullptr)
{
}

/*
        ~CLog().

*/
CLog::~CLog()
{
    //    Mark singleton as shutdown, to track unwanted access after this point.
    SingletonActive(false);
}

/*
        Startup().

*/
bool CLog::Startup()
{
    BOOST_LOG_TRIVIAL(info) << "Log Startup";
    m_pFile = NULL;
    return (true);
}

/*
        Shutdown().

*/
bool CLog::Shutdown(void)
{
    Info("Logger shutting down...");
    Info("dummy");
    Detach();

    return (true);
}

void CLog::PipeReaderThread()
{

    PlatformUtils::SetThreadName("Log");
    while (1)
    {
        char buffer[BUFFER_SIZE];
        ssize_t bytes_read = read(m_PipeReader, buffer, BUFFER_SIZE);
        if (bytes_read > 0)
        {
            // Do something with the data read from stdout
            //fwrite(buffer, 1, bytes_read, log_);
            write(m_OriginalSTDOUT, buffer, (size_t)bytes_read);
            fprintf(m_pFile, "%s", buffer);
            fflush(m_pFile);
        }
        else
        {
            break;
        }
    }
}

void CLog::Attach(const std::string& _location, bool multipleInstance)
{
    if (m_bActive)
    {
        return;
    }

    // Add console sink
    boost::shared_ptr<boost::log::sinks::text_ostream_backend> console_backend =
        boost::make_shared<boost::log::sinks::text_ostream_backend>();
    console_backend->add_stream(
        boost::shared_ptr<std::ostream>(&std::cout, boost::null_deleter()));

    m_ConsoleSink = boost::make_shared<boost::log::sinks::synchronous_sink<
        boost::log::sinks::text_ostream_backend>>(console_backend);

    m_ConsoleSink->set_formatter(
        boost::log::expressions::stream
        << "["
        << boost::log::expressions::attr<boost::posix_time::ptime>("TimeStamp")
        << " - " << boost::log::trivial::severity
        << "]: " << boost::log::expressions::smessage);

    boost::log::core::get()->add_sink(m_ConsoleSink);

    // File name pattern: <location>YYYY_MM_DD[_pidNNN].log. Boost fills in the
    // date each time it opens a file, so with the midnight rotation below a
    // long-running process starts a new dated file every day (issue #740).
    // Placeholders are only expanded in the file name, not the directory.
    std::stringstream s;
    s << _location << "%Y_%m_%d";

    if (multipleInstance)
    {
        pid_t pid = getpid();
        s << "_pid" << pid;
    }

    s << ".log";
    std::string f = s.str();

    // Only attach a file sink if the log directory can actually be created.
    // The boost.log file backend creates the directory lazily (on first write),
    // so a full or unwritable disk would otherwise throw an uncaught
    // filesystem_error - both here and on every subsequent log call - and abort
    // the app at launch (issue #664). When it's not writable we keep the
    // console sink and skip file logging.
    std::error_code ec;
    std::filesystem::create_directories(_location, ec);
    const bool dirReady = std::filesystem::is_directory(_location, ec);

    if (dirReady)
    {
        try
        {
            auto backend =
                boost::make_shared<boost::log::sinks::text_file_backend>(
                    boost::log::keywords::file_name = f,
                    // Append: each legacyScreenSaver launch is a new process,
                    // and the default (trunc) wiped earlier sessions from the
                    // day's log. Rotation reopens with the new day's name, so
                    // this also appends if another process already made it.
                    boost::log::keywords::open_mode = std::ios_base::app,
                    boost::log::keywords::time_based_rotation =
                        boost::log::sinks::file::rotation_at_time_point(0, 0,
                                                                        0));

            // Called whenever a file is opened: on the first write after
            // launch, and after every midnight rotation. The sink is locked
            // here, so report through the stream, never through the logger.
            // The handler is owned by the backend, so the raw pointer is safe.
            boost::log::sinks::text_file_backend* rawBackend = backend.get();
            const std::filesystem::path logDir(_location);
            backend->set_open_handler(
                [rawBackend, logDir](std::ostream& stream)
                {
                    const LogPruneResult r =
                        PruneOldLogs(logDir, rawBackend->get_current_file_name()
                                                 .filename()
                                                 .string());
                    if (r.removedFiles > 0)
                    {
                        // Same timestamp format as the formatter's.
                        std::string when =
                            boost::posix_time::to_iso_extended_string(
                                boost::posix_time::microsec_clock::local_time());
                        std::replace(when.begin(), when.end(), 'T', ' ');
                        stream << "[" << when
                               << " - info]: Removed " << r.removedFiles
                               << " old log file(s), " << r.removedBytes
                               << " bytes\n";
                    }
                });

            m_Sink = boost::make_shared<boost::log::sinks::synchronous_sink<
                boost::log::sinks::text_file_backend>>(backend);
            m_Sink->set_formatter(boost::log::parse_formatter(
                "[%TimeStamp% - %Severity%]: %Message%"));
            boost::log::core::get()->add_sink(m_Sink);
        }
        catch (const std::exception& e)
        {
            if (m_Sink)
                boost::log::core::get()->remove_sink(m_Sink);
            m_Sink.reset();
            std::cerr << "Failed to attach file log at " << f << " ("
                      << e.what() << "); using console logging only."
                      << std::endl;
        }
    }
    else
    {
        std::cerr << "Log directory " << _location << " is not writable ("
                  << ec.message() << "); using console logging only."
                  << std::endl;
    }

    boost::log::core::get()->set_filter(boost::log::trivial::severity >=
                                        boost::log::trivial::info);

    // A sink that fails to write, or to open the next day's file at midnight
    // rotation (full or unwritable disk), throws out of whatever code was
    // logging. Drop that record instead (issues #664, #740).
    boost::log::core::get()->set_exception_handler(
        boost::log::make_exception_suppressor());

    boost::log::sources::severity_logger<boost::log::trivial::severity_level>
        lg;

    boost::log::add_common_attributes();

    if (m_Sink)
    {
        try
        {
            BOOST_LOG_TRIVIAL(info) << "Attaching Log";
            m_Sink->flush();
        }
        catch (const std::exception& e)
        {
            // File became unwritable between the check and first write; drop the
            // file sink and carry on with console logging.
            boost::log::core::get()->remove_sink(m_Sink);
            m_Sink.reset();
            std::cerr << "File logging disabled (" << e.what() << ")."
                      << std::endl;
        }
    }

    m_bActive = true;
}

/*
 */
void CLog::Detach(void)
{
    if (m_ConsoleSink)
    {
        boost::log::core::get()->remove_sink(m_ConsoleSink);
        m_ConsoleSink.reset();
    }

    //fflush(stdout);
    //m_bActive = false;
    //if (m_PipeReader)
    //    close(m_PipeReader);
    //m_PipeReader = 0;
    //m_pPipeReaderThread->join();
    //m_pPipeReaderThread = nullptr;
    //fflush(m_pFile);
    //if (m_pFile)
    //    fclose(m_pFile);
    //if (dup2(m_OriginalSTDOUT, fileno(stdout)) == -1)
    //{
    //    perror("dup2 failed");
    //    exit(EXIT_FAILURE);
    //}
}

/*
        SetFile().
        Store file that did the logging.
*/
void CLog::SetInfo(const char* _pFileStr, const uint32_t _line,
                   const char* _pFunc)
{

    // std::scoped_lock locker( m_Lock );

    //std::string tmp = _pFileStr;
    //size_t offs = tmp.find_last_of("/\\", tmp.size());

    //m_File = tmp.substr(offs + 1, tmp.size());
    //m_Function = std::string(_pFunc);
    //m_Line = _line;
}

/*
 */

char CLog::s_MessageSpam[m_MaxMessageLength] = {0};
char CLog::s_MessageType[m_MaxMessageLength] = {0};
size_t CLog::s_MessageSpamCount = 0;

void CLog::Log(
    const char* _pType,
    /*const char *_file, const uint32_t _line, const char *_pFunc,*/ const char*
        _pStr)
{

    if (!m_bSingletonActive)
        return;
    //    std::scoped_lock locker(m_Lock);

    // Keeping for backward compatibility
    if (strcmp(_pType, "DEBUG") == 0)
    {
        BOOST_LOG_TRIVIAL(debug) << _pStr;
    }
    else if (strcmp(_pType, "INFO") == 0)
    {
        BOOST_LOG_TRIVIAL(info) << _pStr;
    }
    else if (strcmp(_pType, "WARN") == 0)
    {
        BOOST_LOG_TRIVIAL(warning) << _pStr;
    }
    else if (strcmp(_pType, "ERROR") == 0)
    {
        BOOST_LOG_TRIVIAL(error) << _pStr;
    }
    else
    {
        BOOST_LOG_TRIVIAL(fatal) << _pStr;
    }

    // Flush the sink in debug mode so we can see the logs in real time
    //#ifdef DEBUG
    //    m_Sink->flush();
    //#endif

    if (m_Sink != nullptr)
        m_Sink->flush();

    // if (m_pFile == NULL)
    // return;

    //time_t curTime;
    //time(&curTime);

    //char timeStamp[32] = {0};
    //strftime(timeStamp, sizeof(timeStamp), "%H:%M:%S", localtime(&curTime));

    ///* log spam start*/
    //if (strcmp(_pStr, s_MessageSpam) == 0)
    //{
    //    ++s_MessageSpamCount;
    //}
    //else
    //{
    //    if (s_MessageSpamCount > 0)
    //    {
    //        printf("[%s-%s]: '%s' x%lu\n", s_MessageType, timeStamp,
    //               s_MessageSpam, s_MessageSpamCount);
    //    }
    //    else
    //    {
    //        //    Not active/attached, dump to stdout.
    //        // fprintf( stdout, "[%s]: %s - %s[%s(%d)]: '%s'\n", _pType,
    //        // timeStamp, _file, _pFunc, m_Line, _pStr );
    //        printf("[%s-%s]: '%s'\n", s_MessageType, timeStamp, s_MessageSpam);
    //    }

    //    s_MessageSpamCount = 0;
    //    memcpy(s_MessageSpam, _pStr, m_MaxMessageLength);
    //    strcpy(s_MessageType, _pType);
    //}
    ///* log spam end */
}

#define grabvarargs                                                            \
    va_list ArgPtr;                                                            \
    char pTempStr[m_MaxMessageLength];                                         \
    std::string fmtString(_pFmt);                                              \
    va_start(ArgPtr, _pFmt);                                                   \
    vsnprintf(pTempStr, m_MaxMessageLength, fmtString.c_str(), ArgPtr);        \
    va_end(ArgPtr);

//    Def our loggers.
void CLog::Debug(std::string_view _pFmt, ...)
{
    grabvarargs Log("DEBUG",
                    /*m_File.c_str(), m_Line, m_Function.c_str(),*/ pTempStr);
}
void CLog::Info(std::string_view _pFmt, ...)
{
    grabvarargs Log("INFO",
                    /*m_File.c_str(), m_Line, m_Function.c_str(),*/ pTempStr);
}
void CLog::Warning(std::string_view _pFmt, ...)
{
    grabvarargs Log("WARN",
                    /*m_File.c_str(), m_Line, m_Function.c_str(),*/ pTempStr);
}
void CLog::Error(std::string_view _pFmt, ...)
{
    grabvarargs Log("ERROR",
                    /*m_File.c_str(), m_Line, m_Function.c_str(),*/ pTempStr);
    PlatformUtils::NotifyError(pTempStr);
}
void CLog::Fatal(std::string_view _pFmt, ...)
{
    grabvarargs Log("FATAL",
                    /*m_File.c_str(), m_Line, m_Function.c_str(),*/ pTempStr);
}

}; // namespace Base

#ifdef MAC
os_log_t g_SignpostHandle = os_log_create("com.spotworks.e-dream.app",
                                          OS_LOG_CATEGORY_POINTS_OF_INTEREST);
#endif