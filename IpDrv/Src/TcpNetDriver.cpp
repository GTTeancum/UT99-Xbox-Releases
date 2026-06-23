/*=============================================================================
	TcpNetDriver.cpp: Unreal TCP/IP driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.

Notes:
	* See \msdev\vc98\include\winsock.h and \msdev\vc98\include\winsock2.h 
	  for Winsock WSAE* errors returned by Windows Sockets.
=============================================================================*/

#include "IpDrvPrivate.h"

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

// Classes.
class UTcpNetDriver;
class UTcpipConnection;

// Size of a UDP header.
#define IP_HEADER_SIZE     (20)
#define UDP_HEADER_SIZE    (IP_HEADER_SIZE+8)
#define SLIP_HEADER_SIZE   (UDP_HEADER_SIZE+4)
#define WINSOCK_MAX_PACKET (512)
#define NETWORK_MAX_PACKET (576)

// Variables.
UBOOL GInitialized;

#if TARGET_XBOX
struct FXboxTcpSecureTravelHost
{
	UBOOL	Valid;
	XNADDR	XnAddr;
	XNKID	SessionKeyId;
	XNKEY	SessionKey;
	in_addr	SecureAddr;
	UBOOL	KeyRegisteredByIpDrv;
};

static FXboxTcpSecureTravelHost GXboxTcpSecureTravelHost;

static UBOOL XboxTcpIsXNetVirtualAddress( in_addr Addr )
{
	DWORD Ip = 0;
	IpGetInt( Addr, Ip );
	if( Ip == 0 || Ip == INADDR_NONE || Ip == INADDR_BROADCAST )
		return 0;

	BYTE* B = (BYTE*)&Ip;
	return B[0] == 0;
}

extern "C" void XboxIpDrvClearSecureTravelHost()
{
	if( GXboxTcpSecureTravelHost.Valid )
	{
		debugf( NAME_Log, TEXT("XNET secure travel host cleared addr=%s"), *IpString(GXboxTcpSecureTravelHost.SecureAddr) );
		if( GXboxTcpSecureTravelHost.KeyRegisteredByIpDrv )
		{
			INT Result = XNetUnregisterKey( &GXboxTcpSecureTravelHost.SessionKeyId );
			debugf( NAME_Log, TEXT("XNET secure travel host key unregister result=%i"), Result );
		}
	}
	appMemzero( &GXboxTcpSecureTravelHost, sizeof(GXboxTcpSecureTravelHost) );
}

extern "C" void XboxIpDrvSetSecureTravelHost( const XNADDR* XnAddr, const XNKID* SessionKeyId, const XNKEY* SessionKey, DWORD PreferredAddress )
{
	if( !XnAddr || !SessionKeyId || !SessionKey )
	{
		XboxIpDrvClearSecureTravelHost();
		return;
	}

	if( GXboxTcpSecureTravelHost.Valid )
		XboxIpDrvClearSecureTravelHost();

	appMemzero( &GXboxTcpSecureTravelHost, sizeof(GXboxTcpSecureTravelHost) );
	GXboxTcpSecureTravelHost.Valid = 1;
	appMemcpy( &GXboxTcpSecureTravelHost.XnAddr, XnAddr, sizeof(GXboxTcpSecureTravelHost.XnAddr) );
	appMemcpy( &GXboxTcpSecureTravelHost.SessionKeyId, SessionKeyId, sizeof(GXboxTcpSecureTravelHost.SessionKeyId) );
	appMemcpy( &GXboxTcpSecureTravelHost.SessionKey, SessionKey, sizeof(GXboxTcpSecureTravelHost.SessionKey) );
	IpSetInt( GXboxTcpSecureTravelHost.SecureAddr, PreferredAddress );
	debugf( NAME_Log, TEXT("XNET secure travel host set preferred=%s hasPreferred=%i"),
		*IpString(GXboxTcpSecureTravelHost.SecureAddr),
		PreferredAddress ? 1 : 0 );
}

static INT XboxTcpHexValue( TCHAR Ch )
{
	if( Ch >= TEXT('0') && Ch <= TEXT('9') )
		return Ch - TEXT('0');
	if( Ch >= TEXT('A') && Ch <= TEXT('F') )
		return Ch - TEXT('A') + 10;
	if( Ch >= TEXT('a') && Ch <= TEXT('f') )
		return Ch - TEXT('a') + 10;
	return -1;
}

static UBOOL XboxTcpHexDecode( const TCHAR* Text, BYTE* Out, INT Count )
{
	if( !Text || !Out || Count < 0 || appStrlen(Text) != Count*2 )
		return 0;

	for( INT i=0; i<Count; i++ )
	{
		INT Hi = XboxTcpHexValue( Text[i*2] );
		INT Lo = XboxTcpHexValue( Text[i*2+1] );
		if( Hi < 0 || Lo < 0 )
			return 0;
		Out[i] = (BYTE)((Hi << 4) | Lo);
	}
	return 1;
}

