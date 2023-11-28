#include <stdio.h>
#include <boost/json.hpp>
#include <boost/json/src.hpp>

#include "EDreamClient.h"
#include "Networking.h"
#include "clientversion.h"
#include "ContentDownloader.h"
#include "Shepherd.h"
#include "Log.h"
#include "Settings.h"
#include "client.h"


static ShowPreferencesCallback_t gShowPreferencesCallback = nullptr;
void ESSetShowPreferencesCallback(ShowPreferencesCallback_t _callback)
{
    gShowPreferencesCallback = _callback;
}
void ESShowPreferences()
{
    if (gShowPreferencesCallback != nullptr)
    {
        gShowPreferencesCallback();
    }
}
boost::atomic<char*> EDreamClient::fAccessToken(NULL);
boost::atomic<char*> EDreamClient::fRefreshToken(NULL);
boost::atomic<bool> EDreamClient::fIsLoggedIn(false);
boost::mutex EDreamClient::fAuthMutex;

static void SetNewAndDeleteOldString(boost::atomic<char*>& str, char* newval, boost::memory_order mem_ord = boost::memory_order_relaxed)
{
    char* toDelete = str.exchange(newval, mem_ord);
    
    if (toDelete != NULL)
        delete[] toDelete;
}

static void SetNewAndDeleteOldString(boost::atomic<char*>& str, const char* newval, boost::memory_order mem_ord = boost::memory_order_relaxed)
{
    if (newval == NULL)
    {
        SetNewAndDeleteOldString(str, (char*)NULL, mem_ord);
        return;
    }
    size_t len = strlen(newval) + 1;
    char *newStr = new char[len];
    memcpy(newStr, newval, len);
    SetNewAndDeleteOldString(str, newStr);
}

void EDreamClient::InitializeClient()
{
    //g_Settings()->Set("settings.content.access_token", std::string(""));
    SetNewAndDeleteOldString(fAccessToken, g_Settings()->Get("settings.content.access_token", std::string("")).c_str());
    SetNewAndDeleteOldString(fRefreshToken, g_Settings()->Get("settings.content.refresh_token", std::string("")).c_str());
    fAuthMutex.lock();
    boost::thread t(&EDreamClient::Authenticate);
}

const char* EDreamClient::GetAccessToken()
{
    return fAccessToken.load(boost::memory_order_relaxed);
}

bool EDreamClient::Authenticate()
{
    char authHeader[ACCESS_TOKEN_MAX_LENGTH + 22];
    Network::spCFileDownloader spDownload = new Network::CFileDownloader("Authenticate");
    spDownload->AppendHeader("Content-Type: application/json");
    snprintf(authHeader, ACCESS_TOKEN_MAX_LENGTH, "Authorization: Bearer %s", GetAccessToken());
    
    spDownload->AppendHeader(authHeader);
    bool success = spDownload->Perform(USER_ENDPOINT);
    if (!success && spDownload->ResponseCode() == 401)
    {
        success = RefreshAccessToken();
    }
    fIsLoggedIn.exchange(success);
    fAuthMutex.unlock();
    return success;
}

bool EDreamClient::IsLoggedIn()
{
    boost::lock_guard<boost::mutex> lock(fAuthMutex);
    return fIsLoggedIn.load();
}

bool EDreamClient::RefreshAccessToken()
{
    char body[REFRESH_TOKEN_MAX_LENGTH + 17];
    snprintf(body, REFRESH_TOKEN_MAX_LENGTH, "{\"refreshToken\":\"%s\"}", fRefreshToken.load());
    Network::spCFileDownloader spDownload = new Network::CFileDownloader( "Refresh token" );
    spDownload->AppendHeader("Content-Type: application/json");
    spDownload->AppendHeader("Accept: application/json");
    spDownload->SetPostFields(body);
    if (spDownload->Perform(REFRESH_ENDPOINT))
    {
        boost::json::error_code ec;
        boost::json::value response = boost::json::parse(spDownload->Data(), ec);
        boost::json::value data = response.at("data");
        const char* accessToken = data.at("AccessToken").as_string().data();
        g_Settings()->Set( "settings.content.access_token", std::string(accessToken));
        SetNewAndDeleteOldString(fAccessToken, accessToken);
        return true;
    }
    else
    {
        if (spDownload->ResponseCode() == 400)
        {
            g_Settings()->Set("settings.content.access_token", std::string(""));
            ESShowPreferences();
            return false;
        }
        g_Log->Error("Refreshing access token failed. Server returned %i: %s", spDownload->ResponseCode(), spDownload->Data().c_str());
        return false;
    }
}

bool EDreamClient::GetDreams()
{
    Network::spCFileDownloader spDownload;
    const char *xmlPath = ContentDownloader::Shepherd::xmlPath();
    char filename[MAX_PATH];

    char authHeader[ACCESS_TOKEN_MAX_LENGTH + 22];
    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        spDownload = new Network::CFileDownloader( "Dreams list" );
        spDownload->AppendHeader("Content-Type: application/json");
        snprintf(authHeader, ACCESS_TOKEN_MAX_LENGTH, "Authorization: Bearer %s", GetAccessToken());
        spDownload->AppendHeader(authHeader);
        if (spDownload->Perform(DREAM_ENDPOINT))
        {
            break;
        }
        else
        {
            if (spDownload->ResponseCode() == 400 ||
                spDownload->ResponseCode() == 401)
            {
                if (currentAttempt == maxAttempts)
                    return false;
                if (!RefreshAccessToken())
                    return false;
            }
            else
            {
                g_Log->Error("Failed to get dreams. Server returned %i: %s", spDownload->ResponseCode(), spDownload->Data().c_str());
            }
        }
    }

    snprintf( filename, MAX_PATH, "%sdreams.json", xmlPath );
    if( !spDownload->Save( filename ) )
    {
        g_Log->Error( "Unable to save %s\n", filename );
        return false;
    }
    return true;
}
