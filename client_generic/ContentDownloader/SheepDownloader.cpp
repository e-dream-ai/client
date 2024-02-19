///////////////////////////////////////////////////////////////////////////////
//
//    electricsheep for windows - collaborative screensaver
//    Copyright 2003 Nicholas Long <nlong@cox.net>
//	  electricsheep for windows is based of software
//	  written by Scott Draves <source@electricsheep.org>
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
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <list>
#include <sstream>

#include <boost/format.hpp>
// #include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/json.hpp>

#include <math.h>
#include <time.h>
#include <zlib.h>

#include <sys/stat.h>
#include <sys/types.h>
#ifndef WIN32
#include <sys/mount.h>
#include <sys/param.h>
#endif
#ifdef LINUX_GNU
#include <limits.h>
#include <sys/statfs.h>
#define MAX_PATH PATH_MAX
#endif

#include "ContentDownloader.h"
#include "MathBase.h"
#include "Networking.h"
#include "JSONUtil.h"
#include "Player.h"
#include "SheepDownloader.h"
#include "base.h"
#include "clientversion.h"
#ifdef WIN32
#include "io.h"
#endif
#include "Log.h"
#include "PlayCounter.h"
#include "Settings.h"
#include "Shepherd.h"
#include "Timer.h"
#include "StringFormat.h"
#if defined(WIN32) && defined(_MSC_VER)
#include "../msvc/msvc_fix.h"
#endif
#include "EDreamClient.h"
#include "PlatformUtils.h"

