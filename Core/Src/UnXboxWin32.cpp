// UnXboxWin32.cpp
// Xbox replacement for UnVcWin32.cpp -- no Windows GUI headers.
// Also contains CRT intrinsic stubs (must be in Core.lib for proper link order).

#include "CorePrivate.h"

// ══════════════════════════════════════════════════════════════════════════
// USystem
// ══════════════════════════════════════════════════════════════════════════
IMPLEMENT_CLASS(USystem);

USystem::USystem()
{
}

void USystem::StaticConstructor()
{
	new(GetClass(),TEXT("PurgeCacheDays"), RF_Public) UIntProperty(CPP_PROPERTY(PurgeCacheDays), TEXT("Options"), CPF_Config);
	new(GetClass(),TEXT("SavePath"),       RF_Public) UStrProperty(CPP_PROPERTY(SavePath),       TEXT("Options"), CPF_Config);
	new(GetClass(),TEXT("CachePath"),      RF_Public) UStrProperty(CPP_PROPERTY(CachePath),      TEXT("Options"), CPF_Config);
	new(GetClass(),TEXT("CacheExt"),       RF_Public) UStrProperty(CPP_PROPERTY(CacheExt),       TEXT("Options"), CPF_Config);
	UArrayProperty* A;
	A = new(GetClass(),TEXT("Path"),       RF_Public) UArrayProperty(CPP_PROPERTY(Paths),    TEXT("Options"), CPF_Config);
	A->Inner = new(A, TEXT("StrProperty"), RF_Public) UStrProperty;
	A = new(GetClass(),TEXT("Suppress"),   RF_Public) UArrayProperty(CPP_PROPERTY(Suppress), TEXT("Options"), CPF_Config);
	A->Inner = new(A, TEXT("NameProperty"),RF_Public) UNameProperty;
}

UBOOL USystem::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	return 0;
}

// ══════════════════════════════════════════════════════════════════════════
// Platform init/exit
// ══════════════════════════════════════════════════════════════════════════
void appPlatformPreInit()  {}
void appPlatformInit()
{
	GSys = new(UObject::GetTransientPackage(), TEXT("System")) USystem;
}
void appPlatformPreExit()  {}
void appPlatformExit()     {}

// ══════════════════════════════════════════════════════════════════════════
// Platform functions
// ══════════════════════════════════════════════════════════════════════════
const TCHAR* appBaseDir()  { return TEXT("E:\\UT99\\"); }

DOUBLE appSeconds()        { return (DOUBLE)GetTickCount() * 0.001; }

void appSleep( FLOAT Seconds ) { Sleep( (DWORD)(Seconds * 1000.0f) ); }

void appSystemTime( INT& Year, INT& Month, INT& DayOfWeek, INT& Day,
                    INT& Hour, INT& Min, INT& Sec, INT& MSec )
{
	SYSTEMTIME ST;
	GetSystemTime( &ST );
	Year=ST.wYear; Month=ST.wMonth; DayOfWeek=ST.wDayOfWeek; Day=ST.wDay;
	Hour=ST.wHour; Min=ST.wMinute; Sec=ST.wSecond; MSec=ST.wMilliseconds;
}

void appRequestExit( UBOOL bForce )
{
	debugf( TEXT("appRequestExit(%i)"), bForce );
	GIsRequestingExit = 1;
	if( bForce ) DebugBreak();
}

const TCHAR* appGetSystemErrorMessage( INT Error )
{
	static TCHAR Msg[1024];
	appSprintf( Msg, TEXT("Error %i"), Error );
	return Msg;
}

const TCHAR* appClipboard()                       { return TEXT(""); }
void         appClipboard( const TCHAR* Str )     {}
void         appClipboardCopy( const TCHAR* Str ) {}
FString      appClipboardPaste()                  { return FString(TEXT("")); }
void         appCleanFileCache()                  {}

void  appLaunchURL( const TCHAR* URL, const TCHAR* Parms, FString* Error )
	{ if(Error) *Error=TEXT("Not supported on Xbox"); }
void  appCreateProc( const TCHAR* URL, const TCHAR* Parms ) {}
UBOOL appGetProcReturnCode( void* ProcHandle, INT* ReturnCode ) { return 0; }

const TCHAR* appComputerName() { return TEXT("XBOX"); }
const TCHAR* appUserName()     { return TEXT("Player"); }

void* appGetDllHandle( const TCHAR* Filename )              { return NULL; }
void  appFreeDllHandle( void* DllHandle )                   {}
void* appGetDllExport( void* DllHandle, const TCHAR* ProcName ) { return NULL; }

FGuid appCreateGuid()
{
	DWORD T = GetTickCount();
	return FGuid( T, T^0xDEADBEEF, T^0xCAFEBABE, T^0x12345678 );
}

DWORD appCycles() { return GetTickCount(); }
