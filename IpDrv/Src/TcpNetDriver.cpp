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
static const FLOAT GXboxTcpSecureRetryIntervalSeconds = 1.0f;
static const FLOAT GXboxTcpSecureConnectTimeoutSeconds = 15.0f;

struct FXboxTcpSecureAddressRef
{
	in_addr Addr;
	INT RefCount;
};

// A current driver and its pending replacement can temporarily use the same
// XNet virtual address.  XNetUnregisterInAddr is process-wide, so one
// connection must not invalidate that address while the other still owns it.
static FXboxTcpSecureAddressRef GXboxTcpSecureAddressRefs[16];

static UBOOL XboxTcpIsXNetVirtualAddress( in_addr Addr )
{
	DWORD Ip = 0;
	IpGetInt( Addr, Ip );
	if( Ip == 0 || Ip == INADDR_NONE || Ip == INADDR_BROADCAST )
		return 0;

	BYTE* B = (BYTE*)&Ip;
	return B[0] == 0;
}

static UBOOL XboxTcpAddressesMatch( in_addr A, in_addr B )
{
	DWORD AValue = 0;
	DWORD BValue = 0;
	IpGetInt( A, AValue );
	IpGetInt( B, BValue );
	return AValue == BValue;
}

static void XboxTcpAcquireSecureAddress( in_addr Addr, const TCHAR* Where )
{
	if( !XboxTcpIsXNetVirtualAddress(Addr) )
		return;

	INT FreeIndex = INDEX_NONE;
	for( INT i=0; i<ARRAY_COUNT(GXboxTcpSecureAddressRefs); i++ )
	{
		FXboxTcpSecureAddressRef& Ref = GXboxTcpSecureAddressRefs[i];
		if( Ref.RefCount && XboxTcpAddressesMatch(Ref.Addr, Addr) )
		{
			Ref.RefCount++;
			debugf( NAME_Log, TEXT("XNET secure %s address retained addr=%s refs=%i"),
				Where ? Where : TEXT("unknown"), *IpString(Addr), Ref.RefCount );
			return;
		}
		if( !Ref.RefCount && FreeIndex == INDEX_NONE )
			FreeIndex = i;
	}

	if( FreeIndex != INDEX_NONE )
	{
		GXboxTcpSecureAddressRefs[FreeIndex].Addr = Addr;
		GXboxTcpSecureAddressRefs[FreeIndex].RefCount = 1;
		debugf( NAME_Log, TEXT("XNET secure %s address acquired addr=%s refs=1"),
			Where ? Where : TEXT("unknown"), *IpString(Addr) );
	}
	else
	{
		debugf( NAME_Warning, TEXT("XNET secure address ownership table full addr=%s"), *IpString(Addr) );
	}
}

static INT XboxTcpReleaseSecureAddress( in_addr Addr, const TCHAR* Where, INT* RemainingRefs )
{
	if( RemainingRefs )
		*RemainingRefs = 0;
	if( !XboxTcpIsXNetVirtualAddress(Addr) )
		return 0;

	for( INT i=0; i<ARRAY_COUNT(GXboxTcpSecureAddressRefs); i++ )
	{
		FXboxTcpSecureAddressRef& Ref = GXboxTcpSecureAddressRefs[i];
		if( Ref.RefCount && XboxTcpAddressesMatch(Ref.Addr, Addr) )
		{
			Ref.RefCount--;
			if( RemainingRefs )
				*RemainingRefs = Ref.RefCount;
			if( Ref.RefCount )
			{
				debugf( NAME_Log, TEXT("XNET secure %s address released addr=%s refs=%i unregister=deferred"),
					Where ? Where : TEXT("unknown"), *IpString(Addr), Ref.RefCount );
				return 0;
			}
			IpSetInt( Ref.Addr, 0 );
			return XNetUnregisterInAddr( Addr );
		}
	}

	debugf( NAME_Warning, TEXT("XNET secure %s address release missing owner addr=%s"),
		Where ? Where : TEXT("unknown"), *IpString(Addr) );
	return XNetUnregisterInAddr( Addr );
}

