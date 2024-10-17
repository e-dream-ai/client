/*
        LOG.H
        Author: Stef.

        Implements singleton logging class.
*/
#ifndef _LOG_H_
#define _LOG_H_

#include <boost/core/null_deleter.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <iostream>

#include <string_view>
#include <mutex>
#include <thread>

#ifdef LINUX_GNU
#include <cstdio>
#endif

#include "Singleton.h"
#include "SmartPtr.h"
#include "base.h"

#if 0

#ifdef DEBUG
// Define a macro for logging
#define DEBUG_LOG(fmt, ...)                                                    \
    do                                                                         \
    {                                                                          \
        time_t current_time = time(NULL);                                      \
        char* time_str = ctime(&current_time);                                 \
        time_str[strlen(time_str) - 1] =                                       \
            '\0'; /* Removing newline from ctime output */                     \
        FILE* fptr;                                                            \
        fptr = fopen("/Users/Shared/e-dream.ai/Logs/debug.log", "a");          \
        fprintf(fptr, "[%s] [%s:%d] " fmt "\n", time_str, __FILE__, __LINE__,  \
                ##__VA_ARGS__);                                                \
        fclose(fptr);                                                          \
    } while (0)
#endif
#else
#define DEBUG_LOG(fmt, ...)
#endif

extern void ProfilerBegin(const char* name);
extern void ProfilerEnd(const char* name);

namespace Base
{

/*
        CLog.

*/
MakeSmartPointers(CLog);

class CLog : public CSingleton<CLog>
{
    friend class CSingleton<CLog>;

    std::mutex m_Lock;

    static const uint32_t m_MaxMessageLength = 4096;
    static char s_MessageSpam[m_MaxMessageLength];
    static char s_MessageType[m_MaxMessageLength];
    static size_t s_MessageSpamCount;

    
    //    Private constructor accessible only to CSingleton.
    CLog();

    //    No copy constructor or assignment operator.
    NO_CLASS_STANDARDS(CLog);

    bool m_bActive;

    FILE* m_pFile;

    //    Temporary storage vars.
    std::string m_File;
    std::string m_Function;
    uint32_t m_Line;
    int m_OriginalSTDOUT;
    int m_PipeReader;
    class std::thread* m_pPipeReaderThread;

    boost::shared_ptr<boost::log::sinks::synchronous_sink<
        boost::log::sinks::text_file_backend>>
        m_Sink;

    boost::shared_ptr<boost::log::sinks::synchronous_sink<
        boost::log::sinks::text_ostream_backend>> m_ConsoleSink;
    
    void Log(const char* _pType,
             /*const char *_file, const uint32_t _line, const char *_pFunc,*/
             const char* _pStr);
    void PipeReaderThread();

  public:
    bool Startup();
    bool Shutdown(void);
    virtual ~CLog();

    const char* Description() { return "Logger"; };

    void Attach(const std::string& _location, bool multipleInstance = false);
    void Detach(void);

    void SetInfo(const char* _pFileStr, const uint32_t _line,
                 const char* _pFunc);

    void Debug(std::string_view _pFmt, ...);
    void Info(std::string_view _pFmt, ...);
    void Warning(std::string_view _pFmt, ...);
    void Error(std::string_view _pFmt, ...);
    void Fatal(std::string_view _pFmt, ...);

    //    Provides singleton access.
    /* __attribute__((no_instrument_function)) */static CLog*
    Instance(const char* /*_pFileStr*/, const uint32_t /*_line*/,
             const char* /*_pFunc*/)
    {
        static CLog log;

        if (log.SingletonActive() == false)
            printf("Trying to access shutdown singleton %s\n",
                   log.Description());

        //    Annoying, smartpointer lock&unlock the mutex, then return to
        // someone who
        // will do the same... log.SetInfo( _pFileStr, _line, _pFunc );
        return (&log);
    }
};

}; // namespace Base

#define g_Log ::Base::CLog::Instance(__FILE__, __LINE__, __FUNCTION__)

#endif
