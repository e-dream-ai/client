
#include <inttypes.h>

#ifdef MAC
#include <os/signpost.h>
#endif 

#ifdef WIN32
#include <io.h> // Import read/write in msvc
#endif;

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

#include "Log.h"
#include "base.h"
#include "PlatformUtils.h"

#define BUFFER_SIZE 1024 * 128

namespace Base
{

/*
        CLog().

*/
CLog::CLog()
    : m_bActive(false), m_pFile(NULL), m_PipeReader(0),
      m_pPipeReaderThread(nullptr)
{
}

/*
        ~CLog().

*/
CLog::~CLog()
{
    //	Mark singleton as shutdown, to track unwanted access after this point.
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

void CLog::Attach(const std::string& _location, const uint32_t /*_level*/)
{
    // Create filename with timestamp
    time_t curTime;
    time(&curTime);

    char timeStamp[32] = {0};
    strftime(timeStamp, sizeof(timeStamp), "%Y_%m_%d", localtime(&curTime));

    std::stringstream s;
    s << _location << timeStamp << ".log";
    std::string f = s.str();

    m_Sink = boost::log::add_file_log(f, boost::log::keywords::format =
                                             "[%TimeStamp% - %Severity%]: %Message%");

    boost::log::core::get()->set_filter(boost::log::trivial::severity >=
                                        boost::log::trivial::info);

    boost::log::sources::severity_logger<boost::log::trivial::severity_level>
        lg;

    boost::log::add_common_attributes();

    BOOST_LOG_TRIVIAL(info) << "Attaching Log";
    m_Sink->flush();
    //    if (m_bActive)
//        return;
//
//    time_t curTime;
//    time(&curTime);
//
//    char timeStamp[32] = {0};
//    strftime(timeStamp, sizeof(timeStamp), "%Y_%m_%d", localtime(&curTime));
//
//    // Create a pipe
//    int pipefd[2];
//    #if WIN32
//    if (_pipe(pipefd, 100, 0) == -1)
//    #else
//    if (pipe(pipefd) == -1)
//    #endif
//    {
//        perror("Unable to create pipe.");
//        exit(EXIT_FAILURE);
//    }
//
//    // Save the original stdout file descriptor
//    m_OriginalSTDOUT = dup(fileno(stdout));
//    if (m_OriginalSTDOUT == -1)
//    {
//        perror("dup failed");
//        exit(EXIT_FAILURE);
//    }
//
//    // Redirect stdout to the write end of the pipe
//    if (dup2(pipefd[1], fileno(stdout)) == -1)
//    {
//        perror("dup2 failed");
//        exit(EXIT_FAILURE);
//    }
//
//    // Close the write end of the pipe
//    close(pipefd[1]);
//    m_PipeReader = pipefd[0];
//
//    std::stringstream s;
//    s << _location << timeStamp << ".log";
//    std::string f = s.str();
//    if ((m_pFile = fopen(f.c_str(), "a+")) == NULL)
//    {
//#ifdef WIN32
//        MessageBoxA(NULL,
//                    "Unable to create log file, exiting. Please check file "
//                    "permissions for electricsheep folder.",
//                    "Log error", MB_OK);
//#endif
//        exit(-1);
//    }
//    m_pPipeReaderThread =
//        new std::thread(std::bind(&CLog::PipeReaderThread, this));
    m_bActive = true;

}

/*
 */
void CLog::Detach(void)
{

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
    //        //	Not active/attached, dump to stdout.
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
    va_start(ArgPtr, _pFmt);                                                   \
    vsnprintf(pTempStr, m_MaxMessageLength, _pFmt.data(), ArgPtr);             \
    va_end(ArgPtr);

//	Def our loggers.
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