static UBOOL XboxTcpSameTravelHost( const XNADDR* XnAddr, const XNKID* SessionKeyId, const XNKEY* SessionKey )
{
	return GXboxTcpSecureTravelHost.Valid
	&&  XnAddr
	&&  SessionKeyId
	&&  SessionKey
	&&  appMemcmp( &GXboxTcpSecureTravelHost.XnAddr, XnAddr, sizeof(*XnAddr) ) == 0
	&&  appMemcmp( &GXboxTcpSecureTravelHost.SessionKeyId, SessionKeyId, sizeof(*SessionKeyId) ) == 0
	&&  appMemcmp( &GXboxTcpSecureTravelHost.SessionKey, SessionKey, sizeof(*SessionKey) ) == 0;
}

static void XboxTcpForgetMatchingTravelAddress( in_addr Addr )
{
	if( GXboxTcpSecureTravelHost.Valid
	&&  XboxTcpIsXNetVirtualAddress(GXboxTcpSecureTravelHost.SecureAddr)
	&&  XboxTcpAddressesMatch(Addr, GXboxTcpSecureTravelHost.SecureAddr) )
	{
		IpSetInt( GXboxTcpSecureTravelHost.SecureAddr, 0 );
	}
}

extern "C" void XboxIpDrvClearSecureTravelHost()
{
	if( GXboxTcpSecureTravelHost.Valid )
	{
		debugf( NAME_Log, TEXT("XNET secure travel host cleared addr=%s"), *IpString(GXboxTcpSecureTravelHost.SecureAddr) );
		if( XboxTcpIsXNetVirtualAddress(GXboxTcpSecureTravelHost.SecureAddr) )
		{
			INT AddressResult = XNetUnregisterInAddr( GXboxTcpSecureTravelHost.SecureAddr );
			debugf( NAME_Log, TEXT("XNET secure travel host address unregister result=%i"), AddressResult );
			IpSetInt( GXboxTcpSecureTravelHost.SecureAddr, 0 );
		}
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

	// UC2004 keeps both the current and pending remote registrations alive during
	// travel.  Our connection objects already retain their own peer identity and
	// translated address, so do not tear down the shared handoff when the pending
	// driver presents the same session again.
	if( XboxTcpSameTravelHost(XnAddr, SessionKeyId, SessionKey) )
	{
		in_addr Preferred;
		IpSetInt( Preferred, PreferredAddress );
		if( XboxTcpIsXNetVirtualAddress(Preferred) )
		{
			if( XboxTcpIsXNetVirtualAddress(GXboxTcpSecureTravelHost.SecureAddr)
			&& !XboxTcpAddressesMatch(GXboxTcpSecureTravelHost.SecureAddr, Preferred) )
			{
				INT AddressResult = XNetUnregisterInAddr( GXboxTcpSecureTravelHost.SecureAddr );
				debugf( NAME_Log, TEXT("XNET secure replaced unclaimed travel address result=%i"), AddressResult );
			}
			GXboxTcpSecureTravelHost.SecureAddr = Preferred;
		}
		debugf( NAME_Log, TEXT("XNET secure travel host retained preferred=%s hasPreferred=%i"),
			*IpString(GXboxTcpSecureTravelHost.SecureAddr),
			XboxTcpIsXNetVirtualAddress(GXboxTcpSecureTravelHost.SecureAddr) ? 1 : 0 );
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
		INT UnregisterResult = XNetUnregisterInAddr( Addr );
		XboxTcpForgetMatchingTravelAddress( Addr );
		IpSetInt( Addr, 0 );
		debugf( NAME_Log, TEXT("XNET secure %s released lost association old=%s result=%i"),
			Where ? Where : TEXT("unknown"),
			*LostAddress,
			UnregisterResult );
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

	in_addr SecureAddr = GXboxTcpSecureTravelHost.SecureAddr;
	if( XboxTcpIsXNetVirtualAddress(SecureAddr)
	||  XboxTcpTranslateSecureTravelHost(SecureAddr, TEXT("url"), 1) )
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
	DOUBLE			OpenedTime;
#if TARGET_XBOX
	UBOOL			HasSecureRemote;
	XNADDR			SecureRemoteXnAddr;
	XNKID			SecureRemoteKeyId;
	UBOOL			OwnsSecureAddress;
	UBOOL			SecureDestroying;
	DOUBLE			SecureAttemptStartTime;
	DOUBLE			SecureNextRetryTime;
	INT			SecureRetryCount;
#endif

	// Constructors and destructors.
	UTcpipConnection( SOCKET InSocket, UNetDriver* InDriver, sockaddr_in InRemoteAddr, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL )
	:	UNetConnection	( InDriver, InURL )
	,	Socket			( InSocket )
	,	RemoteAddr		( InRemoteAddr )
	,	OpenedLocally	( InOpenedLocally )
	,	ResolveInfo		( NULL )
	,	LoggedFirstSend	( 0 )
	,	OpenedTime		( appSeconds() )
#if TARGET_XBOX
	,	HasSecureRemote( 0 )
	,	OwnsSecureAddress( 0 )
	,	SecureDestroying( 0 )
	,	SecureAttemptStartTime( -1.0f )
	,	SecureNextRetryTime( 0.0f )
	,	SecureRetryCount( 0 )
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
#if TARGET_XBOX
		// UC2004 accounts for the 16-byte XNet UDP envelope in addition to the
		// ordinary IP/UDP headers.  Accurate overhead keeps the Unreal rate
		// limiter from overfilling the encrypted transport during a long session.
		PacketOverhead       = UDP_HEADER_SIZE + 16;
#endif
		InitOut();
#if TARGET_XBOX
		// UObject construction/config loading can leave the inherited network clocks
		// carrying the class-default sentinel on Xbox.  A pending secure connection
		// must start its timeout window at the driver's current time, not at that
		// stale value (observed as an immediate 2^24-second timeout in xemu).
		LastReceiveTime = Driver ? Driver->Time : 0.0;
		LastSendTime = LastReceiveTime;
		LastTickTime = LastReceiveTime;
		StatUpdateTime = LastReceiveTime;
		SecureAttemptStartTime = -1.0;
		SecureNextRetryTime = 0.0;
#endif

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
				{
					if( GXboxTcpSecureTravelHost.Valid )
					{
						appMemcpy( &SecureRemoteXnAddr, &GXboxTcpSecureTravelHost.XnAddr, sizeof(SecureRemoteXnAddr) );
						appMemcpy( &SecureRemoteKeyId, &GXboxTcpSecureTravelHost.SessionKeyId, sizeof(SecureRemoteKeyId) );
						HasSecureRemote = 1;
					}
					XboxTcpEnsureSecureAssociation( RemoteAddr.sin_addr, TEXT("connect-init"), 1, 1 );
					OwnsSecureAddress = XboxTcpIsXNetVirtualAddress( RemoteAddr.sin_addr );
					if( OwnsSecureAddress )
					{
						XboxTcpAcquireSecureAddress( RemoteAddr.sin_addr, TEXT("connect-init") );
						XboxTcpForgetMatchingTravelAddress( RemoteAddr.sin_addr );
					}
				}
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
	void ReleaseSecureAddress( const TCHAR* Where, UBOOL bClearRemote )
	{
		guard(UTcpipConnection::ReleaseSecureAddress);
		if( OwnsSecureAddress && XboxTcpIsXNetVirtualAddress(RemoteAddr.sin_addr) )
		{
			FString OldAddress = IpString( RemoteAddr.sin_addr );
			INT RemainingRefs = 0;
			INT Result = XboxTcpReleaseSecureAddress( RemoteAddr.sin_addr, Where, &RemainingRefs );
			XboxTcpForgetMatchingTravelAddress( RemoteAddr.sin_addr );
			debugf( NAME_Log, TEXT("XNET secure %s connection address released addr=%s result=%i refs=%i retries=%i"),
				Where ? Where : TEXT("unknown"),
				*OldAddress,
				Result,
				RemainingRefs,
				SecureRetryCount );
		}
		OwnsSecureAddress = 0;
		if( bClearRemote )
			IpSetInt( RemoteAddr.sin_addr, 0 );
		unguard;
	}

	UBOOL PrepareSecureSend( const TCHAR* Where )
	{
		guard(UTcpipConnection::PrepareSecureSend);
		if( SecureDestroying )
			return 0;
		if( !HasSecureRemote )
			return 1;

		DOUBLE Now = Driver ? Driver->Time : appSeconds();

		DWORD Status = XboxTcpIsXNetVirtualAddress(RemoteAddr.sin_addr)
			? XNetGetConnectStatus( RemoteAddr.sin_addr )
			: XNET_CONNECT_STATUS_IDLE;
		if( Status == XNET_CONNECT_STATUS_CONNECTED )
		{
			SecureAttemptStartTime = -1.0f;
			SecureNextRetryTime = 0.0f;
			SecureRetryCount = 0;
			return 1;
		}

		if( SecureAttemptStartTime < 0.0f )
			SecureAttemptStartTime = Now;
		if( Now - SecureAttemptStartTime >= GXboxTcpSecureConnectTimeoutSeconds )
		{
			debugf( NAME_Log, TEXT("XNET secure association timed out addr=%s status=%lu elapsed=%.2f retries=%i"),
				*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)),
				Status,
				Now - SecureAttemptStartTime,
				SecureRetryCount );
			State = USOCK_Closed;
			return 0;
		}

		if( !XboxTcpIsXNetVirtualAddress(RemoteAddr.sin_addr) )
		{
			if( Now < SecureNextRetryTime )
				return 0;
			if( GXboxTcpSecureTravelHost.Valid )
				XboxTcpEnsureTravelHostKey( Where );
			if( !XboxTcpTranslateSecurePeer( RemoteAddr.sin_addr, &SecureRemoteXnAddr, &SecureRemoteKeyId, Where, 1 ) )
			{
				SecureNextRetryTime = Now + GXboxTcpSecureRetryIntervalSeconds;
				return 0;
			}
			OwnsSecureAddress = 1;
			XboxTcpAcquireSecureAddress( RemoteAddr.sin_addr, Where );
			Status = XNetGetConnectStatus( RemoteAddr.sin_addr );
			if( Status == XNET_CONNECT_STATUS_CONNECTED )
				return 1;
		}

		if( Status == XNET_CONNECT_STATUS_PENDING || Now < SecureNextRetryTime )
			return 0;

		if( Status == XNET_CONNECT_STATUS_LOST )
		{
			ReleaseSecureAddress( TEXT("retry"), 1 );
			if( GXboxTcpSecureTravelHost.Valid )
				XboxTcpEnsureTravelHostKey( Where );
			if( !XboxTcpTranslateSecurePeer( RemoteAddr.sin_addr, &SecureRemoteXnAddr, &SecureRemoteKeyId, Where, 1 ) )
			{
				SecureNextRetryTime = Now + GXboxTcpSecureRetryIntervalSeconds;
				return 0;
			}
			OwnsSecureAddress = 1;
			XboxTcpAcquireSecureAddress( RemoteAddr.sin_addr, Where );
			Status = XNetGetConnectStatus( RemoteAddr.sin_addr );
			if( Status == XNET_CONNECT_STATUS_CONNECTED )
				return 1;
		}

		INT ConnectResult = XNetConnect( RemoteAddr.sin_addr );
		SecureRetryCount++;
		SecureNextRetryTime = Now + GXboxTcpSecureRetryIntervalSeconds;
		debugf( NAME_Log, TEXT("XNET secure %s retry addr=%s status=%lu connect=%i attempt=%i elapsed=%.2f"),
			Where ? Where : TEXT("unknown"),
			*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)),
			Status,
			ConnectResult,
			SecureRetryCount,
			Now - SecureAttemptStartTime );
		return 0;
		unguard;
	}

	void SetSecureRemotePeer( const XNADDR* XnAddr, const XNKID* SessionKeyId )
	{
		guard(UTcpipConnection::SetSecureRemotePeer);
		if( !XnAddr || !SessionKeyId )
			return;

		appMemcpy( &SecureRemoteXnAddr, XnAddr, sizeof(SecureRemoteXnAddr) );
		appMemcpy( &SecureRemoteKeyId, SessionKeyId, sizeof(SecureRemoteKeyId) );
		HasSecureRemote = 1;
		UBOOL bAlreadyOwned = OwnsSecureAddress;
		OwnsSecureAddress = XboxTcpIsXNetVirtualAddress( RemoteAddr.sin_addr );
		if( OwnsSecureAddress && !bAlreadyOwned )
			XboxTcpAcquireSecureAddress( RemoteAddr.sin_addr, TEXT("accepted") );
		if( OwnsSecureAddress )
			XboxTcpForgetMatchingTravelAddress( RemoteAddr.sin_addr );
		debugf( NAME_Log, TEXT("XNET secure peer stored remote=%s"),
			*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)) );
		unguard;
	}
