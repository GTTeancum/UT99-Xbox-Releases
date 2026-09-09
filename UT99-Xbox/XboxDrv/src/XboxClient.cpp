// XboxClient.cpp

extern "C" UBOOL XboxSplitIsActive();
extern "C" void XboxSplitBeginRenderFrame( INT ViewportCount );
extern "C" void XboxSplitSetRenderViewport( INT ViewportIndex );
extern "C" void XboxSplitTryActivate( UClient* Client );
extern "C" void XboxSplitTickDummies( UClient* Client );
extern "C" UBOOL XboxSplitShouldRenderViewport( UViewport* Viewport, INT ViewportIndex );
extern "C" void XboxSplitClearUnusedRenderRegions( UClient* Client );
extern "C" void XboxProfileApplyClientConfig( UXboxClient* Client );
extern "C" void XboxRenderSetDisplayCalibration( FLOAT Brightness, FLOAT Contrast, FLOAT Gamma );

static INT XboxClientClampAction( INT Action, INT DefaultAction )
{
    return (Action >= XCA_None && Action <= XCA_CenterView) ? Action : DefaultAction;
}

void UXboxClient::StaticConstructor()
{
    new(GetClass(),TEXT("NumLocalPlayers"),       RF_Public) UIntProperty  (CPP_PROPERTY(NumLocalPlayers),       TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("InvertVertical"),         RF_Public) UBoolProperty (CPP_PROPERTY(InvertVertical),        TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ControllerSensitivity"),  RF_Public) UFloatProperty(CPP_PROPERTY(ControllerSensitivity), TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("DeadZone"),               RF_Public) UFloatProperty(CPP_PROPERTY(DeadZone),              TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ScaleXYZ"),               RF_Public) UFloatProperty(CPP_PROPERTY(ScaleXYZ),              TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ScaleRUV"),               RF_Public) UFloatProperty(CPP_PROPERTY(ScaleRUV),              TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonLayout"),           RF_Public) UIntProperty  (CPP_PROPERTY(ButtonLayout),          TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ControlPreset"),          RF_Public) UIntProperty  (CPP_PROPERTY(ControlPreset),         TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("StickLayout"),            RF_Public) UIntProperty  (CPP_PROPERTY(StickLayout),           TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("SafeAreaSize"),           RF_Public) UIntProperty  (CPP_PROPERTY(SafeAreaSize),          TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("SafeAreaX"),              RF_Public) UIntProperty  (CPP_PROPERTY(SafeAreaX),             TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("SafeAreaY"),              RF_Public) UIntProperty  (CPP_PROPERTY(SafeAreaY),             TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("Contrast"),               RF_Public) UFloatProperty(CPP_PROPERTY(DisplayContrast),       TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("Gamma"),                  RF_Public) UFloatProperty(CPP_PROPERTY(DisplayGamma),          TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionA"),          RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionA),         TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionB"),          RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionB),         TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionX"),          RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionX),         TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionY"),          RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionY),         TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionLeftTrigger"), RF_Public) UIntProperty (CPP_PROPERTY(ButtonActionLeftTrigger), TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionRightTrigger"), RF_Public) UIntProperty(CPP_PROPERTY(ButtonActionRightTrigger),TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionWhite"),      RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionWhite),     TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionBlack"),      RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionBlack),     TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionBack"),       RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionBack),      TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ButtonActionRightThumb"), RF_Public) UIntProperty  (CPP_PROPERTY(ButtonActionRightThumb),TEXT("Display"), CPF_Config);
}

