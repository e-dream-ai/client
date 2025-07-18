#ifndef _NETWORKING_H_
#define _NETWORKING_H_

#include <map>
#include <vector>

#ifdef WIN32
#pragma comment(lib, "wldap32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <boost/thread.hpp>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>

#include "Singleton.h"
#include "SmartPtr.h"
#include "base.h"

#ifdef LINUX_GNU
#undef Status
#endif

namespace Network
{

/*
        CCurlTransfer.
        Baseclass for curl handled transfers.
*/
class CCurlTransfer
{
    friend class CManager;

    //	Identifier;
    std::string m_Name;
    std::string m_Status;
    std::string m_AverageSpeed;
    long m_HttpCode;
    char errorBuffer[CURL_ERROR_SIZE];

    //	Map of respons codes allowed. This is checkd on failed perform's.
    std::vector<uint32_t> m_AllowedResponses;

  protected:
    CURL* m_pCurl;
    CURLM* m_pCurlM;
    struct curl_slist* m_Headers = NULL;

    bool Verify(CURLcode _code);
    bool VerifyM(CURLMcode _code);
    void Status(const std::string& _status) { m_Status = _status; };

  public:
    CCurlTransfer(const std::string& _name);
    virtual ~CCurlTransfer();

    static int32_t customProgressCallback(void* _pUserData, double _downTotal,
                                          double _downNow, double _upTotal,
                                          double _upNow);

    virtual void AppendHeader(const std::string& header);

    virtual bool InterruptiblePerform();

    virtual bool Perform(const std::string& _url);

    //	Add a response code to the list of allowed ones.
    void Allow(const uint32_t _code) { m_AllowedResponses.push_back(_code); }

    const std::string& Name() const { return m_Name; };
    const std::string& Status() const { return m_Status; };
    long ResponseCode() const { return m_HttpCode; };
    const std::string SpeedString() const { return m_AverageSpeed; };
};

//
class CFileDownloader : public CCurlTransfer
{
    std::string m_Data;
    std::multimap<std::string, std::string> m_ResponseHeaders;
    
  public:
    static int32_t customWrite(void* _pBuffer, size_t _size, size_t _nmemb,
                               void* _pUserData);

    CFileDownloader(const std::string& _name);
    virtual ~CFileDownloader();

    bool SetPostFields(const char* postFields);
    virtual bool Perform(const std::string& _url);
    bool Save(const std::string& _output);

    const std::string& Data() { return m_Data; };
    
    std::string GetResponseHeader(const std::string& headerName) const;
    std::vector<std::string> GetResponseHeaders(const std::string& headerName) const;
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata);
};

//
class CFileDownloader_TimeCondition : public CFileDownloader
{
  public:
    CFileDownloader_TimeCondition(const std::string& _name);
    virtual ~CFileDownloader_TimeCondition();

    bool PerformDownloadWithTC(const std::string& _url, const time_t _lastTime);
};

//
class CFileUploader : public CCurlTransfer
{
  public:
    CFileUploader(const std::string& _name);
    virtual ~CFileUploader();

    bool PerformUpload(const std::string& _url, const std::string& _file,
                       const uint32_t _filesize);
};

//	Def some smart pointers for these.
MakeSmartPointers(CCurlTransfer);
MakeSmartPointers(CFileDownloader);
MakeSmartPointers(CFileDownloader_TimeCondition);
MakeSmartPointers(CFileUploader);

/*
        CManager().
        The main manager.
*/
MakeSmartPointers(CManager);
class CManager : public Base::CSingleton<CManager>
{
    friend class Base::CSingleton<CManager>;

    std::mutex m_Lock;

    //	Private constructor accessible only to CSingleton.
    CManager();

    //	No copy constructor or assignment operator.
    NO_CLASS_STANDARDS(CManager);

    //	To keep track of progress.
    std::map<std::string, std::string> m_ProgressMap;

    //	Proxy url and corresponding user/pass.
    std::string m_ProxyUrl, m_ProxyUserPass;

    bool m_Aborted;

  public:
    bool Startup();
    bool Shutdown();
    virtual ~CManager() { m_bSingletonActive = false; };

    const char* Description() { return "Network manager"; };

    //	Called by CCurlTransfer destructors.
    void Remove(CCurlTransfer* _pTransfer);

    //	Session wide proxy settings & user/pass.
    void Proxy(const std::string& _url, const std::string& _userName,
               const std::string& _password);

    //	Called by CCurlTransfer prior to each Perform() call to handle proxy &
    // authentication.
    CURLcode Prepare(CURL* _pCurl);

    //	Used by the transfers to update progress.
    void UpdateProgress(CCurlTransfer* _pTransfer,
                        const double _percentComplete,
                        const double _bytesTransferred);

    // Used to abort any curl transfer

    void Abort(void);
    bool IsAborted(void);

    //	Fills in a vector of status strings for all active transfers.
    std::string Status();

    //	Urlencode string.
    static std::string Encode(const std::string& _src);

    //	Threadsafe.
    static CManager* Instance(const char* /*_pFileStr*/,
                              const uint32_t /*_line*/, const char* /*_pFunc*/)
    {
        // printf( "g_NetworkManager( %s(%d): %s )\n", _pFileStr, _line, _pFunc
        // ); fflush( stdout );

        static CManager networkManager;

        if (networkManager.SingletonActive() == false)
        {
            printf("Trying to access shutdown singleton %s\n",
                   networkManager.Description());
        }

        return (&networkManager);
    }
};

}; // namespace Network

//	Helper for less typing...
#define g_NetworkManager                                                       \
    Network::CManager::Instance(__FILE__, __LINE__, __FUNCTION__)

#endif