static UBOOL XboxTcpEnsureTravelHostKey( const TCHAR* Where )
{
	if( !GXboxTcpSecureTravelHost.Valid )
		return 0;
	if( GXboxTcpSecureTravelHost.KeyRegisteredByIpDrv )
		return 1;

	INT Result = XNetRegisterKey( &GXboxTcpSecureTravelHost.SessionKeyId, &GXboxTcpSecureTravelHost.SessionKey );
	if( Result == 0 )
	{
		GXboxTcpSecureTravelHost.KeyRegisteredByIpDrv = 1;
		debugf( NAME_Log, TEXT("XNET secure %s registered travel key in IpDrv"),
			Where ? Where : TEXT("unknown") );
		return 1;
	}
	if( Result == WSAEALREADY )
	{
		debugf( NAME_Log, TEXT("XNET secure %s travel key already registered"),
			Where ? Where : TEXT("unknown") );
		return 1;
	}

	debugf( NAME_Log, TEXT("XNET secure %s travel key register failed result=%i"),
		Where ? Where : TEXT("unknown"),
		Result );
	return 0;
}

static UBOOL XboxTcpTranslateSecureTravelHost( in_addr& Addr, const TCHAR* Where, UBOOL bLogStatus )
{
	if( !GXboxTcpSecureTravelHost.Valid )
		return 0;
	if( !XboxTcpEnsureTravelHostKey(Where) )
		return 0;

	FString OldAddress = IpString( Addr );
	IN_ADDR Translated;
	appMemzero( &Translated, sizeof(Translated) );
	INT Result = XNetXnAddrToInAddr( &GXboxTcpSecureTravelHost.XnAddr, &GXboxTcpSecureTravelHost.SessionKeyId, &Translated );
	if( Result != 0 )
	{
		debugf( NAME_Log, TEXT("XNET secure %s translate failed old=%s result=%i"),
			Where ? Where : TEXT("unknown"),
			*OldAddress,
			Result );
		return 0;
	}

	DWORD OldIp = 0;
	DWORD NewIp = 0;
	IpGetInt( Addr, OldIp );
	IpGetInt( Translated, NewIp );
	Addr = Translated;
	GXboxTcpSecureTravelHost.SecureAddr = Translated;
	if( bLogStatus || OldIp != NewIp )
	{
		debugf( NAME_Log, TEXT("XNET secure %s translated old=%s new=%s"),
			Where ? Where : TEXT("unknown"),
			*OldAddress,
			*IpString(Addr) );
	}
	return 1;
}

static UBOOL XboxTcpTranslateSecurePeer( in_addr& Addr, const XNADDR* XnAddr, const XNKID* SessionKeyId, const TCHAR* Where, UBOOL bLogStatus )
{
	if( !XnAddr || !SessionKeyId )
		return 0;

	FString OldAddress = IpString( Addr );
	IN_ADDR Translated;
	appMemzero( &Translated, sizeof(Translated) );
	INT Result = XNetXnAddrToInAddr( XnAddr, SessionKeyId, &Translated );
	if( Result != 0 )
	{
		debugf( NAME_Log, TEXT("XNET secure %s peer translate failed old=%s result=%i"),
			Where ? Where : TEXT("unknown"),
			*OldAddress,
			Result );
		return 0;
	}

	DWORD OldIp = 0;
	DWORD NewIp = 0;
	IpGetInt( Addr, OldIp );
	IpGetInt( Translated, NewIp );
	Addr = Translated;
	if( bLogStatus || OldIp != NewIp )
	{
		debugf( NAME_Log, TEXT("XNET secure %s peer translated old=%s new=%s"),
			Where ? Where : TEXT("unknown"),
			*OldAddress,
			*IpString(Addr) );
	}
	return 1;
}

static DWORD XboxTcpEnsureSecureAssociation( in_addr& Addr, const TCHAR* Where, UBOOL bLogStatus, UBOOL bRefreshIfLost )
{
	UBOOL bVirtualAddress = XboxTcpIsXNetVirtualAddress( Addr );
	if( !bVirtualAddress && !GXboxTcpSecureTravelHost.Valid )
		return XNET_CONNECT_STATUS_CONNECTED;

	if( GXboxTcpSecureTravelHost.Valid && !bVirtualAddress )
	{
		XboxTcpTranslateSecureTravelHost( Addr, Where, bLogStatus );
		bVirtualAddress = XboxTcpIsXNetVirtualAddress( Addr );
	}

	if( !bVirtualAddress )
		return XNET_CONNECT_STATUS_CONNECTED;

	DWORD Status = XNetGetConnectStatus( Addr );
	if( Status == XNET_CONNECT_STATUS_LOST && bRefreshIfLost && GXboxTcpSecureTravelHost.Valid )
	{
		FString LostAddress = IpString( Addr );
		if( XboxTcpTranslateSecureTravelHost( Addr, Where, 1 ) )
		{
			Status = XNetGetConnectStatus( Addr );
			debugf( NAME_Log, TEXT("XNET secure %s refreshed lost association old=%s new=%s status=%lu"),
				Where ? Where : TEXT("unknown"),
				*LostAddress,
				*IpString(Addr),
				Status );
		}
	}
	INT ConnectResult = 0;
	if( Status == XNET_CONNECT_STATUS_IDLE || Status == XNET_CONNECT_STATUS_LOST )
		ConnectResult = XNetConnect( Addr );

	if( bLogStatus || ConnectResult != 0 || Status == XNET_CONNECT_STATUS_LOST )
	{
		debugf( NAME_Log, TEXT("XNET secure %s addr=%s status=%lu connect=%i"),
			Where ? Where : TEXT("unknown"),
			*IpString(Addr),
			Status,
			ConnectResult );
	}
	return Status;
}

