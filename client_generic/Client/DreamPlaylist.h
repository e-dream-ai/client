#ifndef	_DREAMPLAYLIST_H
#define _DREAMPLAYLIST_H

#include "Common.h"
#include "Playlist.h"
#include "Log.h"
#include "Timer.h"
#include "Settings.h"
#include "PlayCounter.h"
#include <sstream>
#include <sys/stat.h>
#include "Shepherd.h"
#include "isaac.h"
#include "ContentDownloader.h"

#include	"boost/filesystem/path.hpp"
#include	"boost/filesystem/operations.hpp"
#include	"boost/filesystem/convenience.hpp"
#include	<boost/thread.hpp>

using boost::filesystem::path;
using boost::filesystem::exists;
using boost::filesystem::directory_iterator;
using boost::filesystem::extension;


namespace ContentDecoder
{

class	CDreamPlaylist : public CPlaylist
{
	boost::mutex	m_Lock;
	
	boost::mutex	m_CurrentPlayingLock;

	//	Path to folder to monitor & update interval in seconds.
	path			m_Path;
	fp8				m_NormalInterval;
	fp8				m_EmptyInterval;
	fp8				m_Clock;

	Base::CTimer	m_Timer;
	
	uint32			m_numSheep;
	
	bool			m_AutoMedian;
	bool			m_RandomMedian;
	fp8				m_MedianLevel;
	uint64			m_FlockMBs;
	uint64			m_FlockGoldMBs;

	
	//
	void	DeduceGraphnessFromFilenameAndQueue( path const &/*_basedir*/, const std::string& _filename )
	{
		
		m_numSheep++;
	}

	void	AutoMedianLevel(uint64 megabytes)
	{
		if (megabytes < 100)
		{
			g_Log->Info( "Flock < 100 MBs AutoMedian = 1." );
			m_MedianLevel = 1.;
		} else
		if (megabytes > 1000)
		{
			g_Log->Info( "Flock > 1000 MBs AutoMedian = .25" );
			m_MedianLevel = .25;
		} else
		{
			m_MedianLevel = 13./12. - fp8(megabytes)/1200.;
			if (m_MedianLevel > 1.)
				m_MedianLevel = 1.;
			if (m_MedianLevel < .25)
				m_MedianLevel = .25;
			g_Log->Info( "Flock 100 - 1000 MBs AutoMedian = %f", m_MedianLevel );
		}
	}

	//
	void	UpdateDirectory( path const &_dir, const bool _bRebuild = false )
	{
		//boost::mutex::scoped_lock locker( m_Lock );

		m_numSheep = 0;
		
		std::vector<std::string>	files;

		int usedsheeptype = g_Settings()->Get( "settings.player.PlaybackMixingMode", 0 );
        const char* ext = ContentDownloader::Shepherd::videoExtension();
        if ( usedsheeptype == 0 )
        {
            if ( Base::GetFileList( files, _dir.string().c_str(), ext, true, false, true ) == false )
                usedsheeptype = 2; // only gold, if any - revert to all if gold not found
        }

        if ( usedsheeptype == 1 ) // free sheep only
            Base::GetFileList( files, _dir.string().c_str(), ext, false, true, true );

        if ( usedsheeptype > 1 ) // play all sheep, also handle case of error (2 is maximum allowed value)
            Base::GetFileList( files, _dir.string().c_str(), ext, true, true, true );

		//	Clear the sheep context...
		if( _bRebuild )
		{
			if (m_AutoMedian)
				AutoMedianLevel( m_FlockMBs + m_FlockGoldMBs );
			m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Clear", "d", m_MedianLevel ) );
			//m_pState->Execute( "Clear()" );
		}

		for( std::vector<std::string>::const_iterator i=files.begin(); i!=files.end(); ++i )
			DeduceGraphnessFromFilenameAndQueue( _dir, *i );
	}