namespace ContentDownloader
{

using namespace std;
using namespace boost;

#define MAXBUF 1024
#define TIMEOUT 600
static const int32_t MAX_TIMEOUT = 24 * 60 * 60; // 1 day
#define MIN_MEGABYTES 1024
#define MIN_READ_INTERVAL 3600
#define PRINT_TO_SCREEN 0

#ifndef DEBUG
static const uint32_t INIT_DELAY = 60;
#endif

// Initialize the class data
int SheepDownloader::fDownloadedSheep = 0;
bool SheepDownloader::fGotList = false;
bool SheepDownloader::fListDirty = true;
time_t SheepDownloader::fLastListTime = 0;
SheepArray SheepDownloader::fServerFlock;
SheepArray SheepDownloader::fClientFlock;

std::mutex SheepDownloader::s_DownloaderMutex;

/*
 */
SheepDownloader::SheepDownloader()
{
    fHasMessage = false;
    m_bAborted = false;
    fGotList = false;
    fListDirty = true;

    ParseServerDreams();
    updateCachedSheep();
    deleteCached(0, 0);
}

/*
 */
SheepDownloader::~SheepDownloader() { clearFlocks(); }

/*
 */
void SheepDownloader::initializeDownloader() {}

/*
 */
void SheepDownloader::closeDownloader() {}

/*
        numberOfDownloadedSheep().
        Returns the total number of downloaded sheep.
*/
int SheepDownloader::numberOfDownloadedSheep()
{
    std::scoped_lock lockthis(s_DownloaderMutex);

    int returnVal;
    returnVal = fDownloadedSheep;
    return returnVal;
}

/*
        clearFlocks().
        Clears the sheep arrays for the client and the server and deletes the
   memory they allocated.
*/
void SheepDownloader::clearFlocks()
{
    std::scoped_lock lockthis(s_DownloaderMutex);
    //	Clear the server flock.
    for (uint32_t i = 0; i < fServerFlock.size(); i++)
        SAFE_DELETE(fServerFlock[i]);

    fServerFlock.clear();

    fGotList = false;

    fListDirty = true;

    //	Clear the client flock.
    for (unsigned i = 0; i < fClientFlock.size(); i++)
        delete fClientFlock[i];

    fClientFlock.clear();
}

void SheepDownloader::Abort(void)
{
    std::scoped_lock lockthis(m_AbortMutex);

    m_bAborted = true;
}

//	Downloads the given sheep from the server and supplies a unique name for
// it based on it's ids.
bool SheepDownloader::downloadSheep(sDreamMetadata* sheep)
{
    if (sheep->flags & DREAM_FLAG_DOWNLOADED)
        return false;

    m_spSheepDownloader =
        std::make_shared<Network::CFileDownloader>(sheep->uuid);

    Network::spCFileDownloader spDownload = m_spSheepDownloader;

    bool dlded = spDownload->Perform(sheep->url);

    m_spSheepDownloader = NULL;

    {
        std::scoped_lock lockthis(m_AbortMutex);

        if (m_bAborted)
            return false;
    }

    if (!dlded)
    {
        g_Log->Warning("Failed to download %s.\n", sheep->url.data());

        if (spDownload->ResponseCode() == 401)
            g_ContentDownloader().ServerFallback();

        return false;
    }

    std::string fileName{
        string_format("%s%s.mp4", Shepherd::mp4Path(), sheep->uuid.data())};
    if (!spDownload->Save(fileName))
    {
        g_Log->Error("Unable to save %s\n", fileName.data());
        return false;
    }
    g_Player().Add(fileName);

    return true;
}

static void PrintMessage(std::string_view _msg)
{
    g_Log->Error(_msg);
#if PRINT_TO_SCREEN
    ContentDownloader::Shepherd::addMessageText(_msg, 180);
#endif
}

static void LogException(const std::exception& e, size_t dreamIndex,
                         std::string_view fileStr)
{
    auto str = string_format(
        "Exception during parsing dreams list:%s contents:\"%s\" dreamIndex:%d",
        e.what(), fileStr.data(), dreamIndex);
    PrintMessage(str);
}

void SheepDownloader::ParseServerDreams()
{
    std::scoped_lock lockthis(s_DownloaderMutex);
    SheepArray::iterator it = fServerFlock.begin();
    while (it != fServerFlock.end())
    {
        delete *it;
        it++;
    }
    fServerFlock.clear();
    int numDreams = ParseDreamsPage(0);
    int maxPage = (numDreams - 1) / EDreamClient::DREAMS_PER_PAGE;
    for (int i = 1; i <= maxPage; ++i)
    {
        ParseDreamsPage(i);
    }
}

static std::string TryParseString(uint32_t _id, const json::object& _obj,
                                  std::string_view _key)
{
    auto it = _obj.find(_key);
    auto val = it != _obj.end() ? &it->value() : nullptr;
    const char* kinds[] = {"null",   "bool",   "int64", "uint64",
                           "double", "string", "array", "object"};
    if (val && val->is_string())
        return (std::string)val->as_string();
    else if (val)
        PrintMessage(string_format(
            "Key \"%s\" inside dream %u was %s, but expected a string.",
            _key.data(), _id, kinds[(int)val->kind()]));
    else
        PrintMessage(string_format("Key \"%s\" inside dream %u was null.",
                                   _key.data(), _id));
    return "";
}

static const json::object*
TryParseObject(uint32_t _id, const json::object& _obj, std::string_view _key)
{
    auto it = _obj.find(_key);
    auto val = it != _obj.end() ? &it->value() : nullptr;
    const char* kinds[] = {"null",   "bool",   "int64", "uint64",
                           "double", "string", "array", "object"};
    if (val && val->is_object())
        return &val->as_object();
    else if (val)
        PrintMessage(string_format(
            "Key \"%s\" inside dream %u was %s, but expected an object.",
            _key.data(), _id, kinds[(int)val->kind()]));
    else
        PrintMessage(string_format("Key \"%s\" inside dream %u was null.",
                                   _key.data(), _id));
    return nullptr;
}

static float TryParseFloat(uint32_t _id, const json::object& _obj,
                           std::string_view _key)
{
    auto it = _obj.find(_key);
    auto val = it != _obj.end() ? &it->value() : nullptr;
    const char* kinds[] = {"null",   "bool",   "int64", "uint64",
                           "double", "string", "array", "object"};
    if (val && val->is_number())
        return val->to_number<float>();
    else if (val)
        PrintMessage(string_format(
            "Key \"%s\" inside dream %u was %s, but expected a float.",
            _key.data(), _id, kinds[(int)val->kind()]));
    else
        PrintMessage(string_format("Key \"%s\" inside dream %u was null.",
                                   _key.data(), _id));
    return 0.f;
}

int SheepDownloader::ParseDreamsPage(int _page)
{
    std::string filePath{
        string_format("%sdreams_%i.json", Shepherd::jsonPath(), _page)};
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        g_Log->Error("Error opening file: %s", filePath.data());
        return 0;
    }
    std::string contents{(std::istreambuf_iterator<char>(file)),
                         (std::istreambuf_iterator<char>())};
    file.close();
    size_t dreamIndex = 0;
    int numDreams = 0;
    try
    {
        boost::json::error_code ec;
        boost::json::value response = boost::json::parse(contents, ec);
        bool success = response.at("success").as_bool();
        if (!success)
        {
            g_Log->Error("Fetching dreams from API was unsuccessful: %s",
                         response.at("message").as_string().data());
            return 0;
        }
        boost::json::value data = response.at("data");
        boost::json::value dreams = data.at("dreams");
        boost::json::array dreamsArray = dreams.get_array();
        numDreams = (int)data.at("count").as_int64();
        size_t count = dreamsArray.size();

        if (count)
        {
            do
            {
                boost::json::object dream =
                    dreamsArray.at(dreamIndex).as_object();
                try
                {
                    boost::json::value video = dream.at("video");
                    if (video.is_null() || !video.is_string())
                        continue;

                    //    Create a new dream and parse the attributes.
                    sDreamMetadata* newDream = new sDreamMetadata();
                    newDream->id = (uint32_t)dream.at("id").as_int64();

                    newDream->url = video.as_string();
                    newDream->uuid =
                        TryParseString(newDream->id, dream, "uuid");
                    newDream->activityLevel =
                        TryParseFloat(newDream->id, dream, "activityLevel");
                    fServerFlock.push_back(newDream);
                    newDream->frontendUrl =
                        TryParseString(newDream->id, dream, "frontendUrl");
                    newDream->name =
                        TryParseString(newDream->id, dream, "name");
                    const boost::json::object* user =
                        TryParseObject(newDream->id, dream, "user");
                    if (!user)
                        continue;
                    newDream->author =
                        TryParseString(newDream->id, *user, "name");
                    newDream->setFileWriteTime(
                        TryParseString(newDream->id, dream, "updated_at"));
                    newDream->rating = atoi("5");
                }
                catch (const boost::system::system_error& e)
                {
                    std::string str = JSONUtil::PrintJSON(dream);
                    LogException(e, dreamIndex, str);
                }
            } while (++dreamIndex < dreamsArray.size());
        }
    }
    catch (const std::exception& e)
    {
        LogException(e, dreamIndex, contents);
    }
    fGotList = true;
    fListDirty = false;
    return numDreams;
}