static DWORD XboxTcpEnsureSecurePeerAssociation( in_addr& Addr, const XNADDR* XnAddr, const XNKID* SessionKeyId, const TCHAR* Where, UBOOL bLogStatus, UBOOL bRefreshIfLost )
{
	UBOOL bVirtualAddress = XboxTcpIsXNetVirtualAddress( Addr );
	if( !bVirtualAddress )
	{
		if( bRefreshIfLost && XnAddr && SessionKeyId )
			XboxTcpTranslateSecurePeer( Addr, XnAddr, SessionKeyId, Where, bLogStatus );
		bVirtualAddress = XboxTcpIsXNetVirtualAddress( Addr );
	}
	if( !bVirtualAddress )
		return XNET_CONNECT_STATUS_CONNECTED;

	DWORD Status = XNetGetConnectStatus( Addr );
	if( Status == XNET_CONNECT_STATUS_LOST && bRefreshIfLost && XnAddr && SessionKeyId )
	{
		FString LostAddress = IpString( Addr );
		if( XboxTcpTranslateSecurePeer( Addr, XnAddr, SessionKeyId, Where, 1 ) )
		{
			Status = XNetGetConnectStatus( Addr );
			debugf( NAME_Log, TEXT("XNET secure %s refreshed lost peer old=%s new=%s status=%lu"),
				Where ? Where : TEXT("unknown"),
				*LostAddress,
				*IpString(Addr),
				Status );
		}
	}

	INT ConnectResult = 0;
	if( Status == XNET_CONNECT_STATUS_IDLE || Status == XNET_CONNECT_STATUS_LOST )
		ConnectResult = XNetConnect( Addr );

	if( bLogStatus || ConnectResult != 0 || Status == XNET_CONNECT_STATUS_LOST )
	{
		debugf( NAME_Log, TEXT("XNET secure %s peer addr=%s status=%lu connect=%i"),
			Where ? Where : TEXT("unknown"),
			*IpString(Addr),
			Status,
			ConnectResult );
	}
	return Status;
}

static UBOOL XboxTcpWaitForSecureAssociation( in_addr& Addr, const TCHAR* Where, FLOAT TimeoutSeconds )
{
	UBOOL bVerboseWait = TimeoutSeconds >= 0.49f;
	DWORD Status = XboxTcpEnsureSecureAssociation( Addr, Where, bVerboseWait, 1 );
	if( !XboxTcpIsXNetVirtualAddress(Addr) )
		return 1;

	DOUBLE EndTime = appSeconds() + TimeoutSeconds;
	while( Status == XNET_CONNECT_STATUS_PENDING || Status == XNET_CONNECT_STATUS_IDLE )
	{
		if( Status == XNET_CONNECT_STATUS_IDLE )
			XNetConnect( Addr );
		if( appSeconds() >= EndTime )
			break;
		appSleep( 0.01f );
		Status = XNetGetConnectStatus( Addr );
	}

	if( bVerboseWait || Status != XNET_CONNECT_STATUS_PENDING )
	{
		debugf( NAME_Log, TEXT("XNET secure %s wait-complete addr=%s status=%lu connected=%i"),
			Where ? Where : TEXT("unknown"),
			*IpString(Addr),
			Status,
			Status == XNET_CONNECT_STATUS_CONNECTED ? 1 : 0 );
	}
	return Status == XNET_CONNECT_STATUS_CONNECTED;
}

static UBOOL XboxTcpWaitForSecurePeerAssociation( in_addr& Addr, const XNADDR* XnAddr, const XNKID* SessionKeyId, const TCHAR* Where, FLOAT TimeoutSeconds )
{
	UBOOL bVerboseWait = TimeoutSeconds >= 0.49f;
	DWORD Status = XboxTcpEnsureSecurePeerAssociation( Addr, XnAddr, SessionKeyId, Where, bVerboseWait, 1 );
	if( !XboxTcpIsXNetVirtualAddress(Addr) )
		return 1;

	DOUBLE EndTime = appSeconds() + TimeoutSeconds;
	while( Status == XNET_CONNECT_STATUS_PENDING || Status == XNET_CONNECT_STATUS_IDLE )
	{
		if( Status == XNET_CONNECT_STATUS_IDLE )
			XNetConnect( Addr );
		if( appSeconds() >= EndTime )
			break;
		appSleep( 0.01f );
		Status = XNetGetConnectStatus( Addr );
	}

	if( bVerboseWait || Status != XNET_CONNECT_STATUS_PENDING )
	{
		debugf( NAME_Log, TEXT("XNET secure %s peer wait-complete addr=%s status=%lu connected=%i"),
			Where ? Where : TEXT("unknown"),
			*IpString(Addr),
			Status,
			Status == XNET_CONNECT_STATUS_CONNECTED ? 1 : 0 );
	}
	return Status == XNET_CONNECT_STATUS_CONNECTED;
}