#endif

	void Destroy()
	{
		guard(UTcpipConnection::Destroy);
#if TARGET_XBOX
		SecureDestroying = 1;
		ReleaseSecureAddress( TEXT("destroy"), 0 );
#endif
		Super::Destroy();
		unguard;
	}

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
			if( !PrepareSecureSend( LoggedFirstSend ? TEXT("send") : TEXT("first-send") ) )
				return;
		}
		else if( OpenedLocally && URL.HasOption(TEXT("LAN")) )
		{
			// A LAN URL without secure host data is a legacy/plain-IP path.
			// Secure LAN connections always set HasSecureRemote in the constructor.
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
				SecureNextRetryTime = 0.0f;
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
	INT			SecureConnectionLimitDropCount;
#endif

	// Constructor.
	UTcpNetDriver()
	{
		appMemzero( &LocalAddr, sizeof(LocalAddr) );
		Socket = 0;
#if TARGET_XBOX
		RequireSecurePeers = 0;
		SecureDropCount = 0;
		SecureConnectionLimitDropCount = 0;
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
				XNKID SecureKeyId;
				appMemzero( &SecureXnAddr, sizeof(SecureXnAddr) );
				appMemzero( &SecureKeyId, sizeof(SecureKeyId) );
				if( RequireSecurePeers )
				{
					INT SecureResult = XNetInAddrToXnAddr( FromAddr.sin_addr, &SecureXnAddr, &SecureKeyId );
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
				// UC2004 refuses more than five recent connections from one address.
				// This prevents delayed packets from obsolete source ports from creating
				// a pile of parallel connections after reconnect or map travel.
				INT SameAddressCount = 0;
				for( INT RecentIndex=0; RecentIndex<ClientConnections.Num(); RecentIndex++ )
				{
					UTcpipConnection* Recent = (UTcpipConnection*)ClientConnections(RecentIndex);
					if( Recent
					&&  XboxTcpAddressesMatch(Recent->RemoteAddr.sin_addr, FromAddr.sin_addr)
					&&  Recent->OpenedTime > appSeconds() - 60.0 )
					{
						SameAddressCount++;
					}
				}
				if( SameAddressCount >= 5 )
				{
					if( SecureConnectionLimitDropCount < 8 )
						debugf( NAME_Log, TEXT("XNET dropped excess recent connection remote=%s recent=%i"),
							*IpString(FromAddr.sin_addr,ntohs(FromAddr.sin_port)), SameAddressCount );
					SecureConnectionLimitDropCount++;
					continue;
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
					Connection->SetSecureRemotePeer( &SecureXnAddr, &SecureKeyId );
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
		// The secure travel host is a handoff shared by current and pending net
		// drivers.  Connections release their own translated addresses above; the
		// System Link lifecycle owner clears the handoff only after the last driver
		// is gone.  This mirrors UC2004's two-slot remote-session lifetime.
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
