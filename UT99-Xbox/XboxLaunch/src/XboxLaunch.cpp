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
extern "C" TCHAR GPackage_XboxDrv[64]   = TEXT("XboxDrv");
extern "C" TCHAR GPackage_XboxRender[64]= TEXT("XboxRender");
extern "C" TCHAR GPackage_XboxAudio[64] = TEXT("XboxAudio");

// Global logger instance — opened before anything Unreal touches
FXboxLogger GXboxLog;

void __cdecl main()
{
    // ── Open log FIRST — before any Unreal code runs ─────────────────────
    GXboxLog.OpenWithFallbacks();
    GXboxLog.Write( "BOOT: main() entered" );
    GXboxLog.Write( "BOOT: build %s %s", __DATE__, __TIME__ );
    GXboxLog.Write( "BOOT: features p8tex=1 xinputReuse=1 xinputThrottle=1 cache=256k memstack=32k audio=normal logDiet=1 texEvict=1 texFailCooldown=1 musicMemPad=1 scratchDiet=1 fixedXboxMenuDiscovery=1" );

    XDEVICE_PREALLOC_TYPE DeviceTypes[2];
    DeviceTypes[0].DeviceType = XDEVICE_TYPE_GAMEPAD;
    DeviceTypes[0].dwPreallocCount = 4;
    DeviceTypes[1].DeviceType = XDEVICE_TYPE_MEMORY_UNIT;
    DeviceTypes[1].dwPreallocCount = 8;
    GXboxLog.Write( "BOOT: calling XInitDevices gamepads=4 memoryUnits=8" );
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
    GXboxLog.Write( "BOOT: XInitDevices returned initialGamepadMask=0x%08X", XGetDevices( XDEVICE_TYPE_GAMEPAD ) );

    // Local platform objects -- named to avoid clashing with UT99 globals
    FMallocXbox          XboxMalloc;
    FOutputDeviceXboxError XboxError;
    FFeedbackContextXbox XboxWarn;
    FFileManagerXbox     XboxFileManager;

    GXboxLog.Write( "BOOT: platform objects created" );

    try
    {
    GIsStarted  = 1;
    GIsClient   = 1;
    GIsGuarded  = 1;

    // Pull all Engine class .obj files into the image so their global
    // constructors run before ProcessRegistrants() is called inside appInit().
    ForceEngineClassLinks();

    GXboxLog.Write( "BOOT: calling appInit()" );

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

    GIsServer     = 1;
    GIsClient     = 1;
    GIsEditor     = 0;
    GIsScriptable = 1;
    GLazyLoad     = 0;

    GXboxLog.Write( "BOOT: calling InitEngine()" );

    UEngine* Engine = InitEngine();

    GXboxLog.Write( "BOOT: InitEngine() returned (Engine=%s)", Engine ? "OK" : "NULL" );

    if( Engine && !GIsRequestingExit )
    {
        GXboxLog.Write( "BOOT: entering MainLoop()" );
        MainLoop( Engine );
        GXboxLog.Write( "BOOT: MainLoop() exited" );
    }
    else
    {
        GXboxLog.Write( "BOOT: skipping MainLoop (Engine=%s, GIsRequestingExit=%d)",
            Engine ? "OK" : "NULL", GIsRequestingExit );
    }

    GXboxLog.Write( "BOOT: calling appPreExit()" );
    appPreExit();
    GIsGuarded = 0;

    GXboxLog.Write( "BOOT: calling appExit() — goodbye" );
    GXboxLog.Close();
    appExit();
    }
    catch( const TCHAR* Error )
    {
        GXboxLog.Write( "BOOT: caught TCHAR exception: %s", TCHAR_TO_ANSI(Error) );
        GXboxLog.Close();
    }
    catch( ... )
    {
        GXboxLog.Write( "BOOT: caught unknown C++ exception" );
        GXboxLog.Close();
    }
}