static void XboxTcpConfigureSecureTravelHostFromURL( FURL& ConnectURL )
{
	const TCHAR* SessionIdText   = ConnectURL.GetOption( TEXT("SessionID="),   TEXT("") );
	const TCHAR* ExchangeKeyText = ConnectURL.GetOption( TEXT("ExchangeKey="), TEXT("") );
	const TCHAR* HostAddrText    = ConnectURL.GetOption( TEXT("HostAddr="),    TEXT("") );
	if( !SessionIdText[0] && !ExchangeKeyText[0] && !HostAddrText[0] )
		return;

	XNKID SessionKeyId;
	XNKEY SessionKey;
	XNADDR HostXnAddr;
	appMemzero( &SessionKeyId, sizeof(SessionKeyId) );
	appMemzero( &SessionKey, sizeof(SessionKey) );
	appMemzero( &HostXnAddr, sizeof(HostXnAddr) );

	if( !XboxTcpHexDecode( SessionIdText, (BYTE*)&SessionKeyId, sizeof(SessionKeyId) )
	||  !XboxTcpHexDecode( ExchangeKeyText, (BYTE*)&SessionKey, sizeof(SessionKey) )
	||  !XboxTcpHexDecode( HostAddrText, (BYTE*)&HostXnAddr, sizeof(HostXnAddr) ) )
	{
		debugf( NAME_Log, TEXT("XNET secure URL options invalid sessionLen=%i keyLen=%i hostLen=%i"),
			appStrlen(SessionIdText),
			appStrlen(ExchangeKeyText),
			appStrlen(HostAddrText) );
		return;
	}

	XboxIpDrvSetSecureTravelHost( &HostXnAddr, &SessionKeyId, &SessionKey, 0 );

	in_addr SecureAddr;
	IpSetInt( SecureAddr, 0 );
	if( XboxTcpTranslateSecureTravelHost( SecureAddr, TEXT("url"), 1 ) )
	{
		ConnectURL.Host = IpString( SecureAddr );
		if( ConnectURL.Port <= 0 )
			ConnectURL.Port = 7777;
		debugf( NAME_Log, TEXT("XNET secure URL translated host=%s port=%i"),
			*ConnectURL.Host,
			ConnectURL.Port );
	}
}
#endif

/*-----------------------------------------------------------------------------
	UTcpipConnection.
-----------------------------------------------------------------------------*/

//
// Windows socket class.
//
class DLL_EXPORT_CLASS UTcpipConnection : public UNetConnection
{
	DECLARE_CLASS(UTcpipConnection,UNetConnection,CLASS_Config|CLASS_Transient)
	NO_DEFAULT_CONSTRUCTOR(UTcpipConnection)

	// Variables.
	sockaddr_in		RemoteAddr;
	SOCKET			Socket;
	UBOOL			OpenedLocally;
	FResolveInfo*	ResolveInfo;
	UBOOL			LoggedFirstSend;
#if TARGET_XBOX
	UBOOL			HasSecureRemote;
	XNADDR			SecureRemoteXnAddr;
	XNKID			SecureRemoteKeyId;
#endif

	// Constructors and destructors.
	UTcpipConnection( SOCKET InSocket, UNetDriver* InDriver, sockaddr_in InRemoteAddr, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL )
	:	UNetConnection	( InDriver, InURL )
	,	Socket			( InSocket )
	,	RemoteAddr		( InRemoteAddr )
	,	OpenedLocally	( InOpenedLocally )
	,	ResolveInfo		( NULL )
	,	LoggedFirstSend	( 0 )
#if TARGET_XBOX
	,	HasSecureRemote( 0 )
#endif
	{
		guard(UTcpipConnection::UTcpipConnection);
#if TARGET_XBOX
		appMemzero( &SecureRemoteXnAddr, sizeof(SecureRemoteXnAddr) );
		appMemzero( &SecureRemoteKeyId, sizeof(SecureRemoteKeyId) );
#endif

		// Init the connection.
		State                 = InState;
		MaxPacket			  = WINSOCK_MAX_PACKET;
		PacketOverhead		  = SLIP_HEADER_SIZE;
		InitOut();

		// In connecting, figure out IP address.
		if( InOpenedLocally )
		{
			const TCHAR* s = *InURL.Host;
			for( INT i=0; i<4 && s!=NULL && *s>='0' && *s<='9'; i++ )
			{
				s = appStrchr(s,'.');
				if( s )
					s++;
			}
			if( i==4 && !s )
			{
				// Get numerical address directly.
				IpSetInt(RemoteAddr.sin_addr, inet_addr( appToAnsi(*InURL.Host)));
#if TARGET_XBOX
				if( InURL.HasOption(TEXT("LAN")) )
					XboxTcpEnsureSecureAssociation( RemoteAddr.sin_addr, TEXT("connect-init"), 1, 1 );
#endif
			}
			else
			{
				// Create thread to resolve the address.
				ResolveInfo = new FResolveInfo( *InURL.Host );
			}
		}
		debugf( NAME_Log, TEXT("XNET connection created opened=%i state=%i remote=%s urlHost=%s urlPort=%i lan=%i rate=%i"),
			OpenedLocally ? 1 : 0,
			State,
			*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)),
			*InURL.Host,
			InURL.Port,
			InURL.HasOption(TEXT("LAN")) ? 1 : 0,
			CurrentNetSpeed );

		unguard;
	}

