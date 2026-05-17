// XboxClient.cpp

void UXboxClient::StaticConstructor()
{
    new(GetClass(),TEXT("NumLocalPlayers"),       RF_Public) UIntProperty  (CPP_PROPERTY(NumLocalPlayers),       TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("InvertVertical"),         RF_Public) UBoolProperty (CPP_PROPERTY(InvertVertical),        TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("ControllerSensitivity"),  RF_Public) UFloatProperty(CPP_PROPERTY(ControllerSensitivity), TEXT("Display"), CPF_Config);
    new(GetClass(),TEXT("DeadZone"),               RF_Public) UFloatProperty(CPP_PROPERTY(DeadZone),              TEXT("Display"), CPF_Config);
}

void UXboxClient::Init( UEngine* InEngine )
{
    guard(UXboxClient::Init);

    // Match the stock UWindowsClient init contract: the base UClient owns the
    // engine link and generic client config validation.
    Super::Init( InEngine );

    NumLocalPlayers       = 1;
    HasFocus              = 1;
    InvertVertical        = 0;
    ControllerSensitivity = 1.0f;
    DeadZone              = 0.2f;
    TextureLODSet[LODSET_World] = 2;
    TextureLODSet[LODSET_Skin]  = 2;
    MinDesiredFrameRate   = 20.0f;
    ScreenFlashes         = 0;
    Decals                = 0;
    NoDynamicLights       = 1;

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
    UBOOL bBoundaryTick = (ClientTickCount >= 210 && ClientTickCount <= 260);
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
    for( INT i=0; i<Viewports.Num(); i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( Viewports(i) );
        if( VP )
        {
            if( bBoundaryTick )
                GXboxLog.Write( "XCLIENT tick=%d vp=%d poll-begin VP=0x%08X actor=0x%08X rendev=0x%08X",
                    ClientTickCount, i, (DWORD)VP, (DWORD)VP->Actor, (DWORD)VP->RenDev );
            VP->PollController();
            if( bBoundaryTick )
                GXboxLog.Write( "XCLIENT tick=%d vp=%d draw-begin", ClientTickCount, i );
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
            Engine->Draw( VP, 1 );
            if( bBoundaryTick )
                GXboxLog.Write( "XCLIENT tick=%d vp=%d draw-end", ClientTickCount, i );
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