void UXboxClient::Init( UEngine* InEngine )
{
    guard(UXboxClient::Init);

    // Match the stock UWindowsClient init contract: the base UClient owns the
    // engine link and generic client config validation.
    Super::Init( InEngine );

    InvertVertical        = 0;
    ControllerSensitivity = 1.0f;
    DeadZone              = 0.2f;
    ScaleXYZ              = 100.0f;
    ScaleRUV              = 100.0f;
    ButtonLayout          = 0;
    ControlPreset         = 0;
    StickLayout           = XSL_Default;
    SafeAreaSize          = 92;
    SafeAreaX             = 0;
    SafeAreaY             = 0;
    Brightness            = 0.5f;
    DisplayContrast       = 1.0f;
    DisplayGamma          = 1.0f;
    ButtonActionA         = XCA_Jump;
    ButtonActionB         = XCA_Duck;
    ButtonActionX         = XCA_Use;
    ButtonActionY         = XCA_Dodge;
    ButtonActionLeftTrigger  = XCA_AltFire;
    ButtonActionRightTrigger = XCA_Fire;
    ButtonActionWhite     = XCA_PrevWeaponWheel;
    ButtonActionBlack     = XCA_NextWeaponWheel;
    ButtonActionBack      = XCA_Scoreboard;
    ButtonActionRightThumb= XCA_CenterView;

    LoadConfig();
    if( ButtonLayout == 1 )
    {
        GXboxLog.Write( "XboxClient::Init: migrating obsolete southpaw ButtonLayout=1 to default button layout" );
        ButtonLayout = 0;
        SaveConfig();
    }
    else if( ButtonLayout < 0 || ButtonLayout > 2 )
    {
        GXboxLog.Write( "XboxClient::Init: clamping invalid ButtonLayout=%d to default", ButtonLayout );
        ButtonLayout = 0;
        SaveConfig();
    }

    if( ButtonLayout == 2 && ControlPreset == 0 )
    {
        ControlPreset = 1;
        ButtonActionA = XCA_Fire;
        ButtonActionRightTrigger = XCA_Jump;
        GXboxLog.Write( "XboxClient::Init: migrated legacy Face Fire ButtonLayout=2 to configurable controls preset" );
        SaveConfig();
    }

    if( ControlPreset < -1 || ControlPreset >= XBOX_CONTROL_PRESET_COUNT )
        ControlPreset = 0;
    if( StickLayout < XSL_Default || StickLayout > XSL_LegacySouthpaw )
        StickLayout = XSL_Default;
    SafeAreaSize = Clamp<INT>( SafeAreaSize, 85, 100 );
    SafeAreaX = Clamp<INT>( SafeAreaX, -48, 48 );
    SafeAreaY = Clamp<INT>( SafeAreaY, -36, 36 );
    Brightness = Clamp<FLOAT>( Brightness, 0.0f, 1.0f );
    DisplayContrast = Clamp<FLOAT>( DisplayContrast, 0.5f, 1.5f );
    DisplayGamma = Clamp<FLOAT>( DisplayGamma, 0.5f, 2.0f );
    ButtonActionA = XboxClientClampAction( ButtonActionA, XCA_Jump );
    ButtonActionB = XboxClientClampAction( ButtonActionB, XCA_Duck );
    ButtonActionX = XboxClientClampAction( ButtonActionX, XCA_Use );
    ButtonActionY = XboxClientClampAction( ButtonActionY, XCA_Dodge );
    ButtonActionLeftTrigger = XboxClientClampAction( ButtonActionLeftTrigger, XCA_AltFire );
    ButtonActionRightTrigger = XboxClientClampAction( ButtonActionRightTrigger, XCA_Fire );
    ButtonActionWhite = XboxClientClampAction( ButtonActionWhite, XCA_PrevWeaponWheel );
    ButtonActionBlack = XboxClientClampAction( ButtonActionBlack, XCA_NextWeaponWheel );
    ButtonActionBack = XboxClientClampAction( ButtonActionBack, XCA_Scoreboard );
    ButtonActionRightThumb = XboxClientClampAction( ButtonActionRightThumb, XCA_CenterView );
    XboxProfileApplyClientConfig( this );
    XboxRenderSetDisplayCalibration( Brightness, DisplayContrast, DisplayGamma );

    NumLocalPlayers       = 1;
    HasFocus              = 1;

    // Keep the Xbox profile deterministic even when an older generated
    // UnrealTournament.ini still contains desktop defaults. These three are
    // especially important while we are diagnosing distant surface flicker and
    // render cost.
    ScreenFlashes         = 0;
    Decals                = 0;
    NoDynamicLights       = 1;
    MinDesiredFrameRate   = 60.0f;
    TextureLODSet[LODSET_World] = 0;
    TextureLODSet[LODSET_Skin]  = 0;

    GXboxLog.Write( "XboxClient::Init: settings flashes=%d decals=%d dynLights=%d minFPS=%.1f scaleXYZ=%.1f scaleRUV=%.1f preset=%d stick=%d safeSize=%d safePos=%d,%d brightness=%.2f contrast=%.2f gamma=%.2f",
        ScreenFlashes, Decals, NoDynamicLights, MinDesiredFrameRate, ScaleXYZ, ScaleRUV, ControlPreset, StickLayout, SafeAreaSize, SafeAreaX, SafeAreaY,
        Brightness, DisplayContrast, DisplayGamma );

    PostEditChange();
    unguard;
}

