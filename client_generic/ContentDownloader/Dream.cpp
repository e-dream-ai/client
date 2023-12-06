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
#include <string.h>
#include <time.h>
#include <vector>

#include "Dream.h"

namespace ContentDownloader
{

Dream::Dream()
//
// Description:
//		Default constrictor. Initialize all class data.
//
{
    fURL = nullptr;
    fFileSize = 0;
    fFileName = nullptr;
    fName = nullptr;
    fWriteTime = 0;
    fRating = 0;
    fDownloaded = false;
    fSheepId = 0;
    fFirst = 0;
    fLast = 0;
    fType = 0;
    fUuid = nullptr;
    fAuthor = nullptr;
    fName = nullptr;
    fDeleted = false;
    fGeneration = 0;
    fIsTemp = false;
}

Dream::Dream(const Dream &sheep)
//
// Description:
//		Copy constrictor. copies all class data.
//
{
    fURL = nullptr;
    fFileName = nullptr;
    fUuid = nullptr;
    fAuthor = nullptr;
    fName = nullptr;
    setURL(sheep.fURL);
    setFileName(sheep.fFileName);
    setUuid(sheep.fUuid);
    setAuthor(sheep.fAuthor);
    setName(sheep.fName);
    fFileSize = sheep.fFileSize;
    fWriteTime = sheep.fWriteTime;
    fRating = sheep.fRating;
    fDownloaded = sheep.fDownloaded;
    fSheepId = sheep.fSheepId;
    fFirst = sheep.fFirst;
    fLast = sheep.fLast;
    fType = sheep.fType;
    fDeleted = sheep.fDeleted;
    fGeneration = sheep.fGeneration;
    fIsTemp = sheep.fIsTemp;
}

Dream::~Dream()
{
    SAFE_DELETE(fUuid)
    SAFE_DELETE(fURL)
    SAFE_DELETE(fFileName);
    SAFE_DELETE(fAuthor);
    SAFE_DELETE(fName);
}

static void UpdateString(char *&_strToUpdate, const char *_newValue)
{
    if (_strToUpdate != nullptr)
    {
        delete[] _strToUpdate;
        _strToUpdate = nullptr;
    }
    if (_newValue)
    {
        _strToUpdate = new char[strlen(_newValue) + 1];
        strcpy(_strToUpdate, _newValue);
    }
}

void Dream::setURL(const char *url) { UpdateString(fURL, url); }

void Dream::setFileName(const char *filename)
{
    UpdateString(fFileName, filename);
}

void Dream::setUuid(const char *uuid) { UpdateString(fUuid, uuid); }

void Dream::setAuthor(const char *_author) { UpdateString(fAuthor, _author); }

void Dream::setName(const char *_name) { UpdateString(fName, _name); }

void Dream::setFileWriteTime(const char *timeString)
{
    struct tm timeinfo = {};
    strptime(timeString, "%Y-%m-%dT%H:%M:%S", &timeinfo);
    timeinfo.tm_isdst = 0; // Set daylight saving time to 0
    time_t time = std::mktime(&timeinfo);
    setFileWriteTime(time);
}

}; // namespace ContentDownloader