/*
        updateCachedSheep().
        Run through the client cache to update it.
*/
void SheepDownloader::updateCachedSheep()
{
    std::scoped_lock lockthis(s_DownloaderMutex);

    //	Get the client flock.
    if (Shepherd::getClientFlock(&fClientFlock))
    {
        //	Run through the client flock to find deleted sheep.
        for (uint32_t i = 0; i < fClientFlock.size(); i++)
        {
            //	Get the current sheep.
            sDreamMetadata* currentSheep = fClientFlock[i];

            //	Check if it is deleted.
            if ((currentSheep->flags & DREAM_FLAG_DELETED) && fGotList)
            {
                //	If it is than run through the server flock to see if it
                // is still
                // there.
                uint32_t j;
                for (j = 0; j < fServerFlock.size(); j++)
                {
                    if (fServerFlock[j]->uuid == currentSheep->uuid)
                        break;
                }

                //	If it was not found on the server then it is time to
                // delete the
                // file.
                if (j == fServerFlock.size())
                {
                    g_Log->Info("Deleting %s", currentSheep->fileName.data());
                    if (remove(currentSheep->fileName.data()) != 0)
                        g_Log->Warning("Failed to remove %s",
                                       currentSheep->fileName.data());
                    else
                    {
                        Shepherd::subClientFlockBytes(currentSheep->fileSize,
                                                      0);
                        Shepherd::subClientFlockCount(0);
                    }
                    continue;
                }
                continue;
            }

            if (fGotList)
            {
                //	Update the sheep rating from the server.
                for (uint32_t j = 0; j < fServerFlock.size(); j++)
                {
                    sDreamMetadata* shp = fServerFlock[j];
                    if (shp->uuid == currentSheep->uuid)
                    {
                        currentSheep->rating = shp->rating;
                        break;
                    }
                }
            }

            //	Should use win native call but i don't know what it is.
            struct stat stat_buf;
            if (-1 != stat(currentSheep->fileName.data(), &stat_buf))
                currentSheep->writeTime = stat_buf.st_mtime;
        }

        //		fRenderer->updateClientFlock( fClientFlock );
    }
}

