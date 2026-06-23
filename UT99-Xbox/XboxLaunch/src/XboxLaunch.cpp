// XboxLaunch.cpp
// Entry point for UT99 on Xbox.

#include "XboxLaunchPrivate.h"
#include "FConfigCacheIni.h"

// InitEngine and MainLoop live in UT99Engine.lib.
// With ENGINE_API empty these have plain (non-decorated) linkage.
UEngine* InitEngine();
void     MainLoop( UEngine* Engine );

// Defined in EngineForceLinks.cpp — forces VS2005 linker to include all Engine
// class .obj files from UT99Engine.lib so their global constructors run and
// every native class is registered before appInit() calls ProcessRegistrants().
void ForceEngineClassLinks();

// ── Static-lib GPackage definitions ──────────────────────────────────────
// In a DLL build, each DLL's IMPLEMENT_PACKAGE defines its own GPackage.
// In our static-lib build, IMPLEMENT_PACKAGE only declares (extern).
// We define ALL package name arrays here so they're always linked in.
// The #define GPackage GPackage_Xxx in each lib's forced include ensures
// IMPLEMENT_CLASS references the correct symbol.
extern "C" TCHAR GPackage[64]           = TEXT("UnrealTournament");
extern "C" TCHAR GPackage_Core[64]      = TEXT("Core");
extern "C" TCHAR GPackage_Engine[64]    = TEXT("Engine");
extern "C" TCHAR GPackage_Render[64]    = TEXT("Render");
extern "C" TCHAR GPackage_Fire[64]      = TEXT("Fire");
extern "C" TCHAR GPackage_IpDrv[64]     = TEXT("IpDrv");
extern "C" TCHAR GPackage_XboxDrv[64]   = TEXT("XboxDrv");
extern "C" TCHAR GPackage_XboxRender[64]= TEXT("XboxRender");
extern "C" TCHAR GPackage_XboxAudio[64] = TEXT("XboxAudio");

// Global logger instance — opened before anything Unreal touches
FXboxLogger GXboxLog;
DWORD GXboxMallocLiveBytes = 0;
DWORD GXboxMallocPeakBytes = 0;
DWORD GXboxMallocTotalBytes = 0;
DWORD GXboxMallocLargestBytes = 0;
DWORD GXboxMallocLastLargeBytes = 0;
char  GXboxMallocLargestTag[64] = {0};
char  GXboxMallocLastLargeTag[64] = {0};

extern "C" volatile unsigned int g_XboxDebugMirrorMagic0 = 0x55395439; // UT9U
extern "C" volatile unsigned int g_XboxBootPhase = 0;
extern "C" volatile unsigned int g_XboxLogWriteCount = 0;
extern "C" volatile unsigned int g_XboxHeartbeatCount = 0;
extern "C" volatile unsigned int g_XboxLastLogTick = 0;
extern "C" volatile unsigned int g_XboxLogMirrorWriteOffset = 0;
extern "C" volatile unsigned int g_XboxLogMirrorWrapped = 0;
extern "C" volatile unsigned int g_XboxDebugMirrorMagic1 = 0x4D41524C; // LRAM
extern "C" volatile char g_XboxLogMirror[65536] = {0};

static BOOL GXboxDebugMirrorStarted = FALSE;
static BOOL GXboxDebugMirrorInWrite = FALSE;
static const unsigned int GXboxLogMirrorBytes = 65536;

static void XboxDebugMirrorAppendChar( char Ch )
{
    unsigned int Offset = g_XboxLogMirrorWriteOffset;
    if( Offset >= GXboxLogMirrorBytes - 2 )
    {
        g_XboxLogMirrorWrapped++;
        Offset = 0;
    }

    g_XboxLogMirror[Offset++] = Ch;
    g_XboxLogMirror[Offset] = 0;
    g_XboxLogMirrorWriteOffset = Offset;
}

static void XboxDebugMirrorAppendLine( const char* Line )
{
    if( !Line )
        return;

    while( *Line )
        XboxDebugMirrorAppendChar( *Line++ );
    XboxDebugMirrorAppendChar( '\n' );
}

extern "C" void XboxDebugMirrorStart()
{
    if( GXboxDebugMirrorStarted )
        return;
    GXboxDebugMirrorStarted = TRUE;
    g_XboxBootPhase = 0;
    g_XboxLogWriteCount = 0;
    g_XboxHeartbeatCount = 0;
    g_XboxLastLogTick = GetTickCount();
    g_XboxLogMirrorWriteOffset = 0;
    g_XboxLogMirrorWrapped = 0;
    g_XboxLogMirror[0] = 0;
    XboxDebugMirrorWriteAnsi( "XDBG RAM mirror enabled bytes=65536" );
}

