// XboxViewport.cpp

void UXboxViewport::OpenWindow( DWORD ParentWindow, UBOOL Temporary,
                                 INT NewX, INT NewY, INT OpenX, INT OpenY )
{
    guard(UXboxViewport::OpenWindow);

    GXboxLog.Write( "OpenWindow: entered (NewX=%d, NewY=%d)", NewX, NewY );

    ViewX      = 0;
    ViewY      = 0;
    ViewWidth  = SizeX = NewX > 0 ? NewX : XBOX_SCREEN_WIDTH;
    ViewHeight = SizeY = NewY > 0 ? NewY : XBOX_SCREEN_HEIGHT;
    ColorBytes = 4;
    ControllerIndex     = -1;
    ControllerConnected = 0;
    appMemzero( &ControllerState,     sizeof(ControllerState)     );
    appMemzero( &PrevControllerState, sizeof(PrevControllerState) );

    // Create render device if we don't have one yet
    if( !RenDev )
    {
        GXboxLog.Write( "OpenWindow: loading GameRenderDevice class" );

        UClass* RenderClass = UObject::StaticLoadClass(
            URenderDevice::StaticClass(),
            NULL,
            TEXT("ini:Engine.Engine.GameRenderDevice"),
            NULL,
            LOAD_NoFail,
            NULL
        );

        GXboxLog.Write( "OpenWindow: RenderClass=%s", RenderClass ? "OK" : "NULL" );

        if( RenderClass )
        {
            RenDev = ConstructObject<URenderDevice>( RenderClass, this );
            GXboxLog.Write( "OpenWindow: calling RenDev->Init(%dx%d)", SizeX, SizeY );

            if( !RenDev->Init( this, SizeX, SizeY, ColorBytes, 1 ) )
            {
                GXboxLog.Write( "OpenWindow: RenDev->Init() FAILED" );
                debugf( NAME_Init, TEXT("XboxViewport: RenderDevice init failed") );
                delete RenDev;
                RenDev = NULL;
            }
            else
            {
                GXboxLog.Write( "OpenWindow: RenDev->Init() succeeded" );
            }
        }
        GRenderDevice = RenDev;
    }

    GXboxLog.Write( "OpenWindow: done (RenDev=%s)", RenDev ? "OK" : "NULL" );

    unguard;
}

void UXboxViewport::CloseWindow() {}

void UXboxViewport::Destroy()
{
    guard(UXboxViewport::Destroy);
    Super::Destroy();
    unguard;
}

void UXboxViewport::ShutdownAfterError()
{
    guard(UXboxViewport::ShutdownAfterError);
    Super::ShutdownAfterError();
    unguard;
}

void UXboxViewport::SetViewRegion( INT X, INT Y, INT W, INT H )
{
    ViewX = X; ViewY = Y; ViewWidth = W; ViewHeight = H;
}

void UXboxViewport::BindController( INT Index )
{
    ControllerIndex = Index;
}

void UXboxViewport::PollController()
{
    guard(UXboxViewport::PollController);
    if( ControllerIndex < 0 )
        return;
    PrevControllerState = ControllerState;
    DWORD Result = XInputGetState( (HANDLE)(DWORD_PTR)ControllerIndex, &ControllerState );
    ControllerConnected = ( Result == ERROR_SUCCESS );
    if( ControllerConnected )
        ProcessControllerInput( ControllerState.Gamepad );
    unguard;
}

void UXboxViewport::ProcessControllerInput( const XINPUT_GAMEPAD& Pad )
{
    // TODO: map buttons to EInputKey
}

UBOOL UXboxViewport::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear,
                            DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
    guard(UXboxViewport::Lock);
    return Super::Lock( FlashScale, FlashFog, ScreenClear, RenderLockFlags, HitData, HitSize );
    unguard;
}

void UXboxViewport::Unlock( UBOOL Blit )
{
    guard(UXboxViewport::Unlock);
    Super::Unlock( Blit );
    unguard;
}

void UXboxViewport::Repaint( UBOOL Blit ) {}

UBOOL UXboxViewport::ResizeViewport( DWORD BlitFlags, INT NewX, INT NewY, INT NewColorBytes )
{
    guard(UXboxViewport::ResizeViewport);
    if( NewX != INDEX_NONE )          SizeX      = ViewWidth  = NewX;
    if( NewY != INDEX_NONE )          SizeY      = ViewHeight = NewY;
    if( NewColorBytes != INDEX_NONE ) ColorBytes = NewColorBytes;
    return 1;
    unguard;
}

UBOOL UXboxViewport::IsFullscreen()    { return 1; }
void  UXboxViewport::SetModeCursor()   {}
void  UXboxViewport::UpdateWindowFrame() {}
void  UXboxViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly ) {}

void UXboxViewport::UpdateInput( UBOOL Reset )
{
    guard(UXboxViewport::UpdateInput);
    PollController();
    unguard;
}

void* UXboxViewport::GetWindow() { return NULL; }

UBOOL UXboxViewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
    guard(UXboxViewport::Exec);
    return Super::Exec( Cmd, Ar );
    unguard;
}
