//
//  PlatformUtils.h
//  e-dream
//
//  Created by Tibi Hencz on 12.12.2023.
//

#ifndef PlatformUtils_h
#define PlatformUtils_h

#include <cstdio>
#include <string>

class PlatformUtils
{
  public:
    static bool IsInternetReachable();
    static std::string GetBuildDate();
    static std::string GetGitRevision();
    static std::string GetAppVersion();
};

#endif /* PlatformUtils_h */
