// XboxAudio.cpp — Xbox audio subsystem stub
// Returns 0 from Init() so the engine continues without audio.
// All UAudioSubsystem pure virtuals are implemented as silent no-ops.

#include <xtl.h>
#ifdef Top
#undef Top
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif

#pragma warning(disable: 4005)
#undef  DLL_EXPORT
#define DLL_EXPORT
#undef  DLL_IMPORT
#define DLL_IMPORT
#undef  CORE_API
#define CORE_API
#undef  ENGINE_API
#define ENGINE_API
#pragma warning(disable: 4996 4244 4267 4305 4800 4018)
#pragma conform(forScope, off)

#undef  GPackage
#define GPackage GPackage_XboxAudio

#include "Engine.h"
#include "UnAudio.h"
#include "FXboxLogger.h"

/*-----------------------------------------------------------------------------
    UXboxAudioDevice — silent stub UAudioSubsystem
-----------------------------------------------------------------------------*/
class UXboxAudioDevice : public UAudioSubsystem
{
    DECLARE_CLASS(UXboxAudioDevice, UAudioSubsystem, CLASS_Config)
    NO_DEFAULT_CONSTRUCTOR(UXboxAudioDevice)

    UViewport* Viewport;
public:
    void StaticConstructor()
    {
        Viewport = NULL;
    }

    UBOOL Init()
    {
        GXboxLog.Write( "XboxAudio: Init() -- audio disabled (stub), returning 0" );
        return 0;
    }

    void  SetViewport( UViewport* InViewport )                                               { Viewport = InViewport; }
    UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar )                                        { return 0; }
    void  Update( FPointRegion Region, FCoords& Listener )                                   {}
    void  RegisterMusic( UMusic* Music )                                                     {}
    void  RegisterSound( USound* Sound )                                                     {}
    void  UnregisterSound( USound* Sound )                                                   {}
    void  UnregisterMusic( UMusic* Music )                                                   {}
    UBOOL PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch ) { return 0; }
    void  NoteDestroy( AActor* Actor )                                                       {}
    UBOOL GetLowQualitySetting()                                                             { return 1; }
    UViewport* GetViewport()                                                                 { return Viewport; }
    void  RenderAudioGeometry( FSceneNode* Frame )                                           {}
    void  PostRender( FSceneNode* Frame )                                                    {}
};

IMPLEMENT_CLASS(UXboxAudioDevice);
IMPLEMENT_PACKAGE(XboxAudio);