#if TARGET_XBOX
	void SetSecureRemotePeer( const XNADDR* XnAddr, const XNKID* SessionKeyId )
	{
		guard(UTcpipConnection::SetSecureRemotePeer);
		if( !XnAddr || !SessionKeyId )
			return;

		appMemcpy( &SecureRemoteXnAddr, XnAddr, sizeof(SecureRemoteXnAddr) );
		appMemcpy( &SecureRemoteKeyId, SessionKeyId, sizeof(SecureRemoteKeyId) );
		HasSecureRemote = 1;
		debugf( NAME_Log, TEXT("XNET secure peer stored remote=%s"),
			*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)) );
		if( !XboxTcpWaitForSecurePeerAssociation( RemoteAddr.sin_addr, &SecureRemoteXnAddr, &SecureRemoteKeyId, TEXT("accept"), 0.50f ) )
		{
			debugf( NAME_Log, TEXT("XNET secure peer accept wait did not connect remote=%s"),
				*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)) );
		}
		unguard;
	}
#endif

	// UNetConnection interface.
	void LowLevelSend( void* Data, INT Count )
	{
		guard(UTcpipConnection::LowLevelSend);
		if( ResolveInfo )
		{
			// If destination address isn't resolved yet, send nowhere.
			if( !ResolveInfo->Resolved() )
			{
				// Host name still resolving.
				return;
			}
			else if( ResolveInfo->GetError() )
			{
				// Host name resolution just now failed.
				debugf( NAME_Log, TEXT("%s"), ResolveInfo->GetError() );
				Driver->ServerConnection->State = USOCK_Closed;
				delete ResolveInfo;
				ResolveInfo = NULL;
				return;
			}
			else
			{
				// Host name resolution just now succeeded.
				RemoteAddr.sin_addr = ResolveInfo->GetAddr();
				debugf( TEXT("Resolved %s (%s)"), ResolveInfo->GetHostName(), *IpString(ResolveInfo->GetAddr()) );
				delete ResolveInfo;
				ResolveInfo = NULL;
#if TARGET_XBOX
				if( OpenedLocally && URL.HasOption(TEXT("LAN")) )
					XboxTcpEnsureSecureAssociation( RemoteAddr.sin_addr, TEXT("resolve"), 1, 1 );
#endif
			}
		}

#if TARGET_XBOX
		if( HasSecureRemote )
		{
			DWORD SecureStatus = XboxTcpEnsureSecurePeerAssociation(
				RemoteAddr.sin_addr,
				&SecureRemoteXnAddr,
				&SecureRemoteKeyId,
				LoggedFirstSend ? TEXT("peer-send") : TEXT("peer-first-send"),
				!LoggedFirstSend,
				1 );
			if( SecureStatus == XNET_CONNECT_STATUS_LOST
			&& !XboxTcpWaitForSecurePeerAssociation( RemoteAddr.sin_addr, &SecureRemoteXnAddr, &SecureRemoteKeyId, LoggedFirstSend ? TEXT("peer-send-wait") : TEXT("peer-first-send-wait"), LoggedFirstSend ? 0.10f : 0.50f ) )
			{
				if( !LoggedFirstSend )
				{
					debugf( NAME_Log, TEXT("XNET secure peer first send deferred remote=%s"), *IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)) );
					LoggedFirstSend = 1;
				}
				return;
			}
		}
		else if( OpenedLocally && URL.HasOption(TEXT("LAN")) )
		{
			DWORD SecureStatus = XboxTcpEnsureSecureAssociation( RemoteAddr.sin_addr, LoggedFirstSend ? TEXT("send") : TEXT("first-send"), !LoggedFirstSend, 1 );
			if( SecureStatus == XNET_CONNECT_STATUS_LOST
			&& !XboxTcpWaitForSecureAssociation( RemoteAddr.sin_addr, LoggedFirstSend ? TEXT("send-wait") : TEXT("first-send-wait"), LoggedFirstSend ? 0.10f : 0.50f ) )
			{
				if( !LoggedFirstSend )
				{
					debugf( NAME_Log, TEXT("XNET secure first send deferred remote=%s"), *IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)) );
					LoggedFirstSend = 1;
				}
				return;
			}
		}
