#ifndef CLIENTVERSION_H_INCLUDED
#define CLIENTVERSION_H_INCLUDED

#ifdef	WIN32
	#define CLIENT_VERSIONW				L"WIN_3.0.1"
	#define	CLIENT_VERSION_PRETTYW		L"Windows 3.0"
	#define CLIENT_SETTINGSW			L"ElectricSheep"

	#define CLIENT_VERSION				"WIN_3.0.1"
	#define	CLIENT_VERSION_PRETTY		"Windows 3.0.1"
	#define CLIENT_SETTINGS				"ElectricSheep"

	#define	CLIENT_VERSION_PRETTYW2		L"version 3.0.1"
#else
	#ifdef MAC
		#define CLIENT_VERSION				"OSX_3.0.3"
		#define	CLIENT_VERSION_PRETTY		"macOS 3.0.3"
		#define CLIENT_SETTINGS				"ElectricSheep"
	#else
		#define CLIENT_VERSION				"LNX_3.0.2"
		#define	CLIENT_VERSION_PRETTY		"version 3.0.2"
		#define CLIENT_SETTINGS				"ElectricSheep"
	#endif
#endif

#ifdef WIN32
	#define	CLIENT_SERVERW				L"v2d7c.sheepserver.net"
	#define REDIRECT_SERVERW			L"community.sheepserver.net"
#endif

#define	CLIENT_SERVER				"v2d7c.sheepserver.net"
//#define	CLIENT_SERVER_REGISTERED	"r2d7c.sheepserver.net"

#define REDIRECT_SERVER                "community.sheepserver.net"
#define REDIRECT_SERVER_FULL        "https://community.sheepserver.net"

#define DREAM_SERVER                "https://e-dream-76c98b08cc5d.herokuapp.com"
#define DREAM_ENDPOINT              DREAM_SERVER "/api/v1/dream"
#define REFRESH_ENDPOINT            DREAM_SERVER "/api/v1/auth/refresh"

#define REFRESH_TOKEN_MAX_LENGTH    1024
#define ACCESS_TOKEN_MAX_LENGTH     2048

#define	BETA_RELEASE


#endif // CLIENTVERSION_H_INCLUDED