void UXboxClient::Destroy()
{
    guard(UXboxClient::Destroy);
    Super::Destroy();
    unguard;
}

void UXboxClient::Tick()
{
    guard(UXboxClient::Tick);
    static UBOOL bFirstTick = 1;
    static INT ClientTickCount = 0;
    ClientTickCount++;
    const UBOOL bVerboseTickLog = 0;
    UBOOL bBoundaryTick = bVerboseTickLog && (ClientTickCount >= 210 && ClientTickCount <= 260);
    if( bFirstTick )
    {
        GXboxLog.Write( "XboxClient::Tick: FIRST CALL Viewports.Num=%d Engine=0x%08X",
            Viewports.Num(), (DWORD)Engine );
        bFirstTick = 0;
    }
    if( bBoundaryTick )
        GXboxLog.Write( "XCLIENT tick=%d begin viewports=%d", ClientTickCount, Viewports.Num() );
    if( 0 )
        GXboxLog.Write( "XCLIENT tick=%d begin viewports=%d", ClientTickCount, Viewports.Num() );
    XboxSplitTryActivate( this );
    UBOOL bSplit = XboxSplitIsActive();
    XboxSplitTickDummies( this );
    XboxSplitBeginRenderFrame( Viewports.Num() );
    if( bSplit )
        XboxSplitClearUnusedRenderRegions( this );

    INT LastRenderViewport = Viewports.Num() - 1;
    if( bSplit )
    {
        LastRenderViewport = -1;
        for( INT i=0; i<Viewports.Num(); i++ )
            if( XboxSplitShouldRenderViewport( Viewports(i), i ) )
                LastRenderViewport = i;
    }

    DOUBLE SplitDrawStart = bSplit ? appSeconds() : 0.0;
    INT SplitDrawn = 0;
    INT SplitSkipped = 0;

    for( INT i=0; i<Viewports.Num(); i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( Viewports(i) );
        if( VP )
        {
            if( bBoundaryTick )
                GXboxLog.Write( "XCLIENT tick=%d vp=%d poll-begin VP=0x%08X actor=0x%08X rendev=0x%08X",
                    ClientTickCount, i, (DWORD)VP, (DWORD)VP->Actor, (DWORD)VP->RenDev );
            if( !bSplit )
                VP->PollController();
            if( bBoundaryTick )
                GXboxLog.Write( "XCLIENT tick=%d vp=%d draw-begin", ClientTickCount, i );
            if( bSplit && !XboxSplitShouldRenderViewport( VP, i ) )
            {
                SplitSkipped++;
                if( bBoundaryTick )
                    GXboxLog.Write( "XCLIENT tick=%d vp=%d draw-skip dummy=%d",
                        ClientTickCount, i, VP->bXboxSplitDummy ? 1 : 0 );
                continue;
            }
            // Draw the viewport — this is what triggers rendering each frame.
            // On Windows, WinClient::Tick() calls Viewport->Repaint(1) which
            // calls Engine->Draw(). We call Draw directly here.
            static UBOOL bFirstDraw = 1;
            if( bFirstDraw )
            {
                GXboxLog.Write( "XboxClient::Tick: FIRST Engine->Draw VP=0x%08X RenDev=0x%08X Actor=0x%08X",
                    (DWORD)VP, (DWORD)VP->RenDev, (DWORD)VP->Actor );
                bFirstDraw = 0;
            }
            XboxSplitSetRenderViewport( i );
            Engine->Draw( VP, (!bSplit || i == LastRenderViewport) ? 1 : 0 );
            if( bSplit )
                SplitDrawn++;
            if( bBoundaryTick )
                GXboxLog.Write( "XCLIENT tick=%d vp=%d draw-end", ClientTickCount, i );
        }
    }
    if( bSplit )
    {
        static DOUBLE LastSplitPerfLogSeconds = 0.0;
        static DOUBLE SplitDrawTotalSeconds = 0.0;
        static INT SplitPerfFrames = 0;
        DOUBLE NowSeconds = appSeconds();
        SplitDrawTotalSeconds += NowSeconds - SplitDrawStart;
        SplitPerfFrames++;
        if( LastSplitPerfLogSeconds == 0.0 || NowSeconds - LastSplitPerfLogSeconds >= 2.0 )
        {
            GXboxLog.Write( "XSPLIT PERF viewports=%d drawn=%d skipped=%d last=%d drawMS=%.2f meanDrawMS=%.3f samples=%d",
                Viewports.Num(), SplitDrawn, SplitSkipped, LastRenderViewport,
                (FLOAT)((NowSeconds - SplitDrawStart) * 1000.0),
                (FLOAT)(SplitDrawTotalSeconds * 1000.0 / SplitPerfFrames), SplitPerfFrames );
            LastSplitPerfLogSeconds = NowSeconds;
            SplitDrawTotalSeconds = 0.0;
            SplitPerfFrames = 0;
        }
    }
    if( bBoundaryTick )
        GXboxLog.Write( "XCLIENT tick=%d end", ClientTickCount );
    unguard;
}