/*
        cacheOverflow().
        Checks if the cache will overflow if the given number of bytes are
   added.
*/
int SheepDownloader::cacheOverflow(const double& bytes,
                                   const int getGenerationType) const
{
    //	Return the overflow status
    return (
        Shepherd::cacheSize(getGenerationType) &&
        (bytes > (1024.0 * 1024.0 * Shepherd::cacheSize(getGenerationType))));
}

/*
        deleteCached().
        This function will make sure there is enough room in the cache for any
   newly downloaded files. If the cache is to large than the oldest and worst
   rated files will be deleted.
*/
void SheepDownloader::deleteCached(const uint64_t& size,
                                   const int getGenerationType)
{
    double total;
    time_t oldest_time; // oldest time for sheep with highest playcount
    int highest_playcount;
    uint32_t best;            // oldest sheep with highest playcount
    time_t oldest_sheep_time; // oldest time for sheep (from whole flock)
    uint32_t oldest;          // oldest sheep index

    //	updateCachedSheep();

    if (Shepherd::cacheSize(getGenerationType) != 0)
    {
        while (fClientFlock.size())
        {
            //	Initialize some data.
            total = size;
            oldest_time = 0;
            highest_playcount = 0;
            best = 0;
            oldest = 0;
            oldest_sheep_time = 0;

            //	Iterate the client flock to get the oldest and worst_rated file.
            for (uint32_t i = 0; i < fClientFlock.size(); i++)
            {
                sDreamMetadata* curSheep = fClientFlock[i];
                //	If the file is allready deleted than skip.
                if (curSheep->flags & DREAM_FLAG_DELETED)
                    continue;

                //	Store the file size.
                total += curSheep->fileSize;

                if (oldest_sheep_time == 0 ||
                    oldest_sheep_time > curSheep->writeTime)
                {
                    oldest = i;
                    oldest_sheep_time = curSheep->writeTime;
                }

                uint16_t curPlayCount =
                    g_PlayCounter().PlayCount(0, curSheep->id);

                if (oldest_time == 0 || (curPlayCount > highest_playcount) ||
                    ((curPlayCount == highest_playcount) &&
                     (curSheep->writeTime < oldest_time)))
                {
                    //	Update this as the file to delete if necessary.
                    best = i;
                    oldest_time = curSheep->writeTime;
                    highest_playcount =
                        g_PlayCounter().PlayCount(0, curSheep->id);
                }
            }

            //	If a file is found and the cache has overflowed then start
            // deleting.
            if (oldest_time && cacheOverflow(total, getGenerationType))
            {
                if (rand() % 2 == 0)
                {
                    best = oldest;
                    oldest_time = oldest_sheep_time;
                    g_Log->Info("Deleting oldest sheep");
                }
                else
                    g_Log->Info("Deleting most played sheep");

                std::string filename = fClientFlock[best]->fileName;
                if (filename.find_last_of("/\\") != filename.npos)
                    filename.erase(
                        filename.begin(),
                        filename.begin() +
                            static_cast<std::string::difference_type>(
                                filename.find_last_of("/\\")) +
                            1);

                uint16_t playcount =
                    g_PlayCounter().PlayCount(0, fClientFlock[best]->id) - 1;
                std::string temptime = ctime(&oldest_time);
                temptime.erase(temptime.size() - 1);

                std::string msg{string_format(
                    "Deleted: %s, played:%i time%s %s", filename.data(),
                    playcount, ((playcount == 1) ? "," : "s,"),
                    temptime.data())};
                Shepherd::AddOverflowMessage(msg);
                g_Log->Info("%s", msg.data());
                deleteSheep(fClientFlock[best]);
            }
            else
            {
                //	Cache is ok so return.
                return;
            }
        }
    }
}

