#ifndef _PLAYLIST_H
#define _PLAYLIST_H

#include "SmartPtr.h"
#include "base.h"
#include <string>

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
    virtual bool ChooseSheepForPlaying(uint32_t curGen,
                                       uint32_t curID) = PureVirtual;
    virtual void Override(const uint32_t _id) = PureVirtual;
    virtual void Delete(const uint32_t _id) = PureVirtual;
    virtual bool PopFreshlyDownloadedSheep(std::string& _result)
    {
        return false;
    }
    virtual bool HasFreshlyDownloadedSheep() { return false; }
    virtual bool GetDreamNameAndAuthor(const std::string& _filePath,
                                       std::string* _outDreamName,
                                       std::string* _outDreamAuthor)
    {
        return false;
    }

    virtual bool GetSheepInfoFromPath(const std::string& _path,
                                      uint32_t& Generation, uint32_t& ID,
                                      uint32_t& First, uint32_t& Last,
                                      std::string& _filename)
    {
        //	Remove the full path so we can work on the filename.
        size_t offs = _path.find_last_of("/\\", _path.size());

        _filename = _path.substr(offs + 1);

        //	Deduce graph position from filename.
        int ret = sscanf(_filename.c_str(), "%d=%d=%d=%d.avi", &Generation, &ID,
                         &First, &Last);
        if (ret != 4)
        {
            g_Log->Error("Unable to deduce sheep info from %s", _path.c_str());
            return false;
        }

        return true;
    }
};

MakeSmartPointers(CPlaylist);

} // namespace ContentDecoder

#endif
