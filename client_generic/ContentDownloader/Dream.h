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

//
// Description:
//		This class will represent a sheep as it exists
// on the server or on the client. It has methods to
// get and set different aspects of a sheep
//
class Dream
{
  public:
    // Default constructor
    //
    Dream();

    // Copy Constructor
    //
    Dream(const Dream& sheep);

    // Destructor
    //
    ~Dream();

    // sets the URL that this sheep lives at
    //
    void setURL(const char* url);

    // returns the URL that the sheep lives at
    //
    const char* URL() const { return fURL; }

    // Sets the current rating of the sheep on the server
    //
    void setRating(const int& rating) { fRating = rating; }

    // gets the current rating of the sheep
    //
    int rating() const { return fRating; }

    // Sets the sheep file size on the server
    //
    void setFileSize(const uint64_t& size) { fFileSize = size; }

    // gets the sheep file size
    //
    uint64_t fileSize() const { return fFileSize; }

    // Sets the sheep file name
    //
    void setFileName(const char* name);

    // gets the sheep file name
    //
    const char* fileName() const { return fFileName; }

    // Sets the sheep UUID
    //
    void setUuid(const char* uuid);

    // gets the sheep UUID
    //
    const char* uuid() const { return fUuid; }

    // Sets the sheep author
    //
    void setAuthor(const char* author);

    // gets the sheep author
    //
    const char* author() const { return fAuthor; }

    // Sets the sheep name
    //
    void setName(const char* _name);

    // gets the sheep name
    //
    const char* name() const { return fName; }

    // sets the file write time
    //
    void setFileWriteTime(const time_t& time) { fWriteTime = time; }

    void setFileWriteTime(const char* timeString);

    // gets the file write time
    //
    time_t fileWriteTime() const { return fWriteTime; }

    // sets the sheep id
    //
    void setId(const uint32_t& id) { fSheepId = id; }

    // gets the sheep id
    //
    uint32_t id() const { return fSheepId; }

    // sets the the sheep that transitions into this sheep
    //
    void setFirstId(const uint32_t& first) { fFirst = first; }

    // gets the sheep that transitions into this sheep
    //
    uint32_t firstId() const { return fFirst; }

    // sets the sheep that this sheep should transistion
    // into
    //
    void setLastId(const uint32_t& last) { fLast = last; }

    // gets the sheep that this sheep should transition into
    //
    uint32_t lastId() const { return fLast; }

    // sets whether or not this sheep has been deleted from the
    // server
    //
    void setDeleted(const bool& state) { fDeleted = state; }

    // gets if the sheep has been deleted from the server
    //
    bool deleted() const { return fDeleted; }

    // set the sheep type
    //
    void setType(const int& type) { fType = type; }

    // returns the sheep type
    //
    int type() const { return fType; }

    // sets whether or not the sheep has been downloaded
    //
    void setDownloaded(const bool& state) { fDownloaded = state; }

    // returns if the sheep has been downloaded
    //
    bool downloaded() const { return fDownloaded; }

    // set the sheep generation
    //
    void setGeneration(const uint32_t& gen) { fGeneration = gen; }

    // returns the sheep generation
    //
    uint32_t generation() const { return fGeneration; }

    // returns sheep generation type (currently 0 - normal, 1 - gold)
    int getGenerationType() const
    {
        if (generation() < 10000)
            return 0;
        else
            return 1;
    }

    // sets if the sheep is a tmp file
    // which means it is either being downloaded
    // or was left from the last download process
    //
    void setIsTemp(const bool& isTemp) { fIsTemp = isTemp; }

    // returns if the sheep is a tmp sheep
    //
    bool isTemp() const { return fIsTemp; }

  private:
    // private memeber data
    //
    char* fURL;
    char* fFileName;
    char* fUuid;
    char* fAuthor;
    char* fName;
    uint64_t fFileSize;
    time_t fWriteTime;
    int fRating;
    uint32_t fSheepId;
    uint32_t fFirst;
    uint32_t fLast;
    int fType;
    bool fDeleted;
    bool fDownloaded;
    uint32_t fGeneration;
    bool fIsTemp;
};

struct SheepArray
{
    typedef std::string_view key_type;
    typedef Dream* mapped_type;
    typedef std::vector<mapped_type>::iterator iterator;

    std::vector<mapped_type> vecData;
    std::unordered_map<key_type, mapped_type> mapData;

    iterator begin() { return vecData.begin(); }
    iterator end() { return vecData.end(); }
    size_t size() { return vecData.size(); }

    Dream*& operator[](size_t i) { return vecData[i]; }

    Dream* operator[](key_type key) { return mapData[key]; }

    bool tryGetSheepWithUuid(key_type key, Dream*& outSheep) const
    {
        auto i = mapData.find(key);
        if (i == mapData.end())
            return false;
        outSheep = i->second;
        return true;
    }

    void push_back(Dream* sheep)
    {
        key_type key{sheep->uuid()};
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
