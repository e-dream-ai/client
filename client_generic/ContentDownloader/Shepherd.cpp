///////////////////////////////////////////////////////////////////////////////
//
//    electricsheep for windows - collaborative screensaver
//    Copyright 2003 Nicholas Long <nlong@cox.net>
//    electricsheep for windows is based of software
//    written by Scott Draves <source@electricsheep.org>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
///////////////////////////////////////////////////////////////////////////////
#include <string>
#include <sys/stat.h>
#include <time.h>
#include <vector>
#include <map>
#include <zlib.h>
#if defined(WIN32) && defined(_MSC_VER)
#include "../msvc/msvc_fix.h"
#else
#include <dirent.h>
#endif
#include <boost/filesystem.hpp>

#include "EDreamClient.h"
#include "Log.h"
#include "Settings.h"
#include "SheepDownloader.h"
#include "Shepherd.h"
#include "base.h"
#include "clientversion.h"
#include "md5.h"

namespace ContentDownloader
{

// declare namespace we are using
//
using namespace std;

#define MAXBUF 1024
#define MIN_READ_INTERVAL 600

// Initialize static class data
//
uint64_t Shepherd::s_ClientFlockBytes = 0;
uint64_t Shepherd::s_ClientFlockGoldBytes = 0;
uint64_t Shepherd::s_ClientFlockCount = 0;
uint64_t Shepherd::s_ClientFlockGoldCount = 0;
atomic_char_ptr Shepherd::fRootPath(NULL);
atomic_char_ptr Shepherd::fMp4Path(NULL);
atomic_char_ptr Shepherd::fJsonPath(NULL);
atomic_char_ptr Shepherd::fServerName(NULL);
atomic_char_ptr Shepherd::fVoteServerName(NULL);
atomic_char_ptr Shepherd::fRenderServerName(NULL);
atomic_char_ptr Shepherd::fProxy(NULL);
atomic_char_ptr Shepherd::fProxyUser(NULL);
atomic_char_ptr Shepherd::fProxyPass(NULL);
atomic_char_ptr Shepherd::fNickName(NULL);
int Shepherd::fUseProxy = 0;
int Shepherd::fSaveFrames = 0;
int Shepherd::fCacheSize = 100;
int Shepherd::fCacheSizeGold = 100;
int Shepherd::fRegistered = 0;
atomic_char_ptr Shepherd::fPassword(NULL);
atomic_char_ptr Shepherd::fUniqueID(NULL);
boost::detail::atomic_count* Shepherd::renderingFrames = NULL;
boost::detail::atomic_count* Shepherd::totalRenderedFrames = NULL;
bool Shepherd::m_RenderingAllowed = true;

std::queue<spCMessageBody> Shepherd::m_MessageQueue;
boost::mutex Shepherd::m_MessageQueueMutex;

std::vector<spCTimedMessageBody> Shepherd::m_OverflowMessageQueue;
boost::mutex Shepherd::s_OverflowMessageQueueMutex;

boost::mutex Shepherd::s_ShepherdMutex;

boost::shared_mutex Shepherd::s_DownloadStateMutex;
boost::shared_mutex Shepherd::s_RenderStateMutex;

boost::shared_mutex Shepherd::s_GetServerNameMutex;

boost::mutex Shepherd::s_ComputeServerNameMutex;

bool Shepherd::fShutdown = false;
int Shepherd::fChangeRes = 0;
int Shepherd::fChangingRes = 0;

time_t Shepherd::s_LastRequestTime = 0;

Base::CBlockingQueue<char*> Shepherd::fStringsToDelete;

std::string Shepherd::s_DownloadState;
std::string Shepherd::s_RenderState;

bool Shepherd::s_IsDownloadStateNew = false;
bool Shepherd::s_IsRenderStateNew = false;

#define _24_HOURS (60 * 60 * 24)

Shepherd::~Shepherd()
{
    g_Log->Info("~Shepherd()...");

    while (m_MessageQueue.size() != 0)
        m_MessageQueue.pop();
}

void Shepherd::initializeShepherd(/*HINSTANCE hInst, HWND hWnd*/)
//
// Description:
//		Initialize global data for the shepherd and his heard.
//
{
    SheepDownloader::initializeDownloader();
    EDreamClient::InitializeClient();

    totalRenderedFrames =
        new boost::detail::atomic_count((int)(int32_t)g_Settings()->Get(
            "settings.generator.totalFramesRendered", 0));
    renderingFrames = new boost::detail::atomic_count(0);
}

/*
        notifyShepherdOfHisUntimleyDeath().
        This method gets called by the main window procedure before the app is
   about to close. This allows the shepherd to clean any data that it allocated.
*/
void Shepherd::notifyShepherdOfHisUntimleyDeath()
{
    g_Log->Info("notifyShepherdOfHisUntimleyDeath...");

    SheepDownloader::closeDownloader();

    // boost::mutex::scoped_lock lockthis( s_ShepherdMutex );

    SAFE_DELETE_ARRAY(fRootPath);
    SAFE_DELETE_ARRAY(fMp4Path);
    SAFE_DELETE_ARRAY(fJsonPath);
    SAFE_DELETE_ARRAY(fServerName);
    SAFE_DELETE_ARRAY(fVoteServerName);
    SAFE_DELETE_ARRAY(fRenderServerName);
    SAFE_DELETE_ARRAY(fPassword);
    SAFE_DELETE_ARRAY(fProxy);
    SAFE_DELETE_ARRAY(fProxyUser);
    SAFE_DELETE_ARRAY(fProxyPass);
    SAFE_DELETE_ARRAY(fUniqueID);

    SAFE_DELETE(totalRenderedFrames);
    SAFE_DELETE(renderingFrames);

    char* str;

    while (fStringsToDelete.pop(str))
    {
        SAFE_DELETE_ARRAY(str);
    }

    fStringsToDelete.clear(0);
}

void Shepherd::setNewAndDeleteOldString(atomic_char_ptr& str, char* newval,
                                        boost::memory_order mem_ord)
{
    char* toDelete = str.exchange(newval, mem_ord);

    if (toDelete != NULL)
        fStringsToDelete.push(toDelete);
}

static void MakeCopyAndSetAtomicCharPtr(
    atomic_char_ptr& target, const char* newVal,
    boost::memory_order mem_ord = boost::memory_order_relaxed)
{
    size_t len = strlen(newVal);

    char* newCopy = new char[len + 1];
    strcpy(newCopy, newVal);

    Shepherd::setNewAndDeleteOldString(target, newCopy, mem_ord);
}

void Shepherd::setRootPath(const char* path)
{
    // strip off trailing white space
    //
    size_t len = strlen(path);
    char* runner = const_cast<char*>(path) + len - 1;
    while (*runner == ' ')
    {
        len--;
        runner--;
    }

    // we need space for trailing \ character
    char* newRootPath = new char[len + 2];

    // copy the data
    memcpy(newRootPath, path, len);

    // check for the trailing \ character
    if (*runner != PATH_SEPARATOR_C)
    {
        newRootPath[len] = PATH_SEPARATOR_C;
        newRootPath[len + 1] = '\0';
    }
    else
        newRootPath[len] = '\0';

        // create the directory for the path
#ifdef WIN32
    CreateDirectoryA(newRootPath, NULL);
#else
    mkdir(newRootPath, 0755);
#endif

    setNewAndDeleteOldString(fRootPath, newRootPath);

    char* newMpegPath = new char[(len + 12)];

    snprintf(newMpegPath, len + 12, "%smp4%c", newRootPath, PATH_SEPARATOR_C);
#ifdef WIN32
    CreateDirectoryA(newMpegPath, NULL);
#else
    mkdir(newMpegPath, 0755);
#endif

    setNewAndDeleteOldString(fMp4Path, newMpegPath);

    char* newJsonPath = new char[(len + 12)];
    snprintf(newJsonPath, len + 12, "%sjson%c", newRootPath, PATH_SEPARATOR_C);
#ifdef WIN32
    CreateDirectoryA(newJsonPath, NULL);
#else
    mkdir(newJsonPath, 0755);
#endif

    setNewAndDeleteOldString(fJsonPath, newJsonPath);
}

void Shepherd::setProxy(const char* proxy)
//
// Description:
//		Sets the proxy server address for tcp|ip
//	transactions.
//
{
    MakeCopyAndSetAtomicCharPtr(fProxy, proxy);
}

void Shepherd::setProxyUserName(const char* userName)
{
    MakeCopyAndSetAtomicCharPtr(fProxyUser, userName);
}

void Shepherd::setProxyPassword(const char* password)
{
    MakeCopyAndSetAtomicCharPtr(fProxyPass, password);
}

void Shepherd::SetNickName(const char* nick)
{
    MakeCopyAndSetAtomicCharPtr(fNickName, nick);
}

const char* Shepherd::GetNickName()
{
    return fNickName.load(boost::memory_order_relaxed);
}

void Shepherd::addMessageText(std::string_view s, time_t timeout)
{
    QueueMessage(s, (double)timeout);
}

const char* Shepherd::rootPath()
{
    return fRootPath.load(boost::memory_order_relaxed);
}

const char* Shepherd::mp4Path()
{
    return fMp4Path.load(boost::memory_order_relaxed);
}

const char* Shepherd::jsonPath()
{
    return fJsonPath.load(boost::memory_order_relaxed);
}

const char* Shepherd::proxy()
{
    return fProxy.load(boost::memory_order_relaxed);
}

const char* Shepherd::proxyUserName()
{
    return fProxyUser.load(boost::memory_order_relaxed);
}

/*
 */
const char* Shepherd::proxyPassword()
{
    return fProxyPass.load(boost::memory_order_relaxed);
}

/*
 */
void Shepherd::setPassword(const char* password)
{
    size_t len = strlen(password);

    char* newPassword = new char[len + 1];
    strcpy(newPassword, password);

    setNewAndDeleteOldString(fPassword, newPassword);
}

/*
 */
const char* Shepherd::password()
{
    return fPassword.load(boost::memory_order_relaxed);
}

/*
        Get flock size in MBs after recounting
*/

uint64_t Shepherd::GetFlockSizeMBsRecount(const int generationtype)
{
    if (generationtype == 0)
    {
        ContentDownloader::SheepArray tempSheepArray;
        ContentDownloader::Shepherd::getClientFlock(&tempSheepArray);
        for (unsigned i = 0; i < tempSheepArray.size(); ++i)
            delete tempSheepArray[i];
    }
    if (generationtype == 1)
        return ContentDownloader::Shepherd::getClientFlockMBs(1);

    return ContentDownloader::Shepherd::getClientFlockMBs(0);
}

/*
        getClientFlock().
        This method fills the given sheeparray with all of the sheep on the
   client.
*/
bool Shepherd::getClientFlock(SheepArray* sheep)
{
    uint64_t clientFlockBytes = 0;
    uint64_t clientFlockCount = 0;

    uint64_t clientFlockGoldBytes = 0;
    uint64_t clientFlockGoldCount = 0;
    const SheepArray& serverFlock = SheepDownloader::getServerFlock();

    SheepArray::iterator iter;
    for (iter = sheep->begin(); iter != sheep->end(); ++iter)
        delete *iter;

    sheep->clear();

    //	Get the sheep in fMp4Path.
    getSheep(mp4Path(), sheep, serverFlock);
    for (iter = sheep->begin(); iter != sheep->end(); ++iter)
    {
        clientFlockBytes += (*iter)->fileSize;
        ++clientFlockCount;
    }

    s_ClientFlockBytes = clientFlockBytes;
    s_ClientFlockCount = clientFlockCount;

    s_ClientFlockGoldBytes = clientFlockGoldBytes;
    s_ClientFlockGoldCount = clientFlockGoldCount;

    return true;
}

using namespace boost::filesystem;

/*
        getSheep().
        Recursively loops through all files in the path looking for sheep.
*/
bool Shepherd::getSheep(const char* path, SheepArray* sheep,
                        const SheepArray& serverFlock)
{
    if (path == nullptr)
        return false;

    bool gotSheep = false;
    char fbuf[MAXBUF];

    try
    {
        boost::filesystem::path p(path);

        directory_iterator end_itr; // default construction yields past-the-end
        for (directory_iterator itr(p); itr != end_itr; ++itr)
        {
            uint32_t id = 0;
            uint32_t first = 0;
            uint32_t last = 0;
            uint32_t generation = 0;
            bool isTemp = false;
            bool isDeleted = false;

            if (is_directory(itr->status()))
            {
                bool gotSheepSubfolder = getSheep(
                    (char*)(itr->path().string() + std::string("/")).c_str(),
                    sheep, serverFlock);
                gotSheep |= gotSheepSubfolder;

                if (!gotSheepSubfolder)
                    remove_all(itr->path());
            }
            else
            {
                auto fileName = itr->path().filename();
                if (fileName.extension() == ".mp4")
                {
                    std::string uuid = fileName.stem().string();
                    sDreamMetadata* serverSheep = nullptr;
                    if (serverFlock.tryGetSheepWithUuid(uuid, serverSheep))
                    {
                        sDreamMetadata* newSheep =
                            new sDreamMetadata(*serverSheep);
                        newSheep->fileName = itr->path().string();
                        newSheep->fileSize =
                            boost::filesystem::file_size(itr->path());
                        sheep->push_back(newSheep);
                        gotSheep = true;
                    }
                }
            }
        }
    }
    catch (boost::filesystem::filesystem_error& err)
    {
        g_Log->Error("Path enumeration threw error: %s", err.what());
    }

    return gotSheep;
}

//	Sets the unique id for this Shepherd.
void Shepherd::setUniqueID(const char* uniqueID)
{
    size_t len;
    if (strlen(uniqueID) > 16)
        len = strlen(uniqueID);
    else
        len = 16;

    char* newUniqueID = new char[len + 1];
    strcpy(newUniqueID, uniqueID);

    setNewAndDeleteOldString(fUniqueID, newUniqueID);
}

//	This method returns the unique ID to use.
const char* Shepherd::uniqueID()
{
    return fUniqueID.load(boost::memory_order_relaxed);
}

void Shepherd::setDownloadState(const std::string& state)
{
    boost::upgrade_lock<boost::shared_mutex> lockthis(s_DownloadStateMutex);

    s_DownloadState.assign(state);

    s_IsDownloadStateNew = true;
}

std::string Shepherd::downloadState(bool& isnew)
{
    boost::shared_lock<boost::shared_mutex> lockthis(s_DownloadStateMutex);

    isnew = s_IsDownloadStateNew;

    s_IsDownloadStateNew = false;

    return s_DownloadState;
}

void Shepherd::setRenderState(const std::string& state)
{
    boost::upgrade_lock<boost::shared_mutex> lockthis(s_RenderStateMutex);

    s_RenderState.assign(state);

    s_IsRenderStateNew = true;
}

std::string Shepherd::renderState(bool& isnew)
{
    boost::shared_lock<boost::shared_mutex> lockthis(s_RenderStateMutex);

    isnew = s_IsRenderStateNew;

    s_IsRenderStateNew = false;

    return s_RenderState;
}

//
void Shepherd::FrameStarted()
{
    if (renderingFrames)
        ++(*renderingFrames);
}
int Shepherd::FramesRendering()
{
    return renderingFrames ? static_cast<int>(*renderingFrames) : 0;
}
int Shepherd::TotalFramesRendered()
{
    return totalRenderedFrames ? static_cast<int>(*totalRenderedFrames) : 0;
}
bool Shepherd::RenderingAllowed() { return m_RenderingAllowed; }
void Shepherd::SetRenderingAllowed(bool _yesno) { m_RenderingAllowed = _yesno; }

//
void Shepherd::FrameCompleted()
{
    if (renderingFrames)
        --(*renderingFrames);
    if (totalRenderedFrames)
        ++(*totalRenderedFrames);
    g_Settings()->Set("settings.generator.totalFramesRendered",
                      totalRenderedFrames ? (int32_t)*totalRenderedFrames : 0);
}

const char* Shepherd::GetDreamServer()
{
    static std::string dreamServer;
    if (dreamServer.empty())
    {
#ifdef DEBUG
        int server = g_Settings()->Get("settings.debug.server", 0);
        if (server == 0)
            dreamServer = DREAM_SERVER_STAGING;
        else
            dreamServer = DREAM_SERVER_PRODUCTION;
#else
        dreamServer = DREAM_SERVER_PRODUCTION;
#endif
    }
    return dreamServer.data();
}

const char* Shepherd::GetEndpoint(eServerEndpoint _endpoint)
{
    static std::map<eServerEndpoint, std::string> endpoints;
    if (!endpoints.size())
    {
        endpoints = {SERVER_ENDPOINT_DEFINITIONS};
    }
    return endpoints[_endpoint].data();
}

}; // namespace ContentDownloader
