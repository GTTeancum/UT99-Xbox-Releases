// XboxDrv.h
// NOTE: XTL.h + macro undefs are in XboxDrvPrivate.h (forced include).

#pragma once

#include "Engine.h"
#include "UnRender.h"

#define XBOX_MAX_LOCAL_PLAYERS  4
#define XBOX_SCREEN_WIDTH       640
#define XBOX_SCREEN_HEIGHT      480

enum EXboxControlAction
{
    XCA_None = 0,
    XCA_Fire,
    XCA_AltFire,
    XCA_Jump,
    XCA_Duck,
    XCA_Use,
    XCA_Dodge,
    XCA_PrevWeaponWheel,
    XCA_NextWeaponWheel,
    XCA_Scoreboard,
    XCA_CenterView
};

enum EXboxStickLayout
{
    XSL_Default = 0,
    XSL_Southpaw,
    XSL_Legacy,
    XSL_LegacySouthpaw
};

#define XBOX_CONTROL_PRESET_COUNT 8

class UXboxViewport;

class UXboxClient : public UClient
{
    DECLARE_CLASS( UXboxClient, UClient, CLASS_Config )
    NO_DEFAULT_CONSTRUCTOR( UXboxClient )
public:
    INT             NumLocalPlayers;
    UBOOL           HasFocus;
    UBOOL           InvertVertical;
    FLOAT           ControllerSensitivity;
    FLOAT           DeadZone;
    FLOAT           ScaleXYZ;
    FLOAT           ScaleRUV;
    INT             ButtonLayout;
    INT             ControlPreset;
    INT             StickLayout;
    INT             SafeAreaSize;
    INT             SafeAreaX;
    INT             SafeAreaY;
    INT             ButtonActionA;
    INT             ButtonActionB;
    INT             ButtonActionX;
    INT             ButtonActionY;
    INT             ButtonActionLeftTrigger;
    INT             ButtonActionRightTrigger;
    INT             ButtonActionWhite;
    INT             ButtonActionBlack;
    INT             ButtonActionBack;
    INT             ButtonActionRightThumb;

    void            StaticConstructor();
    void            Init( UEngine* InEngine );
    void            Destroy();
    void            Tick();
    UViewport*      NewViewport( const FName Name );
    void            PostEditChange();
    void            ShutdownAfterError();
    void            ShowViewportWindows( DWORD ShowFlags, int DoShow );
    void            EnableViewportWindows( DWORD ShowFlags, int DoEnable );
    UBOOL           Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
    void            MakeCurrent( UViewport* NewViewport );
};

class UXboxViewport : public UViewport
{
    DECLARE_CLASS( UXboxViewport, UViewport, CLASS_Transient )
    // No NO_DEFAULT_CONSTRUCTOR -- new(outer,name) needs public default ctor
public:
    INT             ViewX, ViewY;
    INT             ViewWidth, ViewHeight;
    INT             ControllerPort;       // 0-3 port index
    HANDLE          ControllerHandle;     // from XInputOpen, or NULL
    XINPUT_STATE    ControllerState;
    XINPUT_STATE    PrevControllerState;
    UBOOL           ControllerConnected;
    UBOOL           bXboxSplitDummy;

    void            Destroy();
    void            ShutdownAfterError();
    UBOOL           Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear,
                          DWORD RenderLockFlags, BYTE* HitData=NULL, INT* HitSize=NULL );
    void            Unlock( UBOOL Blit );
    void            Repaint( UBOOL Blit );
    UBOOL           ResizeViewport( DWORD BlitFlags, INT NewX=INDEX_NONE,
                                    INT NewY=INDEX_NONE, INT NewColorBytes=INDEX_NONE );
    UBOOL           IsFullscreen();
    void            SetModeCursor();
    void            UpdateWindowFrame();
    void            SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly=0 );
    void            UpdateInput( UBOOL Reset );
    void*           GetWindow();
    void            OpenWindow( DWORD ParentWindow, UBOOL Temporary, INT NewX,
                                INT NewY, INT OpenX, INT OpenY );
    void            CloseWindow();
    UBOOL           Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
    void            SetViewRegion( INT X, INT Y, INT W, INT H );
    void            PollController();
    void            ProcessControllerInput( const XINPUT_GAMEPAD& Pad );
};
