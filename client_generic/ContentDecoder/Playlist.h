#ifndef _PLAYLIST_H
#define _PLAYLIST_H

#include <string>
#include <string_view>

#include "SmartPtr.h"
#include "base.h"
#include "Dream.h"

namespace ContentDecoder
{

/*
        CPlaylist().
        Playlist interface.
*/
class CPlaylist
{
  public:
    CPlaylist() {}
    virtual ~CPlaylist() {}
    virtual uint32_t Size() = PureVirtual;
    virtual bool Add(const std::string& _file) = PureVirtual;
    virtual bool Next(std::string& _result, bool& _bEnoughSheep,
                      uint32_t _curID, bool& _playFreshSheep,
                      const bool _bRebuild = false,
                      bool _bStartByRandom = true) = PureVirtual;
    virtual void Override(const uint32_t _id) = PureVirtual;
    virtual void Delete(const uint32_t _id) = PureVirtual;
    virtual bool
    PopFreshlyDownloadedSheep([[maybe_unused]] std::string& _result)
    {
        return false;
    }
    virtual bool HasFreshlyDownloadedSheep() { return false; }
    virtual bool GetDreamMetadata(
        [[maybe_unused]] std::string_view _filePath,
        [[maybe_unused]] ContentDownloader::sDreamMetadata** _outDreamPtr)
    {
        return false;
    }
};

MakeSmartPointers(CPlaylist);

} // namespace ContentDecoder

#endif
