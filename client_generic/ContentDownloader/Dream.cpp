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
#include <string>
#include <time.h>
#include <vector>

#include "Dream.h"

#ifdef WIN32
// Windows specific strptime implementation
#include <iomanip>
#include <sstream>

extern "C" char* strptime(const char* s, const char* f, struct tm* tm)
{
    // Isn't the C++ standard lib nice? std::get_time is defined such that its
    // format parameters are the exact same as strptime. Of course, we have to
    // create a string stream first, and imbue it with the current C locale, and
    // we also have to make sure we return the right things if it fails, or
    // if it succeeds, but this is still far simpler an implementation than any
    // of the versions in any of the C standard libraries.
    std::istringstream input(s);
    input.imbue(std::locale(setlocale(LC_ALL, nullptr)));
    input >> std::get_time(tm, f);
    if (input.fail())
    {
        return nullptr;
    }
    return (char*)(s + input.tellg());
}
#endif

namespace ContentDownloader
{

void sDreamMetadata::setFileWriteTime(const std::string& timeString)
{
    if (timeString.empty())
        return;
    struct tm timeinfo = {};
    strptime(timeString.data(), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    timeinfo.tm_isdst = 0; // Set daylight saving time to 0
    time_t time = std::mktime(&timeinfo);
    writeTime = time;
}

// Set current time to now
void sDreamMetadata::setFileWriteTimeToNow()
{
    time_t time = std::time(0);
    writeTime = time;
}

}; // namespace ContentDownloader
