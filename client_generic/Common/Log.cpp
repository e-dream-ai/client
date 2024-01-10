#include <inttypes.h>
#include <os/signpost.h>
#include <sstream>
#include <cstdarg>
#include <string>
#include <time.h>

#include "Log.h"
#include "base.h"

namespace Base
{

/*
        CLog().

*/
CLog::CLog() : m_bActive(false), m_pFile(NULL) {}

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
    // if( (
    // m_pStdout = freopen( "debug.txt", "a+", stdout );
    //) == NULL )
    // exit(-1);
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

    // if( m_pStdout )
    // fclose( m_pStdout );

    return (true);
}

/*
 */
void CLog::Attach(const std::string& _location, const uint32_t /*_level*/)
{
    time_t curTime;
    time(&curTime);

    char timeStamp[32] = {0};
    strftime(timeStamp, sizeof(timeStamp), "%Y_%m_%d", localtime(&curTime));

    std::stringstream s;
    s << _location << timeStamp << ".log";
    std::string f = s.str();
    if ((m_pFile = freopen(f.c_str(), "a+", stdout)) == NULL)
    {
#ifdef WIN32
        MessageBoxA(NULL,
                    "Unable to create log file, exiting. Please check file "
                    "permissions for electricsheep folder.",
                    "Log error", MB_OK);
#endif
        exit(-1);
    }

    m_bActive = true;
}

/*
 */
void CLog::Detach(void)
{
    m_bActive = false;
    fflush(m_pFile);
    if (m_pFile)
        fclose(m_pFile);
}

/*
        SetFile().
        Store file that did the logging.
*/
void CLog::SetInfo(const char* _pFileStr, const uint32_t _line,
                   const char* _pFunc)
{
    // boost::mutex::scoped_lock locker( m_Lock );

    std::string tmp = _pFileStr;
    size_t offs = tmp.find_last_of("/\\", tmp.size());

    m_File = tmp.substr(offs + 1, tmp.size());
    m_Function = std::string(_pFunc);
    m_Line = _line;
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
    boost::mutex::scoped_lock locker(m_Lock);

    // if (m_pFile == NULL)
    // return;

    time_t curTime;
    time(&curTime);

    char timeStamp[32] = {0};
    strftime(timeStamp, sizeof(timeStamp), "%H:%M:%S", localtime(&curTime));

    /* log spam start*/
    if (strcmp(_pStr, s_MessageSpam) == 0)
    {
        ++s_MessageSpamCount;
    }
    else
    {
        if (s_MessageSpamCount > 0)
        {
            fprintf(stdout, "[%s-%s]: '%s' x%lu\n", s_MessageType, timeStamp,
                    s_MessageSpam, s_MessageSpamCount);
            fflush(stdout);
            if (m_bActive)
            {
                fprintf(m_pFile, "[%s-%s]: '%s' x%lu\n", s_MessageType,
                        timeStamp, s_MessageSpam, s_MessageSpamCount);
                fflush(m_pFile);
            }
        }
        else
        {
            //	Not active/attached, dump to stdout.
            // fprintf( stdout, "[%s]: %s - %s[%s(%d)]: '%s'\n", _pType,
            // timeStamp, _file, _pFunc, m_Line, _pStr );
            fprintf(stdout, "[%s-%s]: '%s'\n", s_MessageType, timeStamp,
                    s_MessageSpam);
            fflush(stdout);
            if (m_bActive)
            {
                // fprintf( m_pFile, "[%s]: %s - %s[%s(%d)]: '%s'\n", _pType,
                // timeStamp, _file, _pFunc, m_Line, _pStr );
                fprintf(m_pFile, "[%s-%s]: '%s'\n", s_MessageType, timeStamp,
                        s_MessageSpam);
                fflush(m_pFile);
            }
        }

        s_MessageSpamCount = 0;
        memcpy(s_MessageSpam, _pStr, m_MaxMessageLength);
        strcpy(s_MessageType, _pType);
    }
    /* log spam end */
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

os_log_t g_SignpostHandle = os_log_create("com.spotworks.e-dream.app",
                                          OS_LOG_CATEGORY_POINTS_OF_INTEREST);