/*
        deleteSheep().

*/
void SheepDownloader::deleteSheep(sDreamMetadata* sheep)
{
    if (remove(sheep->fileName.data()) != 0)
        g_Log->Warning("Failed to remove %s", sheep->fileName.data());
    else
    {
        Shepherd::subClientFlockBytes(sheep->fileSize, 0);
        Shepherd::subClientFlockCount(0);
    }
    sheep->flags |= DREAM_FLAG_DELETED;

    //	Create the filename with an xxx extension.
    size_t len = sheep->fileName.length();
    std::string deletedFile = sheep->fileName;

    deletedFile[len - 3] = 'x';
    deletedFile[len - 2] = 'x';
    deletedFile[len - 1] = 'x';

    //	Open the deleted file and save the zero length file.
    FILE* out = fopen(deletedFile.data(), "wb");
    if (out != nullptr)
        fclose(out);
}

/*
 */
void SheepDownloader::deleteSheepId(uint32_t sheepId)
{
    for (uint32_t i = 0; i < fClientFlock.size(); i++)
    {
        sDreamMetadata* curSheep = fClientFlock[i];
        if (curSheep->flags & DREAM_FLAG_DOWNLOADED)
            continue;

        if (curSheep->id == sheepId)
            deleteSheep(curSheep);
    }
}

bool SheepDownloader::isFolderAccessible(const char* folder)
{
    if (folder == NULL)
        return false;
#ifdef WIN32
    if (_access(folder, 6) != 0)
        return false;
#else
    if (access(folder, 6) != 0)
        return false;
#endif

    struct stat status;
    std::string tempstr(folder);
    if (tempstr.size() > 1 && (tempstr.at(tempstr.size() - 1) == '\\' ||
                               tempstr.at(tempstr.size() - 1) == '/'))
        tempstr.erase(tempstr.size() - 1);
    if (stat(tempstr.c_str(), &status) == -1)
        return false;

    if (!(status.st_mode & S_IFDIR))
        return false; // it's a file

    return true;
}

