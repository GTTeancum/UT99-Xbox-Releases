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

    // Initialize controller — auto-bind port 0.
    ControllerPort      = 0;
    ControllerHandle    = NULL;
    ControllerConnected = 0;
    appMemzero( &ControllerState,     sizeof(ControllerState)     );
    appMemzero( &PrevControllerState, sizeof(PrevControllerState) );

    // Initialize XInput for port 0.
    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    if( DeviceMask & (1 << ControllerPort) )
    {
        ControllerHandle = XInputOpen( XDEVICE_TYPE_GAMEPAD, ControllerPort, XDEVICE_NO_SLOT, NULL );
        if( ControllerHandle )
        {
            ControllerConnected = 1;
            GXboxLog.Write( "OpenWindow: controller %d opened (handle=0x%08X)", ControllerPort, (DWORD)ControllerHandle );
        }
        else
        {
            GXboxLog.Write( "OpenWindow: XInputOpen failed for port %d", ControllerPort );
        }
    }
    else
    {
        GXboxLog.Write( "OpenWindow: no gamepad on port %d (mask=0x%X)", ControllerPort, DeviceMask );
    }

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
            GXboxLog.Write( "OpenWindow: RenderClass name=%s", TCHAR_TO_ANSI(RenderClass->GetName()) );
            RenDev = ConstructObject<URenderDevice>( RenderClass, this );
            GXboxLog.Write( "OpenWindow: ConstructObject returned RenDev=0x%08X", (DWORD)RenDev );

            // ── Pre-deref: RenDev->Init will crash if ConstructObject returned NULL ──
            if( !RenDev )
                GXboxLog.Write( "OpenWindow: WARNING — RenDev is NULL, Init() will crash" );

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

void UXboxViewport::CloseWindow()
{
    if( ControllerHandle )
    {
        XInputClose( ControllerHandle );
        ControllerHandle = NULL;
    }
}

