#ifndef _SIMPLEPLAYLIST_H
#define _SIMPLEPLAYLIST_H

#include "Log.h"
#include "Playlist.h"
#include <queue>

namespace ContentDecoder
{

/*
        CSimplePlaylist().
        Normal linear playlist based on a queue.
*/
class CSimplePlaylist : public CPlaylist
{
    std::queue<std::string> m_List;

  public:
    CSimplePlaylist() : CPlaylist() {}

    virtual ~CSimplePlaylist() {}

    //
    virtual bool Add(const std::string& _file)
    {
        m_List.push(_file);
        return (true);
    }

    virtual uint32 Size() { return static_cast<uint32>(m_List.size()); }

    virtual bool Next(std::string& _result, bool& _bEnoughSheep,
                      uint32 /*_curID*/, bool& _playFreshSheep,
                      const bool /*_bRebuild*/ = false,
                      bool /*_bStartByRandom*/ = false)
    {
        _playFreshSheep = false;
        if (m_List.empty())
            return false;

        _result = m_List.front();
        m_List.pop();

        _bEnoughSheep = true;

        return true;
    }

    virtual bool ChooseSheepForPlaying(uint32 curGen, uint32 curID)
    {
        g_PlayCounter().IncPlayCount(curGen, curID);

        return true;
    }
    //	Overrides the playlist to play _id next time.
    virtual void Override(const uint32 _id) {}

    //	Queues _id to be deleted.
    virtual void Delete(const uint32 _id) {}
};

MakeSmartPointers(CSimplePlaylist);

} // namespace ContentDecoder

#endif
