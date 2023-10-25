#include <stdio.h>
#include <boost/json.hpp>

#include "EDreamClient.h"
#include "Networking.h"
#include "clientversion.h"
#include "ContentDownloader.h"
#include "Shepherd.h"
#include "Log.h"
#include "Settings.h"

boost::atomic<char *> EDreamClient::fAccessToken(NULL);
boost::atomic<char *> EDreamClient::fRefreshToken(NULL);

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
    Network::spCFileDownloader spDownload = new Network::CFileDownloader( "Sheep list" );
    spDownload->AppendHeader("Content-Type: application/json");
    SetNewAndDeleteOldString(fAccessToken, g_Settings()->Get( "settings.content.access_token", std::string("")).c_str());
    SetNewAndDeleteOldString(fRefreshToken, g_Settings()->Get( "settings.content.refresh_token", std::string("eyJjdHkiOiJKV1QiLCJlbmMiOiJBMjU2R0NNIiwiYWxnIjoiUlNBLU9BRVAifQ.DVL8mFNhCXrZVbB10RiJsQeGKRv3UOvsgcLIPb_LqANyN1zOFPFdD2Lk7v3hrVCNWicCkBE06KtzjhQ48u6BlumKfv-9VYTI4UMys-O1Z_gd-q7pH60wJKbSgwoRYuGB3hH75o-qmeTbYXBiQcE31sqwDii5b1LCNXOIW9Kh2sfvAv_K6ipgQ_IxcSK07ioBUTpBrULNmxc8X_wsvRWhbAye9_SKIJi3aChMpvBBhG3A7ZBu0n10tDq6MKqP-F-FKjmCmP6ZRkJcvRvPXGegKm7D9u-9Y0BkNWqIkyp9bSBUGQ3sGGZoJmKHSDWrXt3ILRMlsy0wWriBf-QCO0_yZA.QrWMsOyjeN-A0ILC.MufsVVOO3lEwg_4ubRcxYRe_9eYjVbhrPqsmK0jgiPBVrj-Gah4FZGrcG3UaDTmvmZZreGoTyF5ldTbQIBiamULtpJqKMlMghWUL-RH5wiAdzMFbkOAlnykHs2qwAi112WxFwF5sMM-bPvjabnkqYKvnkbtF34Yp5vK-olFPIfSkzwaGDJnNV-IoSghq6p8iIbw-1LVHdkbPIIHRFHK_cwRvefE7EEWhEB9cQarsvGXJBLnVBPvXaN17TetIBEPodNrrrSMwcf01ITPLz71icV5ay6MlaNFHvjM3CdTuWoSN5rWtObgpTvxbrU2iqFsOBtV8X0foErzwCb0sCYwF2qMqZ4rorZZ-bAF-tDZ6KUUMeUfm_O4-LlWiczUVQPA12_0MJj31yRDbzq76-schXoBua7uCX7eb7jry67kMnmDQwHGMYEUayrdpJSgYWJoY6jv-6KNrZCNIOGW5wLUOo4BefHYHkKh_NrVTaaM7Ys1B4cFKe940MkkAyfVfi1SC_UFlbTanKc3oQ2XrecNIeBS1YQ0TpDWmy_60W9TL8nhVZ_mO2hPMAWCIg1ShYmprCBoBeLD8OeDddzvED0sS2eVziYTvuZdRU1fcB3btX2ujKbj81NhWgIYC6Nekf_vYKOn03r6GHavwmrn2Zv9zphFkh1yp6uPK2Z1qs30Os8viI8SgPH4Y01jXQxgSHc6lcCPxBNwO54zDQjptzugESGDNmuPBEtnQF0HdOxZ8MSZGUBfv9MPrWuzl0GtkDJzpZ2iO6SyWGftVeDOFMoq-NmfYTLAo0u7JgIdRUVzI7VPkKL-6GZ5HH-zyoEtunOIPLClOAZ4xXi9SX4ZRrTiHz-9EWndPLfWG23Sp6ZPIHW6FbtBcexUeh6ddQQ7o5ol2NQLgMsOvert0VEd1CgNYBYMSqNHH2ElHQr2PfTMoJTV66btQ2OMokYk-gPMmncFxEV_eIfxDj-LHCB8HXiRFdC25dXajg6pnmQaylxZM_85KBxr3DxXxrs_d3XCPsCgypYF9NojXVsd48gWQlq0fnEQNkY30nzQ80NFM8u2G2pVwa9IV7aL_014jm0ddlaXq6uzMSe1d3bMxJFXs9seqGw9t24wXUzSc3Xr4X6qaj59dbqyDCU_Y-LHvNgJ-W2HMzUewtXTucwvQag6NPBQs5fY30jLLEZmU2Q1FDVf_fhZOaznFMKO9yY4wu2ZhcVYVXF52poxdqNRsB_gfmiB3dUkB-hJ6qRpCLUBLpa9G9CeEroPajBPpkR2G5XynV63UYDnYVlqMx74inL1Tng1Q7AY1Dia3W9aOcLZdac8tuYdGee1T-HGus6yFvg.v7aIeHfsuVX-tC2NlJsJFw")).c_str());
}

const char* EDreamClient::GetAccessToken()
{
    return fAccessToken.load(boost::memory_order_relaxed);
}

void EDreamClient::RefreshAccessToken()
{
    char body[REFRESH_TOKEN_MAX_LENGTH + 17];
    sprintf(body, "{\"refreshToken\":\"%s\"}", fRefreshToken.load());
    Network::spCFileDownloader spDownload = new Network::CFileDownloader( "Refresh token" );
    spDownload->AppendHeader("Content-Type: application/json");
    spDownload->AppendHeader("Accept: application/json");
    spDownload->SetPostFields(body);
    if (!spDownload->Perform( REFRESH_ENDPOINT ))
    {
        g_Log->Error("Refreshing access token failed. Server returned %i: %s", spDownload->ResponseCode(), spDownload->Data().c_str());
        return;
    }
    boost::json::error_code ec;
    boost::json::value response = boost::json::parse(spDownload->Data(), ec);
    boost::json::value data = response.at("data");
    const char* accessToken = data.at("AccessToken").as_string().data();
    g_Settings()->Set( "settings.content.access_token", std::string(accessToken));
    SetNewAndDeleteOldString(fAccessToken, accessToken);
}

bool EDreamClient::GetDreams()
{
    Network::spCFileDownloader spDownload = new Network::CFileDownloader( "Dreams list" );
    spDownload->AppendHeader("Content-Type: application/json");
    
    const char *xmlPath = ContentDownloader::Shepherd::xmlPath();
    char filename[ MAX_PATH ];

    char authHeader[ACCESS_TOKEN_MAX_LENGTH + 22];
    int maxAttempts = 3;
    int currentAttempt = 0;
    while (currentAttempt++ < maxAttempts)
    {
        sprintf(authHeader, "Authorization: Bearer %s", GetAccessToken());
        spDownload->AppendHeader(authHeader);
        if( !spDownload->Perform( DREAM_ENDPOINT ) )
        {
            if (spDownload->ResponseCode() == 400)
            {
                RefreshAccessToken();
            }
            else
            {
                g_Log->Error("Failed to get dreams. Server returned %i: %s", spDownload->ResponseCode(), spDownload->Data().c_str());
                return false;
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