extern "C" void XboxDebugMirrorStop()
{
    XboxDebugMirrorWriteAnsi( "XDBG RAM mirror stopped" );
    GXboxDebugMirrorStarted = FALSE;
}

extern "C" void XboxDebugSetBootPhase( unsigned int Phase )
{
    g_XboxBootPhase = Phase;
    g_XboxHeartbeatCount++;
}

extern "C" void XboxDebugHeartbeat()
{
    g_XboxHeartbeatCount++;
    g_XboxLastLogTick = GetTickCount();
}

extern "C" void XboxDebugMirrorWriteAnsi( const char* Line )
{
    if( !Line || GXboxDebugMirrorInWrite )
        return;

    GXboxDebugMirrorInWrite = TRUE;
    g_XboxLogWriteCount++;
    g_XboxLastLogTick = GetTickCount();

    char Packet[1400];
    int Len = _snprintf( Packet, sizeof(Packet)-1, "UT99XDBG t=%lu phase=0x%08X write=%u hb=%u %s",
        (DWORD)g_XboxLastLogTick,
        (unsigned)g_XboxBootPhase,
        (unsigned)g_XboxLogWriteCount,
        (unsigned)g_XboxHeartbeatCount,
        Line );
    if( Len < 0 || Len > (int)sizeof(Packet)-1 )
        Len = sizeof(Packet)-1;
    Packet[Len] = 0;
    XboxDebugMirrorAppendLine( Packet );
    GXboxDebugMirrorInWrite = FALSE;
}

static void XboxLogMemorySnapshot( const char* Label )
{
    MEMORYSTATUS MemStatus;
    appMemzero( &MemStatus, sizeof(MemStatus) );
    MemStatus.dwLength = sizeof(MemStatus);
    GlobalMemoryStatus( &MemStatus );
    GXboxLog.Write( "MEM %s availKB=%u heapLiveKB=%u heapPeakKB=%u heapTotalKB=%u largestKB=%u largestTag=%s lastLargeKB=%u lastLargeTag=%s",
        Label ? Label : "snapshot",
        (unsigned)(MemStatus.dwAvailPhys / 1024),
        (unsigned)(GXboxMallocLiveBytes / 1024),
        (unsigned)(GXboxMallocPeakBytes / 1024),
        (unsigned)(GXboxMallocTotalBytes / 1024),
        (unsigned)(GXboxMallocLargestBytes / 1024),
        GXboxMallocLargestTag,
        (unsigned)(GXboxMallocLastLargeBytes / 1024),
        GXboxMallocLastLargeTag );
}

static void XboxSmokeDebugHold( const char* Reason )
{
    if( GetFileAttributesA( "D:\\XboxSystemLinkSmoke.ini" ) == 0xFFFFFFFF )
        return;

    GXboxLog.Write( "BOOT: smoke marker present; holding 120s for RAM-log harvest reason=%s",
        Reason ? Reason : "unknown" );
    GXboxLog.Flush();
    Sleep( 120000 );
}

static void XboxLogCrashState( const char* Context )
{
    GXboxLog.Write( "BOOT: crash state context=%s critical=%d guarded=%d requestExit=%d errorHist=%s",
        Context ? Context : "unknown",
        GIsCriticalError ? 1 : 0,
        GIsGuarded ? 1 : 0,
        GIsRequestingExit ? 1 : 0,
        GErrorHist[0] ? TCHAR_TO_ANSI(GErrorHist) : "<empty>" );
    GXboxLog.Flush();
}