void UXboxViewport::Destroy()
{
    guard(UXboxViewport::Destroy);
    if( ControllerHandle )
    {
        XInputClose( ControllerHandle );
        ControllerHandle = NULL;
    }
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

void UXboxViewport::PollController()
{
    guard(UXboxViewport::PollController);

    // Try to open the controller if we don't have a handle yet.
    if( !ControllerHandle )
    {
        DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
        if( DeviceMask & (1 << ControllerPort) )
        {
            ControllerHandle = XInputOpen( XDEVICE_TYPE_GAMEPAD, ControllerPort, XDEVICE_NO_SLOT, NULL );
            if( ControllerHandle )
                ControllerConnected = 1;
        }
        if( !ControllerHandle )
            return;
    }

    PrevControllerState = ControllerState;
    DWORD Result = XInputGetState( ControllerHandle, &ControllerState );
    if( Result == ERROR_SUCCESS )
    {
        ControllerConnected = 1;
        ProcessControllerInput( ControllerState.Gamepad );
    }
    else
    {
        // Controller disconnected — close handle so we re-open next poll.
        ControllerConnected = 0;
        XInputClose( ControllerHandle );
        ControllerHandle = NULL;
    }

    unguard;
}

void UXboxViewport::ProcessControllerInput( const XINPUT_GAMEPAD& Pad )
{
    guard(UXboxViewport::ProcessControllerInput);

    UXboxClient* Client = (UXboxClient*)GetOuter();
    if( !Client || !Client->Engine )
        return;

    // ---- Digital buttons (bitmask in wButtons) ----
    // D-pad, Start, Back, Left/Right Thumb clicks.
    struct FDigitalMap { WORD Mask; EInputKey Key; };
    static const FDigitalMap DigitalMap[] =
    {
        { XINPUT_GAMEPAD_DPAD_UP,     IK_JoyPovUp },
        { XINPUT_GAMEPAD_DPAD_DOWN,   IK_JoyPovDown },
        { XINPUT_GAMEPAD_DPAD_LEFT,   IK_JoyPovLeft },
        { XINPUT_GAMEPAD_DPAD_RIGHT,  IK_JoyPovRight },
        { XINPUT_GAMEPAD_START,       IK_Escape },     // Menu / Pause
        { XINPUT_GAMEPAD_BACK,        IK_Tab },        // Scoreboard
        { XINPUT_GAMEPAD_LEFT_THUMB,  IK_Joy5 },       // Crouch
        { XINPUT_GAMEPAD_RIGHT_THUMB, IK_Joy6 },       // Zoom
    };

    WORD CurDigital  = Pad.wButtons;
    WORD PrevDigital = PrevControllerState.Gamepad.wButtons;
    WORD DigChanged  = CurDigital ^ PrevDigital;

    for( INT i = 0; i < ARRAY_COUNT(DigitalMap); i++ )
    {
        if( DigChanged & DigitalMap[i].Mask )
        {
            EInputAction Action = (CurDigital & DigitalMap[i].Mask) ? IST_Press : IST_Release;
            Client->Engine->InputEvent( this, DigitalMap[i].Key, Action, 0.0f );
        }
    }

    // ---- Analog buttons (bAnalogButtons[0..7], 0-255 value, index not bitmask) ----
    // A, B, X, Y, Black, White, Left Trigger, Right Trigger.
    const BYTE AnalogThreshold = XINPUT_GAMEPAD_MAX_CROSSTALK; // 30
    struct FAnalogBtnMap { INT Index; EInputKey Key; };
    static const FAnalogBtnMap AnalogMap[] =
    {
        { XINPUT_GAMEPAD_A,              IK_Joy1 },   // Jump / Accept
        { XINPUT_GAMEPAD_B,              IK_Joy2 },   // Alt-fire
        { XINPUT_GAMEPAD_X,              IK_Joy3 },   // Use
        { XINPUT_GAMEPAD_Y,              IK_Joy4 },   // Weapon switch
        { XINPUT_GAMEPAD_BLACK,          IK_Joy7 },   // Prev weapon
        { XINPUT_GAMEPAD_WHITE,          IK_Joy8 },   // Next weapon
        { XINPUT_GAMEPAD_LEFT_TRIGGER,   IK_Joy9 },   // Fire
        { XINPUT_GAMEPAD_RIGHT_TRIGGER,  IK_Joy10 },  // Alt-fire
    };

    for( INT i = 0; i < ARRAY_COUNT(AnalogMap); i++ )
    {
        UBOOL Now  = Pad.bAnalogButtons[AnalogMap[i].Index] > AnalogThreshold;
        UBOOL Prev = PrevControllerState.Gamepad.bAnalogButtons[AnalogMap[i].Index] > AnalogThreshold;
        if( Now != Prev )
            Client->Engine->InputEvent( this, AnalogMap[i].Key, Now ? IST_Press : IST_Release, 0.0f );
    }

    // ---- Analog sticks ----
    FLOAT DeadZone = Client->DeadZone * 32767.0f;
    FLOAT Sensitivity = Client->ControllerSensitivity;

    // Left stick: movement (IK_JoyX = strafe, IK_JoyY = forward/back).
    FLOAT LX = (FLOAT)Pad.sThumbLX;
    FLOAT LY = (FLOAT)Pad.sThumbLY;
    if( LX > -DeadZone && LX < DeadZone ) LX = 0.0f;
    if( LY > -DeadZone && LY < DeadZone ) LY = 0.0f;
    LX = LX / 32767.0f * Sensitivity;
    LY = LY / 32767.0f * Sensitivity;

    Client->Engine->InputEvent( this, IK_JoyX, IST_Axis, LX );
    Client->Engine->InputEvent( this, IK_JoyY, IST_Axis, LY );

    // Right stick: look (IK_JoyU = yaw, IK_JoyV = pitch).
    FLOAT RX = (FLOAT)Pad.sThumbRX;
    FLOAT RY = (FLOAT)Pad.sThumbRY;
    if( RX > -DeadZone && RX < DeadZone ) RX = 0.0f;
    if( RY > -DeadZone && RY < DeadZone ) RY = 0.0f;
    RX = RX / 32767.0f * Sensitivity;
    RY = RY / 32767.0f * Sensitivity;
    if( Client->InvertVertical )
        RY = -RY;

    Client->Engine->InputEvent( this, IK_JoyU, IST_Axis, RX );
    Client->Engine->InputEvent( this, IK_JoyV, IST_Axis, RY );

    unguard;
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