	public:
			CDreamPlaylist( const std::string &_watchFolder, int &/*_usedsheeptype*/ ) : CPlaylist()/*, m_UsedSheepType(_usedsheeptype)*/
			{
				m_NormalInterval = fp8(g_Settings()->Get( "settings.player.NormalInterval", 100 ));
				m_EmptyInterval = 10.0f;
				m_Clock = 0.0f;
 				m_Path = _watchFolder.c_str();
				
				m_numSheep = 0;

				int32	loopIterations = g_Settings()->Get( "settings.player.LoopIterations", 2 );
				bool	seamlessPlayback = g_Settings()->Get( "settings.player.SeamlessPlayback", false );
				fp8		playEvenly = (fp8) g_Settings()->Get( "settings.player.PlayEvenly", 100 ) / 100.0;
				m_MedianLevel = (fp8) g_Settings()->Get( "settings.player.MedianLevel", 80 ) / 100.0;
				m_AutoMedian = g_Settings()->Get( "settings.player.AutoMedianLevel", true );
				m_RandomMedian = g_Settings()->Get( "settings.player.RandomMedianLevel", true );
				if (m_AutoMedian)
				{
					// HACK to get flock size before full initialization
					ContentDownloader::Shepherd::setRootPath( g_Settings()->Get( "settings.content.sheepdir", g_Settings()->Root() + "content" ).c_str() );
					m_FlockMBs = ContentDownloader::Shepherd::GetFlockSizeMBsRecount(0);
					m_FlockGoldMBs = ContentDownloader::Shepherd::GetFlockSizeMBsRecount(1);
					AutoMedianLevel( m_FlockMBs );
				}

				
				UpdateDirectory( m_Path );
			}

			//
			virtual ~CDreamPlaylist()
			{				
				SAFE_DELETE( m_pState );
			}

			//
			virtual bool	Add( const std::string &_file )
			{
				boost::mutex::scoped_lock locker( m_Lock );
				//m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Add", "s", _file.c_str() ) );
				return true ;
			}

			virtual uint32	Size()
			{
				boost::mutex::scoped_lock locker( m_Lock );
				int32	ret = 0;
				//m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Size", ">i", &ret ) );
				return (uint32)ret;
			}

			virtual bool	Next( std::string &_result, bool &_bEnoughSheep, uint32 _curID, const bool _bRebuild = false, bool _bStartByRandom = true )
			{
				boost::mutex::scoped_lock locker( m_Lock );
								
				fp8 interval = ( m_numSheep >  kSheepNumTreshold ) ? m_NormalInterval : m_EmptyInterval;

				//	Update from directory if enough time has passed, or we're asked to.
				if( _bRebuild || ((m_Timer.Time() - m_Clock) > interval) )
				{
					if (g_PlayCounter().ReadOnlyPlayCounts())
					{
						g_PlayCounter().ClosePlayCounts();
						m_FlockMBs = ContentDownloader::Shepherd::GetFlockSizeMBsRecount(0);
						m_FlockGoldMBs = ContentDownloader::Shepherd::GetFlockSizeMBsRecount(1);
					}
					UpdateDirectory( m_Path, _bRebuild );
					m_Clock = m_Timer.Time();
				}

				_bEnoughSheep = ( m_numSheep > kSheepNumTreshold );
				
                
				
				return true;
			}
			
			virtual bool ChooseSheepForPlaying(uint32 curGen, uint32 curID)
			{
				g_PlayCounter().IncPlayCount(curGen, curID);
				
				return true;
			}

			//	Overrides the playlist to play _id next time.
			void	Override( const uint32 _id )
			{
				boost::mutex::scoped_lock locker( m_Lock );
				//m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Override", "i", _id ) );
			}

			//	Queues _id to be deleted.
			void	Delete( const uint32 _id )
			{
				boost::mutex::scoped_lock locker( m_Lock );
				//m_pState->Pop( Base::Script::Call( m_pState->GetState(), "Delete", "i", _id ) );
			}
};

MakeSmartPointers( CDreamPlaylist );

}

#endif //_DREAMPLAYLIST_H