UViewport* UXboxClient::NewViewport( const FName Name )
{
    guard(UXboxClient::NewViewport);
    GXboxLog.Write( "XboxClient::NewViewport: name=%s", TCHAR_TO_ANSI(*Name) );
    UXboxViewport* VP = new( this, Name ) UXboxViewport();
    VP->ControllerPort      = Viewports.Num() - 1;
    VP->ControllerHandle    = NULL;
    VP->ControllerConnected = 0;
    VP->bXboxSplitDummy     = 0;
    appMemzero( &VP->ControllerState,     sizeof(VP->ControllerState)     );
    appMemzero( &VP->PrevControllerState, sizeof(VP->PrevControllerState) );
    GXboxLog.Write( "XboxClient::NewViewport: VP=0x%08X", (DWORD)VP );
    return VP;
    unguard;
}

void UXboxClient::PostEditChange()
{
    guard(UXboxClient::PostEditChange);
    Super::PostEditChange();
    unguard;
}

void UXboxClient::ShutdownAfterError()
{
    guard(UXboxClient::ShutdownAfterError);
    Super::ShutdownAfterError();
    unguard;
}

void UXboxClient::ShowViewportWindows( DWORD ShowFlags, int DoShow )   {}
void UXboxClient::EnableViewportWindows( DWORD ShowFlags, int DoEnable ) {}

UBOOL UXboxClient::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
    guard(UXboxClient::Exec);
    if( Super::Exec( Cmd, Ar ) )
        return 1;
    for( INT i=0; i<Viewports.Num(); i++ )
        if( Viewports(i)->Exec(Cmd,Ar) )
            return 1;
    return 0;
    unguard;
}

void UXboxClient::MakeCurrent( UViewport* NewViewport )
{
    guard(UXboxClient::MakeCurrent);
    for( INT i=0; i<Viewports.Num(); i++ )
    {
        UViewport* VP = Viewports(i);
        if( VP->Current && VP != NewViewport )
        {
            VP->Current = 0;
            VP->UpdateWindowFrame();
        }
    }
    if( NewViewport )
    {
        NewViewport->Current = 1;
        NewViewport->UpdateWindowFrame();
    }
    unguard;
}
