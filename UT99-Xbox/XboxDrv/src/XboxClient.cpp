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
    Engine                = InEngine;
    NumLocalPlayers       = 1;
    HasFocus              = 1;
    InvertVertical        = 0;
    ControllerSensitivity = 1.0f;
    DeadZone              = 0.2f;
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
    for( INT i=0; i<Viewports.Num(); i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( Viewports(i) );
        if( VP )
        {
            VP->PollController();
            // Draw the viewport — this is what triggers rendering each frame.
            // On Windows, WinClient::Tick() calls Viewport->Repaint(1) which
            // calls Engine->Draw(). We call Draw directly here.
            Engine->Draw( VP, 1 );
        }
    }
    unguard;
}

UViewport* UXboxClient::NewViewport( const FName Name )
{
    guard(UXboxClient::NewViewport);
    UXboxViewport* VP = new( this, Name ) UXboxViewport();
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