#endif

		// Send to remote.
		clock(Driver->SendCycles);
		INT Sent = sendto( Socket, (char *)Data, Count, 0, (sockaddr*)&RemoteAddr, sizeof(RemoteAddr) );
		unclock(Driver->SendCycles);
		if( !LoggedFirstSend || Sent==SOCKET_ERROR )
		{
			INT Err = Sent==SOCKET_ERROR ? WSAGetLastError() : 0;
#if TARGET_XBOX
			if( Sent==SOCKET_ERROR
			&& (Err==WSAEHOSTUNREACH || Err==WSAENETUNREACH || Err==WSAENETRESET || Err==WSAECONNRESET) )
			{
				if( HasSecureRemote )
					XboxTcpWaitForSecurePeerAssociation( RemoteAddr.sin_addr, &SecureRemoteXnAddr, &SecureRemoteKeyId, TEXT("peer-send-error"), 0.10f );
				else
					XboxTcpWaitForSecureAssociation( RemoteAddr.sin_addr, TEXT("send-error"), 0.10f );
			}
#endif
			debugf( NAME_Log, TEXT("XNET send %s bytes=%i sent=%i err=%i state=%i remote=%s"),
				LoggedFirstSend ? TEXT("error") : TEXT("first"),
				Count,
				Sent,
				Err,
				State,
				*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)) );
			LoggedFirstSend = 1;
		}

		unguard;
	}
	FString LowLevelGetRemoteAddress()
	{
		guard(UTcpipConnection::LowLevelGetRemoteAddress);
		return IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port));
		unguard;
	}
	FString LowLevelDescribe()
	{
		guard(UTcpipConnection::LowLevelDescribe);
		return FString::Printf
		(
			TEXT("%s %s state: %s"),
			*URL.Host,
			*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)),
				State==USOCK_Pending	?	TEXT("Pending")
			:	State==USOCK_Open		?	TEXT("Open")
			:	State==USOCK_Closed		?	TEXT("Closed")
			:								TEXT("Invalid")
		);
		unguard;
	}
};
IMPLEMENT_CLASS(UTcpipConnection);

/*-----------------------------------------------------------------------------
	UTcpNetDriver.
-----------------------------------------------------------------------------*/

//
// Windows sockets network driver.
//
class DLL_EXPORT_CLASS UTcpNetDriver : public UNetDriver
{
	DECLARE_CLASS(UTcpNetDriver,UNetDriver,CLASS_Transient|CLASS_Config)

	// Variables.
	sockaddr_in	LocalAddr;
	SOCKET		Socket;
#if TARGET_XBOX
	UBOOL		RequireSecurePeers;
	INT			SecureDropCount;
#endif

	// Constructor.
	UTcpNetDriver()
	{
		appMemzero( &LocalAddr, sizeof(LocalAddr) );
		Socket = 0;
#if TARGET_XBOX
		RequireSecurePeers = 0;
		SecureDropCount = 0;
#endif
	}