/*
        FindSheepToDownload().
        This method loads all of the sheep that are cached on disk and deletes
   any files in the cache that no longer exist on the server
*/
void SheepDownloader::FindSheepToDownload()
{
    PlatformUtils::SetThreadName("FindSheepToDownload");
    int best_rating;
    time_t best_ctime;
    int best_anim;
    int best_rating_old;
    int best_anim_old;
    time_t best_ctime_old;

    try
    {
#ifndef DEBUG

        // if there are at least three sheep to display in content folder,
        // sleep, otherwise start to download immediately
        if (fClientFlock.size() > 3)
        {
            std::string msg{
                string_format("Downloading starts in {%i}...",
                              (int32_t)ContentDownloader::INIT_DELAY)};

            Shepherd::setDownloadState(msg);

            //	Make sure we are really deeply settled asleep, avoids lots of
            // timed out frames.
            g_Log->Info(
                "Chilling for %d seconds before trying to download sheeps...",
                ContentDownloader::INIT_DELAY);

            boost::thread::sleep(
                get_system_time() +
                posix_time::seconds(ContentDownloader::INIT_DELAY));
        }
#endif

        boost::uintmax_t lpFreeBytesAvailable = 0;

        int32_t failureSleepDuration = TIMEOUT;
        int32_t badSheepSleepDuration = TIMEOUT;

        while (1)
        {

            boost::this_thread::interruption_point();
            bool incorrect_folder = false;
#ifdef WIN32
            ULARGE_INTEGER winlpFreeBytesAvailable, winlpTotalNumberOfBytes,
                winlpRealBytesAvailable;

            if (GetDiskFreeSpaceExA(Shepherd::jsonPath(),
                                    (PULARGE_INTEGER)&winlpFreeBytesAvailable,
                                    (PULARGE_INTEGER)&winlpTotalNumberOfBytes,
                                    (PULARGE_INTEGER)&winlpRealBytesAvailable))
            {
                lpFreeBytesAvailable = winlpFreeBytesAvailable.QuadPart;
            }
            else
                incorrect_folder = true;
#else
            struct statfs buf;
            if (statfs(Shepherd::jsonPath(), &buf) >= 0)
            {
                lpFreeBytesAvailable = (boost::uintmax_t)buf.f_bavail *
                                       (boost::uintmax_t)buf.f_bsize;
            }
            else
                incorrect_folder = true;
#endif

            incorrect_folder = incorrect_folder ||
                               (!isFolderAccessible(Shepherd::jsonPath()) ||
                                !isFolderAccessible(Shepherd::mp4Path()));
            if (lpFreeBytesAvailable <
                    ((boost::uintmax_t)MIN_MEGABYTES * 1024 * 1024) ||
                incorrect_folder)
            {
                if (incorrect_folder)
                {
                    const char* err = "Content folder is not working.  "
                                      "Downloading disabled.\n";
                    Shepherd::addMessageText(err, 180); // 3 minutes

                    boost::thread::sleep(get_system_time() +
                                         posix_time::seconds(TIMEOUT));
                }
                else
                {
                    const char* err =
                        "Low disk space.  Downloading disabled.\n";
                    Shepherd::addMessageText(err, 180); // 3 minutes

                    boost::thread::sleep(get_system_time() +
                                         posix_time::seconds(TIMEOUT));

                    std::scoped_lock lockthis(s_DownloaderMutex);

                    deleteCached(0, 0);
                    deleteCached(0, 1);
                }
            }
            else
            {
                best_anim_old = -1;
                std::string best_anim_old_url;
                best_ctime_old = 0;
                best_rating_old = INT_MAX;

                //	Get the sheep list from the server.
                if (getSheepList())
                {
                    Shepherd::setDownloadState(
                        "Searching for sheep to download...");

                    if (fListDirty)
                    {
                        // clearFlocks();
                        ParseServerDreams();
                    }
                    else
                    {
                        fGotList = true;
                    }

                    do
                    {
                        best_rating = INT_MIN;
                        best_ctime = 0;
                        best_anim = -1;

                        updateCachedSheep();

                        std::scoped_lock lockthis(s_DownloaderMutex);

                        unsigned int i;
                        unsigned int j;

                        size_t downloadedcount = 0;
                        for (i = 0; i < fServerFlock.size(); i++)
                        {
                            //	Iterate the client flock to see if it allready
                            // exists in
                            // the cache.
                            for (j = 0; j < fClientFlock.size(); j++)
                            {
                                if (fServerFlock[i]->id == fClientFlock[j]->id)
                                {
                                    downloadedcount++;
                                    break;
                                }
                            }
                        }
                        //	Iterate the server flock to find the next sheep
                        // to download.
                        for (i = 0; i < fServerFlock.size(); i++)
                        {
                            //	Iterate the client flock to see if it allready
                            // exists in
                            // the cache.
                            for (j = 0; j < fClientFlock.size(); j++)
                            {
                                if (fServerFlock[i]->id == fClientFlock[j]->id)
                                    break;
                            }

                            //@TODO: is this correct?
                            //	If it is not found and the cache is ok to store
                            // than
                            // check if the file should be downloaded based on
                            // rating and server file write time.
                            if ((j == fClientFlock.size()) &&
                                !cacheOverflow(
                                    (double)fServerFlock[i]->fileSize, 0))
                            {
                                //	Check if it is the best file to
                                // download.
                                if ((best_ctime == 0 && best_ctime_old == 0) ||
                                    (fServerFlock[i]->rating > best_rating &&
                                     fServerFlock[i]->rating <=
                                         best_rating_old) ||
                                    (fServerFlock[i]->rating == best_rating &&
                                     fServerFlock[i]->writeTime < best_ctime))
                                {
                                    bool timeCheck = false;
                                    // if (useDreamAI)
                                    {
                                        //  timeCheck =
                                        //  fServerFlock[i]->fileWriteTime() !=
                                        //  best_ctime_old;
                                    }
                                    // else
                                    {
                                        timeCheck = fServerFlock[i]->writeTime >
                                                    best_ctime_old;
                                    }
                                    if (fServerFlock[i]->rating !=
                                            best_rating_old ||
                                        (fServerFlock[i]->rating ==
                                             best_rating_old &&
                                         timeCheck))
                                    {
                                        best_rating = fServerFlock[i]->rating;
                                        best_ctime = fServerFlock[i]->writeTime;
                                        best_anim = static_cast<int>(i);
                                    }
                                }
                            }
                        }

                        //	Found a valid sheep so download it.
                        if (best_anim != -1)
                        {
                            best_ctime_old = best_ctime;
                            best_rating_old = best_rating;
                            //	Make enough room in the cache for it.
                            deleteCached(
                                fServerFlock[static_cast<size_t>(best_anim)]
                                    ->fileSize,
                                0);

                            Shepherd::setDownloadState(string_format(
                                "Downloading dream %i/%i...\n%s",
                                downloadedcount + 1, fServerFlock.size(),
                                fServerFlock[static_cast<size_t>(best_anim)]
                                    ->url.data()));

                            g_Log->Info("Best dream to download rating=%d, "
                                        "fServerFlock index=%d, write time=%s",
                                        best_rating, best_anim,
                                        ctime(&best_ctime));
                            if (downloadSheep(fServerFlock[static_cast<size_t>(
                                    best_anim)]))
                            {
                                // failureSleepDuration = 0;
                                badSheepSleepDuration = TIMEOUT;
                            }
                            else
                            {
                                best_anim_old = best_anim;
                                best_anim_old_url =
                                    fServerFlock[static_cast<size_t>(
                                                     best_anim_old)]
                                        ->url;
                            }
                        }
                        boost::this_thread::interruption_point();
                    } while (best_anim != -1);

                    if (best_anim_old == -1)
                    {
                        //                        int maxPage = g_Settings()->Get(
                        //                            "settings.content.dreams_page", 0);
                        //                        g_Settings()->Set("settings.content.dreams_page",
                        //                                          maxPage + 1);
                        //
                        //                        badSheepSleepDuration = Base::Math::Clamped(
                        //                            badSheepSleepDuration * 2, TIMEOUT, MAX_TIMEOUT);
                        //
                        failureSleepDuration = badSheepSleepDuration;
                        Shepherd::setDownloadState(
                            string_format("All available dreams downloaded, "
                                          "will retry in {%i}...",
                                          failureSleepDuration));
                    }
                    else
                    {
                        //	Gradually increase duration betwee 10 seconds
                        // and TIMEOUT, if
                        // the sheep fail to download consecutively
                        failureSleepDuration = badSheepSleepDuration;

                        badSheepSleepDuration = Base::Math::Clamped(
                            badSheepSleepDuration * 2, TIMEOUT, MAX_TIMEOUT);

                        Shepherd::setDownloadState(string_format(
                            "Downloading failed, will retry in {%i}...\n%s",
                            failureSleepDuration, best_anim_old_url.data()));
                    }
                }
                else
                {
                    //	Error connecting to server so timeout then try again.
                    const char* err = "error connecting to server";
                    Shepherd::addMessageText(err, 180); //	3 minutes.
                    failureSleepDuration = TIMEOUT;

                    badSheepSleepDuration = 10;
                }

                boost::thread::sleep(get_system_time() +
                                     posix_time::seconds(failureSleepDuration));

                // failureSleepDuration = TIMEOUT;
            }
        }
    }
    catch (thread_interrupted const&)
    {
    }
}

/**
        getSheepList().
        This method will download the sheep list and uncompress it.
*/
bool SheepDownloader::getSheepList()
{
    fListDirty = EDreamClient::GetDreams();
    return fListDirty;
}

/**
        shepherdCallback().
        This method is also in charge of downling the sheep in their
   transitional order.
*/
void SheepDownloader::shepherdCallback(void* data)
{
    ((SheepDownloader*)data)->FindSheepToDownload();
}

}; // namespace ContentDownloader
