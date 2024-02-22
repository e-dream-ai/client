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
#ifndef _SHEEPDOWNLOADER_H_
#define _SHEEPDOWNLOADER_H_

#include "Dream.h"
#include "Networking.h"

namespace ContentDownloader
{
class SheepRenderer;

/**

        This class is responsible for downloading and queueing new sheep to the
   renderer..
*/
class SheepDownloader
{
    /// number of sheep that the downloader has downloaded
    static int fDownloadedSheep;

    /// sheep flocks
    static SheepArray fServerFlock;
    static SheepArray fClientFlock;
    SheepRenderer* fRenderer;

    /// boolean for message checks
    bool fHasMessage;

    static time_t fLastListTime;

    static std::mutex s_DownloaderMutex;

    bool m_bAborted;

    Network::spCFileDownloader m_spSheepDownloader;

    std::mutex m_AbortMutex;

  protected:
    ///	Downloads the given sheep and queues it up for rendering.
    /// Returns true if succeeds
    bool downloadSheep(sDreamMetadata* sheep);

    ///	Function to parse the cache and find a sheep to download.
    void FindSheepToDownload();

    ///	Ipdate the cached sheep and make sure there is enough room for a second
    /// sheep.
    void updateCachedSheep();

    ///	Clears the flock list being maintained
    void clearFlocks();

    /**	Delete enough sheep to clear enough room for the given amount of bytes.

            deleteCached().
            This function will make sure there is enough room in the cache for any
       newly downloaded files. If the cache is to large than the oldest and worst
       rated files will be deleted.
    */
    void deleteCached(const uint64_t& bytes, const int getGenerationType);

    bool isFolderAccessible(const char* folder);

    ///	This methods parses the sheep lists and intializes the array of server
    /// sheep.
    void ParseServerDreams();
    /// Returns the amount of dreams for all the pages
    int ParseDreamsPage(int _page);

    ///	Message retrival from server.
    void setHasMessage(const bool& hasMessage) { fHasMessage = hasMessage; }
    bool hasMessage() const { return fHasMessage; }

    ///	Checks the disk space to make sure the cache is not being overflowed.
    int cacheOverflow(const double& bytes, const int getGenerationType) const;

    /// Clean global and static data for the downloader threads.
    static void closeDownloader();

    ///	Function to initialize the downloader threads
    static void initializeDownloader();

    static bool fGotList;

    static bool fListDirty;

  public:
    SheepDownloader();
    virtual ~SheepDownloader();
    /**
            shepherdCallback().
            This method is also in charge of downling the sheep in their
       transitional order.
    */
    static void shepherdCallback(void* data);

    static int numberOfDownloadedSheep();
    /**
            getSheepList().
            This method will download the sheep list and uncompress it.
    */
    static bool getSheepList();
    static const SheepArray& getServerFlock() { return fServerFlock; }
    static const SheepArray& getClientFlock() { return fClientFlock; }

    /// add to the number of downloaded sheep (called by torrent)
    static void addDownloadedSheep(int sheep) { fDownloadedSheep += sheep; }

    static void deleteSheep(std::string_view _uuid);

    static void deleteSheep(sDreamMetadata* sheep);

    /// Aborts the working thread
    void Abort(void);

    ///	Declare friend classes for protected data accesss.
    friend class Shepherd;
};

}; // namespace ContentDownloader

#endif
