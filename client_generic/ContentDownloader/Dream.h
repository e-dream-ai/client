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
#ifndef _SHEEP_H_
#define _SHEEP_H_

#include <string>
#include <string_view>
#include <unordered_map>

#include "base.h"

namespace ContentDownloader
{

enum eDreamFlags : uint8_t
{
    DREAM_FLAG_NONE = 0,
    DREAM_FLAG_DOWNLOADED = 1,
    DREAM_FLAG_DELETED = 2
};
DEFINE_ENUM_FLAG_OPERATORS(eDreamFlags);

struct sDreamMetadata
{
    std::string url;
    std::string fileName;
    std::string uuid;
    std::string author;
    std::string name;
    uint64_t fileSize;
    uint32_t id;
    time_t writeTime;
    eDreamFlags flags = DREAM_FLAG_NONE;
    float activityLevel = 1.f;
    int rating;
    void setFileWriteTime(const std::string& timeString);
};

struct SheepArray
{
    typedef std::string_view key_type;
    typedef sDreamMetadata* mapped_type;
    typedef std::vector<mapped_type>::iterator iterator;

    std::vector<mapped_type> vecData;
    std::unordered_map<key_type, mapped_type> mapData;

    iterator begin() { return vecData.begin(); }
    iterator end() { return vecData.end(); }
    size_t size() { return vecData.size(); }

    sDreamMetadata*& operator[](size_t i) { return vecData[i]; }

    sDreamMetadata* operator[](key_type key) { return mapData[key]; }

    bool tryGetSheepWithUuid(key_type key, sDreamMetadata*& outSheep) const
    {
        auto i = mapData.find(key);
        if (i == mapData.end())
            return false;
        outSheep = i->second;
        return true;
    }

    void push_back(sDreamMetadata* sheep)
    {
        key_type key{sheep->uuid};
        mapData[key] = sheep;
        vecData.push_back(sheep);
    }

    void clear()
    {
        mapData.clear();
        vecData.clear();
    }
};
// typedef vecmap<std::string, Sheep *> SheepArray;

}; // namespace ContentDownloader

#endif