	// UNetDriver interface.
	UBOOL InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error )
	{
		guard(UTcpNetDriver::InitConnect);
		debugf( NAME_Log, TEXT("XNET InitConnect url=%s host=%s port=%i lan=%i"),
			*ConnectURL.String(1),
			*ConnectURL.Host,
			ConnectURL.Port,
			ConnectURL.HasOption(TEXT("LAN")) ? 1 : 0 );
		if( !Super::InitConnect( InNotify, ConnectURL, Error ) )
			return 0;
		if( !InitBase( 1, InNotify, ConnectURL, Error ) )
		{
			debugf( NAME_Log, TEXT("XNET InitConnect failed: %s"), *Error );
			return 0;
		}
#if TARGET_XBOX
		if( ConnectURL.HasOption(TEXT("LAN")) )
			XboxTcpConfigureSecureTravelHostFromURL( ConnectURL );
#endif

		// Connect to remote.
		sockaddr_in TempAddr;
		TempAddr.sin_family           = AF_INET;
		TempAddr.sin_port             = htons(ConnectURL.Port);
		IpSetBytes(TempAddr.sin_addr, 0, 0, 0, 0);

		// Create new connection.
		ServerConnection = new UTcpipConnection( Socket, this, TempAddr, USOCK_Pending, 1, ConnectURL );
		debugf( NAME_Log, TEXT("XNET Game client local=%s remote=%s rate=%i"),
			*IpString(LocalAddr.sin_addr,ntohs(LocalAddr.sin_port)),
			*IpString(TempAddr.sin_addr,ntohs(TempAddr.sin_port)),
			ServerConnection->CurrentNetSpeed );
		debugf( NAME_DevNet, TEXT("Game client on port %i, rate %i"), ntohs(LocalAddr.sin_port), ServerConnection->CurrentNetSpeed );

		// Create channel zero.
		GetServerConnection()->CreateChannel( CHTYPE_Control, 1, 0 );

		return 1;
		unguard;
	}
	UBOOL UTcpNetDriver::InitListen( FNetworkNotify* InNotify, FURL& LocalURL, FString& Error )
	{
		guard(UTcpNetDriver::InitListen);
		debugf( NAME_Log, TEXT("XNET InitListen url=%s host=%s port=%i lan=%i"),
			*LocalURL.String(1),
			*LocalURL.Host,
			LocalURL.Port,
			LocalURL.HasOption(TEXT("LAN")) ? 1 : 0 );
		if( !Super::InitListen( InNotify, LocalURL, Error ) )
			return 0;
		if( !InitBase( 0, InNotify, LocalURL, Error ) )
		{
			debugf( NAME_Log, TEXT("XNET InitListen failed: %s"), *Error );
			return 0;
		}
#if TARGET_XBOX
		RequireSecurePeers = LocalURL.HasOption(TEXT("LAN"));
#endif

		// Update result URL.
		LocalURL.Host = IpString(LocalAddr.sin_addr);
		LocalURL.Port = ntohs( LocalAddr.sin_port );
		debugf( NAME_Log, TEXT("XNET listen ready local=%s url=%s"),
			*IpString(LocalAddr.sin_addr,ntohs(LocalAddr.sin_port)),
			*LocalURL.String(1) );
		debugf( NAME_DevNet, TEXT("TcpNetDriver on port %i"), LocalURL.Port );

		return 1;
		unguard;
	}
	void TickDispatch( FLOAT DeltaTime )
	{
		guard(UTcpNetDriver::TickDispatch);
		Super::TickDispatch( DeltaTime );

		// Process all incoming packets.
		BYTE Data[NETWORK_MAX_PACKET];
		sockaddr_in FromAddr;
		for( ; ; )
		{
			// Get data, if any.
			clock(RecvCycles);
			fd_set ReadSet;
			FD_ZERO( &ReadSet );
			FD_SET( Socket, &ReadSet );
			TIMEVAL Wait;
			Wait.tv_sec=0;
			Wait.tv_usec=0;
			INT Result=select(Socket+1,&ReadSet,NULL,NULL,&Wait);
			if( Result==0 || Result==SOCKET_ERROR )
				break;
			INT FromSize = sizeof(FromAddr);
			INT Size = recvfrom( Socket, (char*)Data, sizeof(Data), 0, (sockaddr*)&FromAddr, GCC_OPT_INT_CAST &FromSize );
			unclock(RecvCycles);

			// Handle result.
			if( Size==SOCKET_ERROR )
			{
				if( WSAGetLastError()!=WSAEWOULDBLOCK )
				{
					static UBOOL FirstError=1;
					if( FirstError )
						debugf( TEXT("UDP recvfrom error: %i"), WSAGetLastError() );
					FirstError = 0;
				}
				break;
			}

			// Figure out which socket the received data came from.
			UTcpipConnection* Connection = NULL;
			if( GetServerConnection() && IpMatches(GetServerConnection()->RemoteAddr,FromAddr) )
				Connection = GetServerConnection();
			for( INT i=0; i<ClientConnections.Num() && !Connection; i++ )
				if( IpMatches( ((UTcpipConnection*)ClientConnections(i))->RemoteAddr, FromAddr ) )
					Connection = (UTcpipConnection*)ClientConnections(i);

			// If we didn't find a client connection, maybe create a new one.
			if( !Connection && Notify->NotifyAcceptingConnection()==ACCEPTC_Accept )
			{
#if TARGET_XBOX
				UBOOL bHasSecurePeer = 0;
				XNADDR SecureXnAddr;
				appMemzero( &SecureXnAddr, sizeof(SecureXnAddr) );
				if( RequireSecurePeers )
				{
					INT SecureResult = XNetInAddrToXnAddr( FromAddr.sin_addr, &SecureXnAddr, NULL );
					if( SecureResult != 0 )
					{
						if( SecureDropCount < 8 )
						{
							debugf( NAME_Log, TEXT("XNET dropped non-secure LAN packet remote=%s bytes=%i result=%i"),
								*IpString(FromAddr.sin_addr,ntohs(FromAddr.sin_port)),
								Size,
								SecureResult );
						}
						SecureDropCount++;
						continue;
					}
					bHasSecurePeer = 1;
				}
#endif
				debugf( NAME_Log, TEXT("XNET accepted remote=%s bytes=%i clients=%i"),
					*IpString(FromAddr.sin_addr,ntohs(FromAddr.sin_port)),
					Size,
					ClientConnections.Num() );
				Connection = new UTcpipConnection( Socket, this, FromAddr, USOCK_Open, 0, FURL() );
#if TARGET_XBOX
				if( bHasSecurePeer )
				{
					debugf( NAME_Log, TEXT("XNET secure peer accepted direct remote=%s status=%lu"),
						*IpString(FromAddr.sin_addr,ntohs(FromAddr.sin_port)),
						XNetGetConnectStatus(FromAddr.sin_addr) );
				}
#endif
				Connection->URL.Host = IpString(FromAddr.sin_addr);
				Notify->NotifyAcceptedConnection( Connection );
				ClientConnections.AddItem( Connection );
			}

			// Send the packet to the connection for processing.
			if( Connection )
				Connection->ReceivedRawPacket( Data, Size );
		}
		unguard;
	}
	FString LowLevelGetNetworkNumber()
	{
		guard(UTcpNetDriver::LowLevelGetNetworkNumber);
		return IpString(LocalAddr.sin_addr);
		unguard;
	}
	void LowLevelDestroy()
	{
		guard(UTcpNetDriver::LowLevelDestroy);

		// Close the socket.
		if( Socket )
		{
			if( closesocket(Socket) )
				debugf( NAME_Exit, TEXT("WinSock closesocket error (%i)"), WSAGetLastError() );
			Socket=NULL;
			debugf( NAME_Exit, TEXT("WinSock shut down") );
		}
#if TARGET_XBOX
		XboxIpDrvClearSecureTravelHost();
#endif

		unguard;
	}

	// UTcpNetDriver interface.
	UBOOL InitBase( UBOOL Connect, FNetworkNotify* InNotify, FURL& URL, FString& Error )
	{
		guard(UTcpNetDriver::UTcpNetDriver);
		if( URL.HasOption(TEXT("LAN")) && URL.Port<=0 )
			URL.Port = 7777;
		debugf( NAME_Log, TEXT("XNET InitBase connect=%i requestedUrl=%s requestedPort=%i"),
			Connect ? 1 : 0,
			*URL.String(1),
			URL.Port );

		// Init WSA.
		if( !InitSockets( Error ) )
		{
			debugf( NAME_Log, TEXT("XNET InitBase socket init failed: %s"), *Error );
			return 0;
		}

		// Create UDP socket and enable broadcasting.
		Socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if( Socket == INVALID_SOCKET )
		{
			Socket = 0;
			Error = FString::Printf( TEXT("WinSock: socket failed (%i)"), SocketError() );
			debugf( NAME_Log, TEXT("XNET InitBase socket failed: %s"), *Error );
			return 0;
		}
		UBOOL TrueBuffer=1;
		if( setsockopt( Socket, SOL_SOCKET, SO_BROADCAST, (char*)&TrueBuffer, sizeof(TrueBuffer) ) )
		{
			Error = FString::Printf( TEXT("%s: setsockopt SO_BROADCAST failed (%i)"), SOCKET_API, SocketError() );
			closesocket( Socket );
			Socket = 0;
			return 0;
		}
		UBOOL Yes=1;
		if( setsockopt( Socket, SOL_SOCKET, SO_REUSEADDR, (char*)&Yes, sizeof(Yes) ) )
			debugf(TEXT("setsockopt with SO_REUSEADDR failed"));

		// Increase socket queue size, because we are polling rather than threading
		// and thus we rely on Windows Sockets to buffer a lot of data on the server.
		INT RecvSize = Connect ? 0x8000 : 0x20000, SizeSize=sizeof(RecvSize);
		INT SendSize = Connect ? 0x8000 : 0x20000;
		setsockopt( Socket, SOL_SOCKET, SO_RCVBUF, (char*)&RecvSize, SizeSize );
		getsockopt( Socket, SOL_SOCKET, SO_RCVBUF, (char*)&RecvSize, GCC_OPT_INT_CAST &SizeSize );
		setsockopt( Socket, SOL_SOCKET, SO_SNDBUF, (char*)&SendSize, SizeSize );
		getsockopt( Socket, SOL_SOCKET, SO_SNDBUF, (char*)&SendSize, GCC_OPT_INT_CAST &SizeSize );
		debugf( NAME_Init, TEXT("%s: Socket queue %i / %i"), SOCKET_API, RecvSize, SendSize );

		// Bind socket to our port.
		LocalAddr.sin_family    = AF_INET;
		LocalAddr.sin_addr		= getlocalbindaddr( *GLog );
		LocalAddr.sin_port      = 0;
		UBOOL HardcodedPort     = 0;
		UBOOL ForceRequestedPort = !Connect && URL.HasOption(TEXT("LAN"));
		if( !Connect )
		{
			// Init as a server.
			HardcodedPort = Parse( appCmdLine(), TEXT("PORT="), URL.Port );
			LocalAddr.sin_port = htons(URL.Port);
		}
		INT AttemptPort = ntohs(LocalAddr.sin_port);
		INT boundport   = bindnextport( Socket, &LocalAddr, (HardcodedPort || ForceRequestedPort) ? 1 : 20, 1 );
		if( boundport==0 )
		{
			Error = FString::Printf( TEXT("%s: binding to port %i failed (%i)"), SOCKET_API, AttemptPort, SocketError() );
			debugf( NAME_Log, TEXT("XNET bind failed connect=%i requested=%i hardcoded=%i forceLan=%i error=%s"),
				Connect ? 1 : 0,
				AttemptPort,
				HardcodedPort ? 1 : 0,
				ForceRequestedPort ? 1 : 0,
				*Error );
			closesocket( Socket );
			Socket = 0;
			return 0;
		}
		LocalAddr.sin_port = htons((u_short)boundport);
		debugf( NAME_Log, TEXT("XNET bound connect=%i local=%s requested=%i hardcoded=%i forceLan=%i"),
			Connect ? 1 : 0,
			*IpString(LocalAddr.sin_addr,ntohs(LocalAddr.sin_port)),
			AttemptPort,
			HardcodedPort ? 1 : 0,
			ForceRequestedPort ? 1 : 0 );
		DWORD NoBlock=1;
		if( ioctlsocket( Socket, FIONBIO, &NoBlock ) )
		{
			Error = FString::Printf( TEXT("%s: ioctlsocket failed (%i)"), SOCKET_API, SocketError() );
			debugf( NAME_Log, TEXT("XNET ioctlsocket failed: %s"), *Error );
			closesocket( Socket );
			Socket = 0;
			return 0;
		}

		// Success.
		return 1;
		unguard;
	}
	UTcpipConnection* GetServerConnection() {return (UTcpipConnection*)ServerConnection;}
};
IMPLEMENT_CLASS(UTcpNetDriver);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
