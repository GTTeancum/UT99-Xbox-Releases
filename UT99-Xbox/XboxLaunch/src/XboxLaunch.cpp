// XboxLaunch.cpp
// Entry point for UT99 on Xbox.

#include "XboxLaunchPrivate.h"
#include "FConfigCacheIni.h"

// InitEngine and MainLoop live in UT99Engine.lib.
// With ENGINE_API empty these have plain (non-decorated) linkage.
UEngine* InitEngine();
void     MainLoop( UEngine* Engine );

// GPackage is also defined in Core.lib (Core.obj via IMPLEMENT_PACKAGE).
// We must NOT redefine it here -- Core owns it.

void __cdecl main()
{
    // Local platform objects -- named to avoid clashing with UT99 globals
    FMallocXbox          XboxMalloc;
    FOutputDeviceXboxError XboxError;
    FFeedbackContextXbox XboxWarn;
    FFileManagerXbox     XboxFileManager;

    GIsStarted  = 1;
    GIsClient   = 1;
    GIsGuarded  = 1;

    appStrcpy( GPackage, TEXT("UnrealTournament") );

    appInit(
        GPackage,
        TEXT(""),
        &XboxMalloc,
        NULL,
        &XboxError,
        &XboxWarn,
        &XboxFileManager,
        FConfigCacheIni::Factory,
        1
    );

    GIsServer     = 1;
    GIsClient     = 1;
    GIsEditor     = 0;
    GIsScriptable = 1;
    GLazyLoad     = 0;

    UEngine* Engine = InitEngine();
    if( Engine && !GIsRequestingExit )
        MainLoop( Engine );

    appPreExit();
    GIsGuarded = 0;
    appExit();
}