void __cdecl main()
{
    // ── Open log FIRST — before any Unreal code runs ─────────────────────
    XboxDebugMirrorStart();
    XboxDebugSetBootPhase( 0x1000 );
    GXboxLog.OpenWithFallbacks();
    XboxDebugSetBootPhase( 0x1001 );
    GXboxLog.Write( "BOOT: main() entered" );
    GXboxLog.Write( "BOOT: build %s %s", __DATE__, __TIME__ );
    GXboxLog.Write( "BOOT: features p8tex=1 xinputReuse=1 xinputThrottle=1 cache=256k memstack=32k audio=normal logDiet=1 texEvict=1 texFailCooldown=1 musicMemPad=1 scratchDiet=1 fixedXboxMenuDiscovery=1" );

    XDEVICE_PREALLOC_TYPE DeviceTypes[2];
    DeviceTypes[0].DeviceType = XDEVICE_TYPE_GAMEPAD;
    DeviceTypes[0].dwPreallocCount = 4;
    DeviceTypes[1].DeviceType = XDEVICE_TYPE_MEMORY_UNIT;
    DeviceTypes[1].dwPreallocCount = 8;
    GXboxLog.Write( "BOOT: calling XInitDevices gamepads=4 memoryUnits=8" );
    XboxDebugSetBootPhase( 0x1010 );
    XInitDevices( ARRAY_COUNT(DeviceTypes), DeviceTypes );
    INT EnumWaits = 0;
    while( XGetDeviceEnumerationStatus() == XDEVICE_ENUMERATION_BUSY )
    {
        Sleep( 10 );
        EnumWaits++;
        if( EnumWaits >= 200 )
        {
            GXboxLog.Write( "BOOT: XInitDevices enumeration still busy after %d waits", EnumWaits );
            break;
        }
    }
    XboxDebugSetBootPhase( 0x1011 );
    GXboxLog.Write( "BOOT: XInitDevices returned initialGamepadMask=0x%08X", XGetDevices( XDEVICE_TYPE_GAMEPAD ) );

    // Local platform objects -- named to avoid clashing with UT99 globals
    FMallocXbox          XboxMalloc;
    FOutputDeviceXboxError XboxError;
    FFeedbackContextXbox XboxWarn;
    FFileManagerXbox     XboxFileManager;

    GXboxLog.Write( "BOOT: platform objects created" );
    XboxDebugSetBootPhase( 0x1020 );
    XboxLogMemorySnapshot( "platform-objects" );

    try
    {
    GIsStarted  = 1;
    GIsClient   = 1;
    GIsGuarded  = 1;

    // Pull all Engine class .obj files into the image so their global
    // constructors run before ProcessRegistrants() is called inside appInit().
    ForceEngineClassLinks();

    GXboxLog.Write( "BOOT: calling appInit()" );
    XboxDebugSetBootPhase( 0x1100 );

    appInit(
        GPackage,
        TEXT(""),
        &XboxMalloc,
        &XboxWarn,
        &XboxError,
        &XboxWarn,
        &XboxFileManager,
        FConfigCacheIni::Factory,
        1
    );

    GXboxLog.Write( "BOOT: appInit() returned" );
    XboxDebugSetBootPhase( 0x1101 );
    XboxLogMemorySnapshot( "after-appInit" );

    GIsServer     = 1;
    GIsClient     = 1;
    GIsEditor     = 0;
    GIsScriptable = 1;
    GLazyLoad     = 0;

    GXboxLog.Write( "BOOT: calling InitEngine()" );
    XboxDebugSetBootPhase( 0x1200 );
    XboxLogMemorySnapshot( "before-InitEngine" );

    UEngine* Engine = InitEngine();

    GXboxLog.Write( "BOOT: InitEngine() returned (Engine=%s)", Engine ? "OK" : "NULL" );
    XboxDebugSetBootPhase( 0x1201 );
    XboxLogMemorySnapshot( "after-InitEngine" );

    if( Engine && !GIsRequestingExit )
    {
        GXboxLog.Write( "BOOT: entering MainLoop()" );
        XboxDebugSetBootPhase( 0x1300 );
        MainLoop( Engine );
        GXboxLog.Write( "BOOT: MainLoop() exited" );
        XboxDebugSetBootPhase( 0x1301 );
        XboxLogMemorySnapshot( "after-MainLoop" );
    }
    else
    {
        GXboxLog.Write( "BOOT: skipping MainLoop (Engine=%s, GIsRequestingExit=%d)",
            Engine ? "OK" : "NULL", GIsRequestingExit );
    }

    GXboxLog.Write( "BOOT: calling appPreExit()" );
    XboxDebugSetBootPhase( 0x1400 );
    appPreExit();
    GIsGuarded = 0;

    GXboxLog.Write( "BOOT: calling appExit() — goodbye" );
    XboxDebugSetBootPhase( 0x1401 );
    XboxDebugMirrorStop();
    GXboxLog.Close();
    appExit();
    }
    catch( const TCHAR* Error )
    {
        XboxDebugSetBootPhase( 0xE001 );
        GXboxLog.Write( "BOOT: caught TCHAR exception: %s", TCHAR_TO_ANSI(Error) );
        XboxLogCrashState( "TCHAR exception" );
        XboxSmokeDebugHold( "TCHAR exception" );
        XboxDebugMirrorStop();
        GXboxLog.Close();
    }
    catch( ... )
    {
        XboxDebugSetBootPhase( 0xE002 );
        GXboxLog.Write( "BOOT: caught unknown C++ exception" );
        XboxLogCrashState( "unknown C++ exception" );
        XboxSmokeDebugHold( "unknown C++ exception" );
        XboxDebugMirrorStop();
        GXboxLog.Close();
    }
}
