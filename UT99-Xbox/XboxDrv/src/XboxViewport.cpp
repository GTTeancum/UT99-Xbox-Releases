// XboxViewport.cpp

extern "C" UBOOL XboxRenderDrawMenuTexture( FSceneNode* Frame, const char* Name, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha );
extern "C" void  XboxRenderDrawMenuRect( FSceneNode* Frame, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, BYTE A );
extern "C" void  XboxRenderDrawMenuRingSlice( FSceneNode* Frame, FLOAT CX, FLOAT CY, FLOAT InnerR, FLOAT OuterR, FLOAT StartAngle, FLOAT EndAngle, BYTE R, BYTE G, BYTE B, BYTE A );
extern "C" void  XboxRenderBeginMenuMeshSlot( FSceneNode* Frame, FLOAT X, FLOAT Y, FLOAT W, FLOAT H );
extern "C" void  XboxRenderEndMenuMeshSlot( FSceneNode* Frame );
extern "C" void  XboxRenderPrepareMenuText( FSceneNode* Frame, const char* Label );
extern "C" void  XboxRenderFinishMenuText( FSceneNode* Frame );

static FLOAT XboxStickAxis( SHORT Raw, FLOAT DeadZone )
{
    FLOAT Delta = (FLOAT)Raw / 32768.0f;
    if( Delta > DeadZone )
        return (Delta - DeadZone) / (1.0f - DeadZone);
    if( Delta < -DeadZone )
        return (Delta + DeadZone) / (1.0f - DeadZone);
    return 0.0f;
}

static HANDLE XboxOpenControllerOnPort( INT Port, DWORD DeviceMask )
{
    HANDLE Handle = XInputOpen( XDEVICE_TYPE_GAMEPAD, Port, XDEVICE_NO_SLOT, NULL );
    if( Handle )
    {
        XINPUT_CAPABILITIES Caps;
        appMemzero( &Caps, sizeof(Caps) );
        DWORD CapsResult = XInputGetCapabilities( Handle, &Caps );
        GXboxLog.Write( "XINPUT open port=%d handle=0x%08X mask=0x%08X caps=0x%08X subtype=0x%02X buttons=0x%04X",
            Port, (DWORD)Handle, DeviceMask, CapsResult, Caps.SubType, Caps.In.Gamepad.wButtons );
    }
    else
    {
        GXboxLog.Write( "XINPUT open failed port=%d mask=0x%08X", Port, DeviceMask );
    }
    return Handle;
}

static INT XboxViewportIndex( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? Cast<UXboxClient>( Viewport->GetOuter() ) : NULL;
    if( !Client )
        return 0;

    for( INT i=0; i<Client->Viewports.Num(); i++ )
    {
        if( Client->Viewports(i) == Viewport )
            return i;
    }
    return 0;
}

static INT XboxViewportControllerPort( UXboxViewport* Viewport )
{
    INT Port = XboxViewportIndex( Viewport );
    INT PortCount = (INT)XGetPortCount();
    if( PortCount <= 0 )
        return 0;
    return Clamp<INT>( Port, 0, PortCount - 1 );
}

static HANDLE XboxOpenViewportController( UXboxViewport* Viewport, DWORD DeviceMask )
{
    if( !Viewport )
        return NULL;

    INT Port = XboxViewportControllerPort( Viewport );
    Viewport->ControllerPort = Port;
    if( !(DeviceMask & (1 << Port)) )
        return NULL;

    return XboxOpenControllerOnPort( Port, DeviceMask );
}

enum EXboxMenuScreen
{
    XMS_Pause,
    XMS_Main,
    XMS_InstantAction,
    XMS_Mutators,
    XMS_SystemLink,
    XMS_PlayerSetup,
    XMS_Settings,
    XMS_ComingSoon
};

struct FXboxMenuState
{
    UBOOL Active;
    EXboxMenuScreen Screen;
    INT PauseFocus;
    INT MainFocus;
    INT InstantFocus;
    INT PlayerFocus;
    INT SettingsFocus;
    INT InstantGameType;
    INT InstantMap[64];
    INT InstantBots;
    INT InstantSkill;
    INT InstantFragLimit;
    INT InstantTimeLimit;
    INT InstantMutatorChoice;
    INT PlayerClass;
    INT PlayerSkin;
    INT PlayerFace;
    INT PlayerVoice;
    INT PlayerTeam;
    DWORD InstantMutatorMask[4];
    FLOAT Pulse;
    UBOOL PausedMatch;
    TCHAR ComingSoonTitle[64];
};

struct FXboxWeaponWheelSlot
{
    const TCHAR* ClassName;
    const TCHAR* DisplayName;
    const TCHAR* IconName;
    const char*  SpriteName;
    FLOAT SpriteScale;
    INT SwitchGroup;
    UTexture* Icon;
    UMesh* PickupMesh;
    FLOAT PickupScale;
    FRotator PickupRotation;
};

static FXboxWeaponWheelSlot GXboxWeaponWheelSlots[] =
{
    { TEXT("Botpack.ImpactHammer"),    TEXT("Impact Hammer"),   TEXT("Botpack.Icons.IconHammer"), "weapon_impact.xui",   1.00f, 1,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.Enforcer"),        TEXT("Enforcer"),        TEXT("Botpack.Icons.IconAutoM"),  "weapon_enforcer.xui", 1.95f, 2,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.UT_BioRifle"),     TEXT("Bio Rifle"),       TEXT("Botpack.Icons.IconBio"),    "weapon_bio.xui",      1.95f, 3,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.ShockRifle"),      TEXT("Shock Rifle"),     TEXT("Botpack.Icons.IconASMD"),   "weapon_shock.xui",    1.95f, 4,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.PulseGun"),        TEXT("Pulse Gun"),       TEXT("Botpack.Icons.IconPulse"),  "weapon_pulse.xui",    1.95f, 5,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.Ripper"),          TEXT("Ripper"),          TEXT("Botpack.Icons.IconRazor"),  "weapon_ripper.xui",   1.95f, 6,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.Minigun2"),        TEXT("Minigun"),         TEXT("Botpack.Icons.IconMini"),   "weapon_mini.xui",     1.95f, 7,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.UT_FlakCannon"),   TEXT("Flak Cannon"),     TEXT("Botpack.Icons.IconFlak"),   "weapon_flak.xui",     1.95f, 8,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.UT_Eightball"),    TEXT("Rocket Launcher"), TEXT("Botpack.Icons.Icon8ball"),  "weapon_rocket.xui",   1.95f, 9,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.SniperRifle"),     TEXT("Sniper Rifle"),    TEXT("Botpack.Icons.IconRifle"),  "weapon_sniper.xui",   1.95f, 10, NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.WarHeadLauncher"), TEXT("Redeemer"),        TEXT("Botpack.Icons.IconWarH"),   "weapon_redeemer.xui", 1.95f, 10, NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.Translocator"),    TEXT("Translocator"),    TEXT("Botpack.Icons.IconTrans"),  "weapon_trans.xui",    1.95f, 0,  NULL, NULL, 1.0f, FRotator(0,0,0) },
    { TEXT("Botpack.ChainSaw"),        TEXT("Chainsaw"),        TEXT("Botpack.Icons.IconSaw"),    "weapon_chainsaw.xui", 1.95f, 1,  NULL, NULL, 1.0f, FRotator(0,0,0) },
};

static UBOOL GXboxWeaponWheelActive[4] = { 0, 0, 0, 0 };
static INT   GXboxWeaponWheelFocus[4]  = { 0, 0, 0, 0 };
static DOUBLE GXboxWeaponWheelPressTime[4][2] = { {0,0}, {0,0}, {0,0}, {0,0} };
static INT   GXboxWeaponWheelLogCount  = 0;
static INT   GXboxWeaponWheelSpriteFailLogCount = 0;
static AActor* GXboxWeaponWheelPreviewActor = NULL;
static ULevel* GXboxWeaponWheelPreviewLevel = NULL;

static FXboxMenuState GXboxMenu =
{
    0,
    XMS_Main,
    0,
    0,
    0,
    0,
    0,
    0,
    { 0 },
    3,
    1,
    2,
    2,
    0,
    0,
    0,
    0,
    0,
    255,
    { 0, 0, 0, 0 },
    0.0f,
    0,
    TEXT("")
};

struct FXboxDiscoveredOption
{
    FString Label;
    FString URLValue;
    FString MapPrefix;
};

struct FXboxPlayerClassOption
{
    FString Label;
    FString URLValue;
    FString MeshName;
    FString MeshPath;
    FString SelectionMesh;
    FString VoiceMetaClass;
    FString DefaultVoice;
    UBOOL bMultiSkinned;
};

static const TCHAR* GXboxSkillLabels[] =
{
    TEXT("NOVICE"),
    TEXT("AVERAGE"),
    TEXT("SKILLED"),
    TEXT("MASTERFUL")
};

static const INT GXboxBotCounts[] =
{
    0, 1, 2, 3, 4, 5, 6, 7
};

static const INT GXboxFragLimits[] =
{
    0, 5, 10, 15, 20, 30, 50
};

static const INT GXboxTimeLimits[] =
{
    0, 5, 10, 15, 20, 30
};

enum EXboxSettingsRow
{
    XSR_LookSensitivity,
    XSR_MoveSensitivity,
    XSR_InvertY,
    XSR_DeadZone,
    XSR_ButtonLayout,
    XSR_MusicVolume,
    XSR_SoundVolume,
    XSR_AnnouncerVolume,
    XSR_Crosshair,
    XSR_HudColor,
    XSR_CrosshairColor,
    XSR_HudOpacity,
    XSR_WeaponHand,
    XSR_AutoSwitch,
    XSR_MatureLanguage,
    XSR_Count
};

static const TCHAR* GXboxButtonLayouts[] =
{
    TEXT("DEFAULT"),
    TEXT("FACE FIRE")
};

static const INT GXboxButtonLayoutValues[] =
{
    0,
    2
};

static const TCHAR* GXboxColorNames[] =
{
    TEXT("BLUE"),
    TEXT("GREEN"),
    TEXT("RED"),
    TEXT("GOLD"),
    TEXT("CYAN"),
    TEXT("WHITE")
};

static const INT GXboxColorTriples[][3] =
{
    { 0, 0, 16 },
    { 0, 16, 0 },
    { 16, 0, 0 },
    { 16, 12, 0 },
    { 0, 16, 16 },
    { 16, 16, 16 }
};

static const TCHAR* GXboxWeaponHands[] =
{
    TEXT("RIGHT"),
    TEXT("CENTER"),
    TEXT("LEFT"),
    TEXT("HIDDEN")
};

static const FLOAT GXboxWeaponHandValues[] =
{
    -1.0f,
    0.0f,
    1.0f,
    2.0f
};

static const TCHAR* GXboxPlayerTeams[] =
{
    TEXT("RED"),
    TEXT("BLUE"),
    TEXT("GREEN"),
    TEXT("GOLD")
};

static INT GXboxSettingsMusicVolume = 255;
static INT GXboxSettingsSoundVolume = 255;
static INT GXboxSettingsAnnouncerVolume = 4;
static INT GXboxSettingsHudColor = 0;
static INT GXboxSettingsCrosshairColor = 1;
static INT GXboxSettingsHudOpacity = 15;
static UBOOL GXboxSettingsLoaded = 0;

static TArray<FXboxDiscoveredOption> GXboxDiscoveredGameTypes;
static TArray<FXboxDiscoveredOption> GXboxDiscoveredMutators;
static TArray<FXboxDiscoveredOption> GXboxDiscoveredMaps;
static UBOOL GXboxMenuRegistryCacheRefreshed = 0;
static UBOOL GXboxDiscoveredListsLoaded = 0;
static INT GXboxDiscoveredMapsGameType = -1;

static TArray<FXboxPlayerClassOption> GXboxPlayerClasses;
static TArray<FXboxDiscoveredOption> GXboxPlayerSkins;
static TArray<FXboxDiscoveredOption> GXboxPlayerFaces;
static TArray<FXboxDiscoveredOption> GXboxPlayerVoices;
static UBOOL GXboxPlayerListsLoaded = 0;
static UBOOL GXboxPlayerStateLoaded = 0;
static INT GXboxPlayerSkinsClass = -1;
static INT GXboxPlayerFacesClass = -1;
static INT GXboxPlayerFacesSkin = -1;
static INT GXboxPlayerVoicesClass = -1;
static AActor* GXboxPlayerPreviewActor = NULL;
static ULevel* GXboxPlayerPreviewLevel = NULL;
static INT GXboxPlayerPreviewClass = -1;
static INT GXboxPlayerPreviewSkin = -1;
static INT GXboxPlayerPreviewFace = -1;
static INT GXboxPlayerPreviewTeam = -1;
static const INT GXboxPlayerPreviewYaw = 32768;

static const INT GXboxSystemLinkBasePort = 9777;
static const INT GXboxSystemLinkPortCount = 4;
static const INT GXboxSystemLinkMaxPeers = 8;
static const FLOAT GXboxSplitDummyRespawnSeconds = 3.0f;

static UBOOL GXboxSplitPending = 0;
static UBOOL GXboxSplitActive = 0;
static INT   GXboxSplitRenderViewport = 0;
static INT   GXboxSplitRenderViewportCount = 1;
static FLOAT GXboxSplitDummyDeathTime[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
static UBOOL GXboxSplitSmokeTravelStarted = 0;
static UBOOL GXboxSplitSmokeActive = 0;
static UBOOL GXboxSplitSmokeFinished = 0;
static DOUBLE GXboxSplitSmokeStartTime = 0.0;
static FVector GXboxSplitSmokeStartLocation(0,0,0);

static UBOOL XboxSetObjectPropertyText( UObject* Object, const TCHAR* PropertyName, const TCHAR* Value );
static UBOOL XboxSetClassDefaultPropertyText( const TCHAR* ClassName, const TCHAR* PropertyName, const TCHAR* Value );

struct FXboxSystemLinkPeer
{
    DWORD Id;
    DWORD Address;
    INT Port;
    FLOAT LastSeen;
    INT Packets;
};

struct FXboxSystemLinkProbe
{
    UBOOL Started;
    UBOOL SocketsReady;
    SOCKET Socket;
    INT LocalPort;
    DWORD LocalId;
    DWORD SendCounter;
    FLOAT LastSendTime;
    FLOAT LastLogTime;
    INT LastError;
    TArray<FXboxSystemLinkPeer> Peers;
};

static FXboxSystemLinkProbe GXboxSystemLink;
static UBOOL GXboxSystemLinkStateInitialized = 0;

static void XboxSystemLinkEnsureState()
{
    if( GXboxSystemLinkStateInitialized )
        return;
    GXboxSystemLink.Socket = INVALID_SOCKET;
    GXboxSystemLinkStateInitialized = 1;
}

static void XboxSystemLinkFormatAddress( DWORD Address, TCHAR* Out, INT OutCount )
{
    BYTE* B = (BYTE*)&Address;
    appSprintf( Out, TEXT("%u.%u.%u.%u"), B[0], B[1], B[2], B[3] );
    Out[OutCount-1] = 0;
}

extern "C" UBOOL XboxSplitIsActive()
{
    return GXboxSplitActive;
}

static void XboxSplitResetRuntime( UXboxClient* Client, const char* Reason )
{
    if( Client )
    {
        for( INT i=Client->Viewports.Num()-1; i>=1; i-- )
        {
            UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(i) );
            if( VP && VP->bXboxSplitDummy )
            {
                if( VP->Actor && VP->Actor->GetLevel() )
                {
                    GXboxLog.Write( "XSPLIT destroying dummy actor index=%d actor=0x%08X reason=%s",
                        i, (DWORD)VP->Actor, Reason ? Reason : "" );
                    VP->Actor->GetLevel()->DestroyActor( VP->Actor, 1 );
                    VP->Actor = NULL;
                }
                GXboxLog.Write( "XSPLIT removing dummy viewport index=%d vp=0x%08X reason=%s",
                    i, (DWORD)VP, Reason ? Reason : "" );
                VP->ConditionalDestroy();
            }
        }

        if( Client->Viewports.Num() > 0 )
        {
            UXboxViewport* Primary = Cast<UXboxViewport>( Client->Viewports(0) );
            if( Primary )
            {
                Primary->bXboxSplitDummy = 0;
                Primary->ViewX = 0;
                Primary->ViewY = 0;
                Primary->SizeX = Primary->ViewWidth  = XBOX_SCREEN_WIDTH;
                Primary->SizeY = Primary->ViewHeight = XBOX_SCREEN_HEIGHT;
            }
        }
    }

    GXboxSplitPending = 0;
    GXboxSplitActive = 0;
    GXboxSplitRenderViewport = 0;
    GXboxSplitRenderViewportCount = 1;
    for( INT i=0; i<4; i++ )
        GXboxSplitDummyDeathTime[i] = -1.0f;

    GXboxLog.Write( "XSPLIT reset reason=%s remainingViewports=%d",
        Reason ? Reason : "",
        Client ? Client->Viewports.Num() : -1 );
}

static UBOOL XboxSplitSmokeEnabled()
{
    return GetFileAttributesA( "D:\\XboxSplitSmoke.ini" ) != 0xFFFFFFFF;
}

static void XboxSplitSmokeMaybeQueue( UXboxClient* Client )
{
    if( !XboxSplitSmokeEnabled() || GXboxSplitSmokeTravelStarted || GXboxSplitPending || GXboxSplitActive )
        return;
    if( !Client || !Client->Engine || Client->Viewports.Num() <= 0 || !Client->Viewports(0) )
        return;

    XboxSetClassDefaultPropertyText( TEXT("Botpack.DeathMatchPlus"), TEXT("InitialBots"), TEXT("0") );
    XboxSetClassDefaultPropertyText( TEXT("Botpack.DeathMatchPlus"), TEXT("MinPlayers"), TEXT("0") );

    GXboxSplitSmokeTravelStarted = 1;
    GXboxSplitPending = 1;
    GXboxSplitActive = 0;

    const TCHAR* URL = TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=0?MaxPlayers=4?Difficulty=1?Name=SmokeP1?Class=Botpack.TMale2?team=0?skin=SoldierSkins.blkt?Face=SoldierSkins.Othello?Voice=BotPack.VoiceMaleTwo");
    GXboxLog.Write( "XSPLIT SELFTEST queued travel: %s", TCHAR_TO_ANSI(URL) );
    Client->Engine->SetClientTravel( Client->Viewports(0), const_cast<TCHAR*>(URL), 0, TRAVEL_Absolute );
}

extern "C" void XboxSplitBeginRenderFrame( INT ViewportCount )
{
    GXboxSplitRenderViewport = 0;
    GXboxSplitRenderViewportCount = Max<INT>( ViewportCount, 1 );
}

extern "C" void XboxSplitSetRenderViewport( INT ViewportIndex )
{
    GXboxSplitRenderViewport = ViewportIndex;
}

extern "C" UBOOL XboxSplitShouldClearRenderLock()
{
    return !GXboxSplitActive || GXboxSplitRenderViewport == 0;
}

extern "C" void XboxViewportApplyViewRegion( UViewport* Viewport, FSceneNode* Frame )
{
    UXboxViewport* XboxViewport = Cast<UXboxViewport>( Viewport );
    if( !XboxViewport || !Frame )
        return;

    Frame->XB = XboxViewport->ViewX;
    Frame->YB = XboxViewport->ViewY;
    Frame->X  = Max<INT>( XboxViewport->ViewWidth,  1 );
    Frame->Y  = Max<INT>( XboxViewport->ViewHeight, 1 );
    Frame->ComputeRenderSize();
}

extern "C" UBOOL XboxViewportShouldPostRenderPlayer( UViewport* Viewport )
{
    UXboxViewport* XboxViewport = Cast<UXboxViewport>( Viewport );
    return !GXboxSplitActive || !XboxViewport || !XboxViewport->bXboxSplitDummy;
}

static void XboxViewportInputAxis( UXboxViewport* Viewport, const TCHAR* AxisName, FLOAT Delta, FLOAT Speed )
{
    if( !Viewport || !Viewport->Input || !Viewport->Actor )
        return;

    TCHAR Cmd[128];
    appSprintf( Cmd, TEXT("AXIS %s SPEED=%f"), AxisName, Speed );
    Viewport->Input->SetInputAction( IST_Axis, Delta );
    Viewport->Input->Exec( Cmd, *GLog );
    Viewport->Input->SetInputAction( IST_None );
}

static void XboxSplitSmokeFeedInput( UXboxViewport* Viewport )
{
    if( !GXboxSplitActive || !XboxSplitSmokeEnabled() || !Viewport || Viewport->bXboxSplitDummy || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    Player->bShowMenu = 0;
    Player->bSpecialMenu = 0;
    if( Player->Level && Player->Level->Pauser != TEXT("") )
        Player->Level->Pauser = TEXT("");

    // Feed through UInput so we hit the reflected UnrealScript property offsets.
    XboxViewportInputAxis( Viewport, TEXT("aBaseY"), 100.0f, 2.0f );
    Player->bReadyToPlay = 1;

    static INT FeedLogCount = 0;
    if( FeedLogCount < 12 )
    {
        FeedLogCount++;
        GXboxLog.Write( "XSPLIT SELFTEST feed #%d vp=0x%08X actor=0x%08X player=0x%08X playerActor=0x%08X loc=%.1f,%.1f,%.1f axes=%.2f/%.2f/%.2f",
            FeedLogCount,
            (DWORD)Viewport,
            (DWORD)Player,
            (DWORD)Player->Player,
            Player->Player ? (DWORD)Player->Player->Actor : 0,
            Player->Location.X, Player->Location.Y, Player->Location.Z,
            Player->aForward, Player->aBaseY, Player->aStrafe );
    }
}

static void XboxSplitPreparePlayer( APlayerPawn* Player, UBOOL bDummy )
{
    if( !Player )
        return;

    Player->bShowMenu = 0;
    Player->bSpecialMenu = 0;
    Player->bReadyToPlay = 1;
    Player->PlayerReStartState = FName(TEXT("PlayerWalking"));
    Player->GotoState( FName(TEXT("PlayerWalking")) );
    if( bDummy )
        Player->myHUD = NULL;
}

static void XboxSplitConfigureViewports( UXboxClient* Client )
{
    if( !Client || Client->Viewports.Num() <= 0 )
        return;

    UViewport* Primary = Client->Viewports(0);
    if( !Primary || !Primary->RenDev )
        return;

    while( Client->Viewports.Num() < 4 )
    {
        TCHAR Name[32];
        appSprintf( Name, TEXT("XboxSplit%i"), Client->Viewports.Num()+1 );
        UXboxViewport* NewVP = Cast<UXboxViewport>( Client->NewViewport( FName(Name, FNAME_Add) ) );
        if( !NewVP )
            break;
        NewVP->RenDev = Primary->RenDev;
        NewVP->ColorBytes = Primary->ColorBytes;
        NewVP->bXboxSplitDummy = 1;
        GXboxLog.Write( "XSPLIT created dummy viewport index=%d vp=0x%08X sharedRenDev=0x%08X",
            Client->Viewports.Num()-1, (DWORD)NewVP, (DWORD)NewVP->RenDev );
    }

    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    const INT HalfW = XBOX_SCREEN_WIDTH / 2;
    const INT HalfH = XBOX_SCREEN_HEIGHT / 2;
    for( INT i=0; i<Client->Viewports.Num() && i<4; i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(i) );
        if( !VP )
            continue;
        if( VP->ControllerHandle )
        {
            XInputClose( VP->ControllerHandle );
            VP->ControllerHandle = NULL;
        }
        VP->ControllerPort = i;
        VP->ControllerConnected = 0;
        appMemzero( &VP->ControllerState,     sizeof(VP->ControllerState)     );
        appMemzero( &VP->PrevControllerState, sizeof(VP->PrevControllerState) );
        VP->RenDev = Primary->RenDev;
        VP->SizeX = VP->ViewWidth = HalfW;
        VP->SizeY = VP->ViewHeight = HalfH;
        VP->ViewX = (i & 1) ? HalfW : 0;
        VP->ViewY = (i & 2) ? HalfH : 0;
        VP->ColorBytes = Primary->ColorBytes ? Primary->ColorBytes : 4;
        VP->bXboxSplitDummy = (i > 0) && !(DeviceMask & (1 << i));
        GXboxLog.Write( "XSPLIT viewport=%d controllerPort=%d devicePresent=%d dummy=%d",
            i, VP->ControllerPort, (DeviceMask & (1 << i)) ? 1 : 0, VP->bXboxSplitDummy ? 1 : 0 );
    }

    for( INT i=0; i<4; i++ )
        GXboxSplitDummyDeathTime[i] = -1.0f;
    GXboxLog.Write( "XSPLIT configured viewports=%d layout=2x2", Client->Viewports.Num() );
}

static void XboxSplitSuppressBots( ULevel* Level )
{
    if( !Level || !Level->GetLevelInfo() || !Level->GetLevelInfo()->Game )
        return;

    UObject* Game = Level->GetLevelInfo()->Game;
    XboxSetObjectPropertyText( Game, TEXT("InitialBots"), TEXT("0") );
    XboxSetObjectPropertyText( Game, TEXT("RemainingBots"), TEXT("0") );
    XboxSetObjectPropertyText( Game, TEXT("MinPlayers"), TEXT("0") );
    XboxSetObjectPropertyText( Game, TEXT("bRequireReady"), TEXT("False") );
    XboxSetObjectPropertyText( Game, TEXT("CountDown"), TEXT("0") );
    if( Level->GetLevelInfo()->Pauser != TEXT("") )
        Level->GetLevelInfo()->Pauser = TEXT("");

    UClass* BotClass = UObject::StaticLoadClass( APawn::StaticClass(), NULL, TEXT("Botpack.Bot"), NULL, LOAD_NoWarn, NULL );
    INT Removed = 0;
    if( BotClass )
    {
        for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; )
        {
            APawn* Next = Pawn->nextPawn;
            if( Pawn->IsA(BotClass) )
            {
                Level->DestroyActor( Pawn );
                Removed++;
            }
            Pawn = Next;
        }
    }

    if( Removed )
        GXboxLog.Write( "XSPLIT removed %d stock bot pawn(s)", Removed );
}

static UBOOL XboxSplitCurrentMapReady( UClient* InClient )
{
    if( !InClient || InClient->Viewports.Num() != 1 )
        return 0;

    UViewport* Primary = InClient->Viewports(0);
    ULevel* Level = (Primary && Primary->Actor) ? Primary->Actor->GetLevel() : NULL;
    if( !Level || !Level->GetLevelInfo() )
        return 0;

    if( !Level->GetLevelInfo()->Game )
        return 0;

    return appStrnicmp( *Level->URL.Map, TEXT("DM-"), 3 ) == 0;
}

extern "C" void XboxSplitTryActivate( UClient* InClient )
{
    XboxSplitSmokeMaybeQueue( Cast<UXboxClient>(InClient) );

    if( !GXboxSplitPending || GXboxSplitActive )
        return;

    UXboxClient* Client = Cast<UXboxClient>( InClient );
    if( !Client || !XboxSplitCurrentMapReady(Client) )
        return;

    UViewport* Primary = Client->Viewports(0);
    ULevel* Level = (Primary && Primary->Actor) ? Primary->Actor->GetLevel() : NULL;
    if( !Level )
        return;

    GXboxLog.Write( "XSPLIT activating after map load url=%s", TCHAR_TO_ANSI(*Level->URL.String()) );

    XboxSplitSuppressBots( Level );
    XboxSplitConfigureViewports( Client );

    for( INT i=1; i<Client->Viewports.Num() && i<4; i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(i) );
        if( !VP )
            continue;

        FString Error;
        FString DummyURLText = FString::Printf
        (
            TEXT("%s?Name=Dummy%i?Team=%i"),
            *Level->URL.String(),
            i + 1,
            i & 3
        );
        FURL DummyURL( NULL, *DummyURLText, TRAVEL_Absolute );
        GXboxLog.Write( "XSPLIT spawning dummy viewport=%d url=%s", i, TCHAR_TO_ANSI(*DummyURL.String()) );
        if( !Level->SpawnPlayActor( VP, ROLE_SimulatedProxy, DummyURL, Error ) )
            GXboxLog.Write( "XSPLIT dummy spawn failed viewport=%d error=%s", i, TCHAR_TO_ANSI(*Error) );
        else if( VP->Actor )
        {
            XboxSplitPreparePlayer( VP->Actor, VP->bXboxSplitDummy );
            GXboxLog.Write( "XSPLIT dummy spawned viewport=%d actor=0x%08X class=%s", i, (DWORD)VP->Actor, TCHAR_TO_ANSI(VP->Actor->GetClass()->GetName()) );
        }
    }

    GXboxSplitPending = 0;
    GXboxSplitActive = 1;
    XboxSplitSuppressBots( Level );
    if( Primary && Primary->Actor )
        XboxSplitPreparePlayer( Primary->Actor, 0 );
    for( INT i=1; i<Client->Viewports.Num() && i<4; i++ )
        if( Client->Viewports(i) && Client->Viewports(i)->Actor )
        {
            UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(i) );
            XboxSplitPreparePlayer( Client->Viewports(i)->Actor, VP ? VP->bXboxSplitDummy : 1 );
        }

    UObject* Game = Level->GetLevelInfo() ? Level->GetLevelInfo()->Game : NULL;
    UFunction* StartMatch = Game ? Game->FindFunction( FName(TEXT("StartMatch"), FNAME_Find) ) : NULL;
    if( StartMatch )
    {
        Game->ProcessEvent( StartMatch, NULL );
        GXboxLog.Write( "XSPLIT StartMatch requested for local dummy match" );
    }

    if( Level->GetLevelInfo() && Level->GetLevelInfo()->Pauser != TEXT("") )
        Level->GetLevelInfo()->Pauser = TEXT("");
    for( INT i=0; i<Client->Viewports.Num() && i<4; i++ )
    {
        UViewport* VP = Client->Viewports(i);
        APlayerPawn* Player = VP ? VP->Actor : NULL;
        if( !Player )
            continue;

        UXboxViewport* XVP = Cast<UXboxViewport>( VP );
        XboxSplitPreparePlayer( Player, XVP ? XVP->bXboxSplitDummy : (i > 0) );
        const TCHAR* StateName = (Player->GetStateFrame() && Player->GetStateFrame()->StateNode)
            ? *Player->GetStateFrame()->StateNode->GetFName()
            : TEXT("None");
        GXboxLog.Write( "XSPLIT player ready viewport=%d actor=0x%08X state=%s physics=%d hud=0x%08X showMenu=%d pauser=%s",
            i, (DWORD)Player, TCHAR_TO_ANSI(StateName), (INT)Player->Physics, (DWORD)Player->myHUD,
            Player->bShowMenu ? 1 : 0,
            Player->Level ? TCHAR_TO_ANSI(*Player->Level->Pauser) : "" );
    }

    GXboxLog.Write( "XSPLIT active viewports=%d", Client->Viewports.Num() );
}

static void XboxSplitSmokeCheck( UClient* InClient )
{
    if( !GXboxSplitActive || !XboxSplitSmokeEnabled() || GXboxSplitSmokeFinished || !InClient || InClient->Viewports.Num() <= 0 )
        return;

    UViewport* VP = InClient->Viewports(0);
    APlayerPawn* Player = VP ? VP->Actor : NULL;
    if( !Player )
        return;

    DOUBLE Now = appSeconds();
    if( !GXboxSplitSmokeActive )
    {
        GXboxSplitSmokeActive = 1;
        GXboxSplitSmokeStartTime = Now;
        GXboxSplitSmokeStartLocation = Player->Location;
        const TCHAR* StateName = (Player->GetStateFrame() && Player->GetStateFrame()->StateNode)
            ? *Player->GetStateFrame()->StateNode->GetFName()
            : TEXT("None");
        GXboxLog.Write( "XSPLIT SELFTEST begin actor=0x%08X state=%s loc=%.1f,%.1f,%.1f",
            (DWORD)Player,
            TCHAR_TO_ANSI(StateName),
            Player->Location.X, Player->Location.Y, Player->Location.Z );
        return;
    }

    FLOAT DistSq = (Player->Location - GXboxSplitSmokeStartLocation).SizeSquared();
    DOUBLE Elapsed = Now - GXboxSplitSmokeStartTime;
    if( Elapsed >= 0.75 && DistSq > 25.0f )
    {
        GXboxSplitSmokeFinished = 1;
        GXboxLog.Write( "XSPLIT SELFTEST PASS movement elapsed=%.2f distSq=%.1f loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f acc=%.1f,%.1f,%.1f",
            Elapsed,
            DistSq,
            Player->Location.X, Player->Location.Y, Player->Location.Z,
            Player->Velocity.X, Player->Velocity.Y, Player->Velocity.Z,
            Player->Acceleration.X, Player->Acceleration.Y, Player->Acceleration.Z );
    }
    else if( Elapsed >= 3.0 )
    {
        GXboxSplitSmokeFinished = 1;
        GXboxLog.Write( "XSPLIT SELFTEST FAIL movement elapsed=%.2f distSq=%.1f loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f acc=%.1f,%.1f,%.1f axes=%.1f/%.1f/%.1f/%.1f/%.1f menu=%d pauser=%s",
            Elapsed,
            DistSq,
            Player->Location.X, Player->Location.Y, Player->Location.Z,
            Player->Velocity.X, Player->Velocity.Y, Player->Velocity.Z,
            Player->Acceleration.X, Player->Acceleration.Y, Player->Acceleration.Z,
            Player->aForward, Player->aBaseY, Player->aStrafe, Player->aTurn, Player->aLookUp,
            Player->bShowMenu ? 1 : 0,
            Player->Level ? TCHAR_TO_ANSI(*Player->Level->Pauser) : "" );
    }
}

extern "C" void XboxSplitTickDummies( UClient* InClient )
{
    if( !GXboxSplitActive || !InClient || InClient->Viewports.Num() < 2 )
    {
        XboxSplitSmokeCheck( InClient );
        return;
    }

    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    for( INT i=1; i<InClient->Viewports.Num() && i<4; i++ )
    {
        UViewport* VP = InClient->Viewports(i);
        UXboxViewport* XVP = Cast<UXboxViewport>( VP );
        if( !XVP )
            continue;

        UBOOL bShouldBeDummy = !(DeviceMask & (1 << i));
        if( XVP->bXboxSplitDummy != bShouldBeDummy )
        {
            XVP->bXboxSplitDummy = bShouldBeDummy;
            if( bShouldBeDummy )
            {
                if( XVP->ControllerHandle )
                {
                    XInputClose( XVP->ControllerHandle );
                    XVP->ControllerHandle = NULL;
                }
                XVP->ControllerConnected = 0;
                appMemzero( &XVP->ControllerState,     sizeof(XVP->ControllerState)     );
                appMemzero( &XVP->PrevControllerState, sizeof(XVP->PrevControllerState) );
            }
            if( XVP->Actor )
                XboxSplitPreparePlayer( XVP->Actor, XVP->bXboxSplitDummy );
            GXboxLog.Write( "XSPLIT viewport=%d port=%d dummy=%d deviceMask=0x%08X",
                i, XVP->ControllerPort, XVP->bXboxSplitDummy ? 1 : 0, DeviceMask );
        }

        if( !XVP->bXboxSplitDummy )
            continue;

        APlayerPawn* Player = VP ? VP->Actor : NULL;
        if( !Player )
            continue;
        FLOAT Now = Player->Level ? Player->Level->TimeSeconds : 0.0f;

        Player->bShowMenu = 0;
        Player->bSpecialMenu = 0;
        Player->myHUD = NULL;
        Player->aStrafe = 0.0f;
        Player->aBaseY  = 0.0f;
        Player->aForward = 0.0f;
        Player->aTurn   = 0.0f;
        Player->aLookUp = 0.0f;
        Player->bFire   = 0;
        Player->bAltFire = 0;
        Player->bDuck   = 0;
        Player->bReadyToPlay = 1;

        UBOOL bDead = Player->Health <= 0 || Player->bHidden;
        if( bDead )
        {
            if( GXboxSplitDummyDeathTime[i] < 0.0f )
            {
                GXboxSplitDummyDeathTime[i] = Now;
                GXboxLog.Write( "XSPLIT dummy dead index=%d player=0x%08X health=%d hidden=%d",
                    i, (DWORD)Player, Player->Health, Player->bHidden );
            }
            if( Now - GXboxSplitDummyDeathTime[i] >= GXboxSplitDummyRespawnSeconds )
            {
                UFunction* Restart = Player->FindFunction( FName(TEXT("ServerReStartPlayer"), FNAME_Find) );
                if( Restart )
                {
                    Player->ProcessEvent( Restart, NULL );
                    GXboxLog.Write( "XSPLIT dummy restart requested index=%d player=0x%08X", i, (DWORD)Player );
                }
                GXboxSplitDummyDeathTime[i] = Now;
            }
        }
        else
        {
            GXboxSplitDummyDeathTime[i] = -1.0f;
    }

    XboxSplitSmokeCheck( InClient );
}
}

static UBOOL XboxSystemLinkInitSockets()
{
    XboxSystemLinkEnsureState();
    if( GXboxSystemLink.SocketsReady )
        return 1;

    XNetStartupParams Params;
    appMemzero( &Params, sizeof(Params) );
    Params.cfgSizeOfStruct = sizeof(Params);
    Params.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
    Params.cfgPrivatePoolSizeInPages = 12;
    Params.cfgSockMaxSockets = 16;
    Params.cfgSockDefaultRecvBufsizeInK = 16;
    Params.cfgSockDefaultSendBufsizeInK = 16;
    INT XNetResult = XNetStartup( &Params );

    WSADATA WsaData;
    appMemzero( &WsaData, sizeof(WsaData) );
    INT WsaResult = WSAStartup( MAKEWORD(2,2), &WsaData );
    GXboxSystemLink.SocketsReady = (XNetResult == 0 && WsaResult == 0);
    GXboxSystemLink.LastError = GXboxSystemLink.SocketsReady ? 0 : (WsaResult ? WsaResult : XNetResult);
    GXboxLog.Write( "XSL probe net init xnet=%d wsa=%d ready=%d", XNetResult, WsaResult, GXboxSystemLink.SocketsReady );
    return GXboxSystemLink.SocketsReady;
}

static void XboxSystemLinkStop()
{
    XboxSystemLinkEnsureState();
    if( GXboxSystemLink.Socket != INVALID_SOCKET )
    {
        closesocket( GXboxSystemLink.Socket );
        GXboxSystemLink.Socket = INVALID_SOCKET;
    }
    GXboxSystemLink.Started = 0;
    GXboxSystemLink.LocalPort = 0;
    GXboxLog.Write( "XSL probe stopped" );
}

static UBOOL XboxSystemLinkStart()
{
    XboxSystemLinkEnsureState();
    if( GXboxSystemLink.Started )
        return 1;

    if( !XboxSystemLinkInitSockets() )
        return 0;

    GXboxSystemLink.Socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( GXboxSystemLink.Socket == INVALID_SOCKET )
    {
        GXboxSystemLink.LastError = WSAGetLastError();
        GXboxLog.Write( "XSL probe socket failed err=%d", GXboxSystemLink.LastError );
        return 0;
    }

    BOOL Yes = TRUE;
    setsockopt( GXboxSystemLink.Socket, SOL_SOCKET, SO_BROADCAST, (char*)&Yes, sizeof(Yes) );
    setsockopt( GXboxSystemLink.Socket, SOL_SOCKET, SO_REUSEADDR, (char*)&Yes, sizeof(Yes) );

    DWORD NoBlock = 1;
    ioctlsocket( GXboxSystemLink.Socket, FIONBIO, &NoBlock );

    UBOOL Bound = 0;
    for( INT i=0; i<GXboxSystemLinkPortCount; i++ )
    {
        sockaddr_in Addr;
        appMemzero( &Addr, sizeof(Addr) );
        Addr.sin_family = AF_INET;
        Addr.sin_addr.s_addr = INADDR_ANY;
        Addr.sin_port = htons( (u_short)(GXboxSystemLinkBasePort + i) );
        if( bind( GXboxSystemLink.Socket, (sockaddr*)&Addr, sizeof(Addr) ) == 0 )
        {
            GXboxSystemLink.LocalPort = GXboxSystemLinkBasePort + i;
            Bound = 1;
            break;
        }
    }

    if( !Bound )
    {
        GXboxSystemLink.LastError = WSAGetLastError();
        GXboxLog.Write( "XSL probe bind failed err=%d", GXboxSystemLink.LastError );
        XboxSystemLinkStop();
        return 0;
    }

    if( !GXboxSystemLink.LocalId )
        GXboxSystemLink.LocalId = GetTickCount() ^ (DWORD)&GXboxSystemLink;
    GXboxSystemLink.Started = 1;
    GXboxSystemLink.LastSendTime = 0.0f;
    GXboxSystemLink.LastLogTime = 0.0f;
    GXboxSystemLink.Peers.Empty();
    GXboxLog.Write( "XSL probe started id=0x%08X port=%d", GXboxSystemLink.LocalId, GXboxSystemLink.LocalPort );
    return 1;
}

static void XboxSystemLinkSendProbe()
{
    if( !GXboxSystemLink.Started || GXboxSystemLink.Socket == INVALID_SOCKET )
        return;

    char Packet[96];
    sprintf( Packet, "UTXSL1|%08X|%d|%lu", GXboxSystemLink.LocalId, GXboxSystemLink.LocalPort, GXboxSystemLink.SendCounter++ );
    INT PacketLen = 0;
    while( Packet[PacketLen] )
        PacketLen++;

    for( INT i=0; i<GXboxSystemLinkPortCount; i++ )
    {
        sockaddr_in To;
        appMemzero( &To, sizeof(To) );
        To.sin_family = AF_INET;
        To.sin_addr.s_addr = INADDR_BROADCAST;
        To.sin_port = htons( (u_short)(GXboxSystemLinkBasePort + i) );
        INT Sent = sendto( GXboxSystemLink.Socket, Packet, PacketLen, 0, (sockaddr*)&To, sizeof(To) );
        if( Sent == SOCKET_ERROR )
            GXboxSystemLink.LastError = WSAGetLastError();
    }
}

static void XboxSystemLinkRecordPeer( DWORD Id, DWORD Address, INT Port, FLOAT Now )
{
    if( Id == GXboxSystemLink.LocalId )
        return;

    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( Peer.Id == Id )
        {
            Peer.Address = Address;
            Peer.Port = Port;
            Peer.LastSeen = Now;
            Peer.Packets++;
            return;
        }
    }

    if( GXboxSystemLink.Peers.Num() >= GXboxSystemLinkMaxPeers )
        GXboxSystemLink.Peers.Remove( 0 );

    FXboxSystemLinkPeer& Peer = *new(GXboxSystemLink.Peers)FXboxSystemLinkPeer;
    Peer.Id = Id;
    Peer.Address = Address;
    Peer.Port = Port;
    Peer.LastSeen = Now;
    Peer.Packets = 1;

    TCHAR AddrText[32];
    XboxSystemLinkFormatAddress( Address, AddrText, ARRAY_COUNT(AddrText) );
    GXboxLog.Write( "XSL peer discovered id=0x%08X addr=%s port=%d", Id, TCHAR_TO_ANSI(AddrText), Port );
}

static void XboxSystemLinkTick()
{
    XboxSystemLinkEnsureState();
    if( GXboxMenu.Screen != XMS_SystemLink )
        return;

    if( !GXboxSystemLink.Started )
        XboxSystemLinkStart();
    if( !GXboxSystemLink.Started )
        return;

    FLOAT Now = appSeconds();
    if( Now - GXboxSystemLink.LastSendTime >= 1.0f )
    {
        XboxSystemLinkSendProbe();
        GXboxSystemLink.LastSendTime = Now;
    }

    for( ;; )
    {
        char Buffer[128];
        sockaddr_in From;
        INT FromSize = sizeof(From);
        INT Count = recvfrom( GXboxSystemLink.Socket, Buffer, sizeof(Buffer)-1, 0, (sockaddr*)&From, &FromSize );
        if( Count == SOCKET_ERROR )
        {
            INT Err = WSAGetLastError();
            if( Err != WSAEWOULDBLOCK )
                GXboxSystemLink.LastError = Err;
            break;
        }

        Buffer[Count] = 0;
        DWORD Id = 0;
        INT Port = 0;
        DWORD Counter = 0;
        if( sscanf( Buffer, "UTXSL1|%08X|%d|%lu", &Id, &Port, &Counter ) == 3 )
            XboxSystemLinkRecordPeer( Id, From.sin_addr.s_addr, Port, Now );
    }

    if( Now - GXboxSystemLink.LastLogTime >= 5.0f )
    {
        GXboxSystemLink.LastLogTime = Now;
        GXboxLog.Write( "XSL probe status id=0x%08X port=%d peers=%d sent=%lu lastErr=%d",
            GXboxSystemLink.LocalId,
            GXboxSystemLink.LocalPort,
            GXboxSystemLink.Peers.Num(),
            GXboxSystemLink.SendCounter,
            GXboxSystemLink.LastError );
    }
}

static void XboxMenuEnsureRegistryCache()
{
    if( GXboxMenuRegistryCacheRefreshed )
        return;

    TArray<FRegistryObjectInfo> Dummy;
    UObject::GetRegistryObjects( Dummy, UClass::StaticClass(), UObject::StaticClass(), 1 );
    GXboxMenuRegistryCacheRefreshed = 1;
    GXboxLog.Write( "XMENU refreshed .int registry cache for menu discovery" );
}

static void XboxMenuResetPlayerPreviewCache()
{
    GXboxPlayerPreviewActor = NULL;
    GXboxPlayerPreviewLevel = NULL;
    GXboxPlayerPreviewClass = -1;
    GXboxPlayerPreviewSkin = -1;
    GXboxPlayerPreviewFace = -1;
    GXboxPlayerPreviewTeam = -1;
}

static void XboxMenuDestroyPlayerPreview()
{
    if( GXboxPlayerPreviewActor )
        GXboxPlayerPreviewActor->Destroy();
    XboxMenuResetPlayerPreviewCache();
}

static UTexture* GXboxMenuPreviewTexture = NULL;
static TCHAR     GXboxMenuPreviewMap[64] = TEXT("");

static void XboxMenuStripDescriptionLabel( const FString& Description, FString& OutLabel )
{
    OutLabel = Description;
    const TCHAR* Comma = appStrchr( *OutLabel, ',' );
    if( Comma )
    {
        INT Len = Comma - *OutLabel;
        OutLabel = OutLabel.Left( Len );
    }
    if( OutLabel.Len() == 0 )
        OutLabel = TEXT("UNKNOWN");
    OutLabel = OutLabel.Caps();
}

static void XboxMenuObjectItemName( const FString& FullName, FString& OutItem )
{
    OutItem = FullName;
    const TCHAR* Dot = NULL;
    for( const TCHAR* Scan=*FullName; *Scan; Scan++ )
        if( *Scan == '.' )
            Dot = Scan;
    if( Dot )
        OutItem = Dot + 1;
}

static UBOOL XboxMenuNameMatches( const FString& Candidate, const TCHAR* Wanted )
{
    if( !Wanted || !Wanted[0] )
        return 1;
    if( appStricmp( *Candidate, Wanted ) == 0 )
        return 1;

    FString Item;
    XboxMenuObjectItemName( Candidate, Item );
    return appStricmp( *Item, Wanted ) == 0;
}

static void XboxMenuCollectIntObjects( TArray<FRegistryObjectInfo>& Out, const TCHAR* WantedClass, const TCHAR* WantedMetaClass )
{
    Out.Empty();
    if( !GSys || !GConfig )
        return;

    TCHAR Buffer[32767];
    INT LoggedSearches = 0;
    INT LoggedFiles = 0;
    INT ParsedObjects = 0;
    INT ClassFiltered = 0;
    INT MetaFiltered = 0;
    for( INT i=0; i<GSys->Paths.Num(); i++ )
    {
        TCHAR Filename[256];
        appSprintf( Filename, TEXT("%s%s"), appBaseDir(), *GSys->Paths(i) );
        TCHAR* Wild = appStrstr( Filename, TEXT("*.") );
        if( !Wild )
            continue;

        appSprintf( Wild, TEXT("*.int") );
        TArray<FString> Files = GFileManager->FindFiles( Filename, 1, 0 );
        if( LoggedSearches++ < 4 )
            GXboxLog.Write( "XMENU int scan search path=%s files=%d", TCHAR_TO_ANSI(Filename), Files.Num() );
        for( INT j=0; j<Files.Num(); j++ )
        {
            TCHAR IntPath[256];
            if( appStrchr( *Files(j), ':' ) )
                appStrncpy( IntPath, *Files(j), ARRAY_COUNT(IntPath) );
            else
                appSprintf( IntPath, TEXT("%s%s"), appBaseDir(), *Files(j) );
            IntPath[ARRAY_COUNT(IntPath)-1] = 0;

            UBOOL bPublic = GConfig->GetSection( TEXT("Public"), Buffer, ARRAY_COUNT(Buffer), IntPath );
            if( LoggedFiles++ < 8 )
                GXboxLog.Write( "XMENU int scan file=%s public=%d", TCHAR_TO_ANSI(IntPath), bPublic );
            if( !bPublic )
                continue;

            TCHAR* Next;
            for( TCHAR* Key=Buffer; *Key; Key=Next )
            {
                Next = Key + appStrlen(Key) + 1;
                TCHAR* Value = appStrchr(Key,'=');
                if( !Value )
                    continue;
                *Value++ = 0;
                if( appStricmp(Key,TEXT("Object")) != 0 )
                    continue;
                if( *Value == '(' )
                    *Value++ = 0;
                INT Len = appStrlen(Value);
                if( Len > 0 && Value[Len-1] == ')' )
                    Value[Len-1] = 0;

                FRegistryObjectInfo Info;
                Parse( Value, TEXT("Name="), Info.Object );
                Parse( Value, TEXT("Class="), Info.Class );
                Parse( Value, TEXT("MetaClass="), Info.MetaClass );
                Parse( Value, TEXT("Description="), Info.Description );
                ParsedObjects++;
                if( !XboxMenuNameMatches( Info.Class, WantedClass ) )
                {
                    ClassFiltered++;
                    continue;
                }
                if( WantedMetaClass && WantedMetaClass[0] && !XboxMenuNameMatches( Info.MetaClass, WantedMetaClass ) )
                {
                    MetaFiltered++;
                    continue;
                }

                UBOOL bDuplicate = 0;
                for( INT Existing=0; Existing<Out.Num(); Existing++ )
                    if( appStricmp( *Out(Existing).Object, *Info.Object ) == 0 )
                        bDuplicate = 1;
                if( !bDuplicate )
                    new(Out)FRegistryObjectInfo(Info);
            }
        }
    }

    GXboxLog.Write( "XMENU direct .int scan class=%s meta=%s parsed=%d classFiltered=%d metaFiltered=%d count=%d",
        WantedClass ? TCHAR_TO_ANSI(WantedClass) : "",
        WantedMetaClass ? TCHAR_TO_ANSI(WantedMetaClass) : "",
        ParsedObjects,
        ClassFiltered,
        MetaFiltered,
        Out.Num() );
}

static void XboxMenuAddFallbackGameType( const TCHAR* Label, const TCHAR* ClassName, const TCHAR* Prefix )
{
    FXboxDiscoveredOption& Option = *new(GXboxDiscoveredGameTypes)FXboxDiscoveredOption;
    Option.Label = Label;
    Option.URLValue = ClassName;
    Option.MapPrefix = Prefix;
}

static void XboxMenuAddFallbackMutator( const TCHAR* Label, const TCHAR* ClassName )
{
    FXboxDiscoveredOption& Option = *new(GXboxDiscoveredMutators)FXboxDiscoveredOption;
    Option.Label = Label;
    Option.URLValue = ClassName;
}

static void XboxMenuLoadDiscoveredLists()
{
    if( GXboxDiscoveredListsLoaded )
        return;

    XboxMenuEnsureRegistryCache();
    GXboxDiscoveredListsLoaded = 1;
    GXboxDiscoveredGameTypes.Empty();
    GXboxDiscoveredMutators.Empty();

    UClass* TournamentGameInfoClass = FindObject<UClass>( ANY_PACKAGE, TEXT("TournamentGameInfo") );
    UClass* MutatorClass = FindObject<UClass>( ANY_PACKAGE, TEXT("Mutator") );

    if( TournamentGameInfoClass )
    {
        TArray<FRegistryObjectInfo> GameInfos;
        UObject::GetRegistryObjects( GameInfos, UClass::StaticClass(), TournamentGameInfoClass, 0 );
        if( GameInfos.Num() <= 1 )
            XboxMenuCollectIntObjects( GameInfos, TEXT("Class"), TEXT("TournamentGameInfo") );
        for( INT i=0; i<GameInfos.Num(); i++ )
        {
            UClass* GameClass = UObject::StaticLoadClass( AGameInfo::StaticClass(), NULL, *GameInfos(i).Object, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
            if( !GameClass )
                continue;

            AGameInfo* Defaults = (AGameInfo*)GameClass->GetDefaultObject();
            FXboxDiscoveredOption& Option = *new(GXboxDiscoveredGameTypes)FXboxDiscoveredOption;
            Option.URLValue = GameInfos(i).Object;
            Option.Label = (Defaults && Defaults->GameName.Len()) ? Defaults->GameName.Caps() : GameInfos(i).Object.Caps();
            Option.MapPrefix = (Defaults && Defaults->MapPrefix.Len()) ? Defaults->MapPrefix : FString(TEXT("DM"));
        }
    }

    if( MutatorClass )
    {
        TArray<FRegistryObjectInfo> Mutators;
        UObject::GetRegistryObjects( Mutators, UClass::StaticClass(), MutatorClass, 0 );
        if( Mutators.Num() <= 1 )
            XboxMenuCollectIntObjects( Mutators, TEXT("Class"), TEXT("Mutator") );
        for( INT i=0; i<Mutators.Num(); i++ )
        {
            FXboxDiscoveredOption& Option = *new(GXboxDiscoveredMutators)FXboxDiscoveredOption;
            Option.URLValue = Mutators(i).Object;
            XboxMenuStripDescriptionLabel( Mutators(i).Description, Option.Label );
        }
    }

    if( GXboxDiscoveredGameTypes.Num() == 0 )
    {
        XboxMenuAddFallbackGameType( TEXT("DEATHMATCH"), TEXT("Botpack.DeathMatchPlus"), TEXT("DM") );
        XboxMenuAddFallbackGameType( TEXT("CAPTURE THE FLAG"), TEXT("Botpack.CTFGame"), TEXT("CTF") );
        XboxMenuAddFallbackGameType( TEXT("DOMINATION"), TEXT("Botpack.Domination"), TEXT("DOM") );
        XboxMenuAddFallbackGameType( TEXT("ASSAULT"), TEXT("Botpack.Assault"), TEXT("AS") );
    }

    if( GXboxDiscoveredMutators.Num() == 0 )
    {
        XboxMenuAddFallbackMutator( TEXT("LOW GRAVITY"), TEXT("Botpack.LowGrav") );
        XboxMenuAddFallbackMutator( TEXT("INSTAGIB"), TEXT("Botpack.InstaGibDM") );
        XboxMenuAddFallbackMutator( TEXT("NO POWERUPS"), TEXT("Botpack.NoPowerups") );
    }

    GXboxLog.Write( "XMENU discovered %d game types, %d mutators from .int registry",
        GXboxDiscoveredGameTypes.Num(), GXboxDiscoveredMutators.Num() );
}

static INT XboxMenuGameTypeCount()
{
    XboxMenuLoadDiscoveredLists();
    return Max<INT>( 1, GXboxDiscoveredGameTypes.Num() );
}

static INT XboxMenuMutatorCount()
{
    XboxMenuLoadDiscoveredLists();
    return Max<INT>( 1, GXboxDiscoveredMutators.Num() );
}

static const FXboxDiscoveredOption& XboxMenuGameType( INT Index )
{
    XboxMenuLoadDiscoveredLists();
    Index = Clamp<INT>( Index, 0, GXboxDiscoveredGameTypes.Num()-1 );
    return GXboxDiscoveredGameTypes(Index);
}

static const FXboxDiscoveredOption& XboxMenuMutator( INT Index )
{
    XboxMenuLoadDiscoveredLists();
    Index = Clamp<INT>( Index, 0, GXboxDiscoveredMutators.Num()-1 );
    return GXboxDiscoveredMutators(Index);
}

static void XboxMenuDisplayMapName( const FString& MapFile, FString& OutLabel )
{
    OutLabel = MapFile;
    if( OutLabel.Right(4) == TEXT(".unr") || OutLabel.Right(4) == TEXT(".UNR") )
        OutLabel = OutLabel.Left( OutLabel.Len() - 4 );
    OutLabel = OutLabel.Caps();
}

static void XboxMenuLoadMapsForGameType( INT GameType )
{
    XboxMenuLoadDiscoveredLists();
    if( GXboxDiscoveredMapsGameType == GameType )
        return;

    GXboxDiscoveredMapsGameType = GameType;
    GXboxDiscoveredMaps.Empty();

    const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
    TCHAR Wildcard[256];
    appSprintf( Wildcard, TEXT("*.%s"), *FURL::DefaultMapExt );

    for( INT DoCD=0; DoCD<1+(GCdPath[0]!=0); DoCD++ )
    {
        for( INT i=0; GSys && i<GSys->Paths.Num(); i++ )
        {
            if( appStrstr( *GSys->Paths(i), Wildcard ) )
            {
                TCHAR Tmp[256]=TEXT("");
                if( DoCD )
                {
                    appStrcat( Tmp, GCdPath );
                    appStrcat( Tmp, TEXT("System") PATH_SEPARATOR );
                }
                appStrcat( Tmp, *GSys->Paths(i) );
                TCHAR* Wild = appStrstr( Tmp, Wildcard );
                if( Wild )
                    *Wild = 0;
                appStrcat( Tmp, *Game.MapPrefix );
                appStrcat( Tmp, Wildcard );

                TArray<FString> TheseNames = GFileManager->FindFiles( Tmp, 1, 0 );
                for( INT n=0; n<TheseNames.Num(); n++ )
                {
                    if( TheseNames(n).InStr( TEXT("-tutorial") ) >= 0 || TheseNames(n).InStr( TEXT("-Tutorial") ) >= 0 )
                        continue;

                    INT Existing = 0;
                    for( ; Existing<GXboxDiscoveredMaps.Num(); Existing++ )
                        if( appStricmp( *GXboxDiscoveredMaps(Existing).URLValue, *TheseNames(n) ) == 0 )
                            break;
                    if( Existing != GXboxDiscoveredMaps.Num() )
                        continue;

                    FXboxDiscoveredOption& Option = *new(GXboxDiscoveredMaps)FXboxDiscoveredOption;
                    Option.URLValue = TheseNames(n);
                    XboxMenuDisplayMapName( TheseNames(n), Option.Label );
                }
            }
        }
    }

    GXboxLog.Write( "XMENU discovered %d maps for game=%s prefix=%s",
        GXboxDiscoveredMaps.Num(), TCHAR_TO_ANSI(*Game.URLValue), TCHAR_TO_ANSI(*Game.MapPrefix) );
}

static INT XboxInstantMapList( INT GameType )
{
    XboxMenuLoadMapsForGameType( GameType );
    return GXboxDiscoveredMaps.Num();
}

static const FXboxDiscoveredOption& XboxMenuMap( INT GameType, INT Index )
{
    XboxMenuLoadMapsForGameType( GameType );
    Index = Clamp<INT>( Index, 0, GXboxDiscoveredMaps.Num()-1 );
    return GXboxDiscoveredMaps(Index);
}

static INT XboxMenuWrap( INT Value, INT Delta, INT Count )
{
    if( Count <= 0 )
        return 0;
    return (Value + Delta + Count) % Count;
}

static void XboxMenuStripMapExtension( const TCHAR* MapFile, TCHAR* OutMapName, INT OutCount )
{
    appStrncpy( OutMapName, MapFile, OutCount );
    OutMapName[OutCount-1] = 0;
    TCHAR* Dot = appStrstr( OutMapName, TEXT(".unr") );
    if( !Dot )
        Dot = appStrstr( OutMapName, TEXT(".UNR") );
    if( Dot )
        *Dot = 0;
}

static UTexture* XboxMenuGetMapPreview( const TCHAR* MapFile )
{
    TCHAR MapName[64];
    XboxMenuStripMapExtension( MapFile, MapName, ARRAY_COUNT(MapName) );
    if( GXboxMenuPreviewTexture && appStricmp(GXboxMenuPreviewMap, MapName)==0 )
        return GXboxMenuPreviewTexture;

    appStrncpy( GXboxMenuPreviewMap, MapName, ARRAY_COUNT(GXboxMenuPreviewMap) );
    GXboxMenuPreviewMap[ARRAY_COUNT(GXboxMenuPreviewMap)-1] = 0;
    if( GXboxMenuPreviewTexture )
        GXboxMenuPreviewTexture->RemoveFromRoot();
    GXboxMenuPreviewTexture = NULL;

    TCHAR ObjectName[96];
    appSprintf( ObjectName, TEXT("%s.Screenshot"), MapName );
    GXboxMenuPreviewTexture = Cast<UTexture>( UObject::StaticLoadObject( UTexture::StaticClass(), NULL, ObjectName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) );
    if( GXboxMenuPreviewTexture )
        GXboxMenuPreviewTexture->AddToRoot();
    GXboxLog.Write( "XMENU map preview %s -> %s", TCHAR_TO_ANSI(ObjectName), GXboxMenuPreviewTexture ? "OK" : "missing" );
    return GXboxMenuPreviewTexture;
}

static void XboxMenuBuildMutatorURL( TCHAR* Out, INT OutCount )
{
    XboxMenuLoadDiscoveredLists();
    Out[0] = 0;
    for( INT i=0; i<GXboxDiscoveredMutators.Num(); i++ )
    {
        if( GXboxMenu.InstantMutatorMask[i >> 5] & (1 << (i & 31)) )
        {
            if( Out[0] )
                appStrncat( Out, TEXT(","), OutCount );
            appStrncat( Out, *GXboxDiscoveredMutators(i).URLValue, OutCount );
        }
    }
}

static void XboxMenuBuildMutatorLabel( TCHAR* Out, INT OutCount )
{
    XboxMenuLoadDiscoveredLists();
    Out[0] = 0;
    INT Selected = 0;
    for( INT i=0; i<GXboxDiscoveredMutators.Num(); i++ )
    {
        if( GXboxMenu.InstantMutatorMask[i >> 5] & (1 << (i & 31)) )
            Selected++;
    }

    if( Selected == 0 )
        appStrncpy( Out, TEXT("NONE"), OutCount );
    else if( Selected == 1 )
    {
        for( INT i=0; i<GXboxDiscoveredMutators.Num(); i++ )
            if( GXboxMenu.InstantMutatorMask[i >> 5] & (1 << (i & 31)) )
                appStrncpy( Out, *GXboxDiscoveredMutators(i).Label, OutCount );
    }
    else
        appSprintf( Out, TEXT("%i SELECTED"), Selected );

    Out[OutCount-1] = 0;
}

static void XboxMenuToggleCurrentMutator()
{
    XboxMenuLoadDiscoveredLists();
    if( GXboxDiscoveredMutators.Num() <= 0 )
        return;
    INT Index = Clamp<INT>( GXboxMenu.InstantMutatorChoice, 0, GXboxDiscoveredMutators.Num()-1 );
    DWORD Bit = 1 << (Index & 31);
    INT Word = Index >> 5;
    if( Word >= 0 && Word < ARRAY_COUNT(GXboxMenu.InstantMutatorMask) )
        GXboxMenu.InstantMutatorMask[Word] ^= Bit;
    GXboxLog.Write( "XMENU mutator toggle choice=%d word=%d mask=0x%08X", Index, Word, GXboxMenu.InstantMutatorMask[Word] );
}

static UXboxClient* XboxMenuGetClient( UXboxViewport* Viewport )
{
    return Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
}

static INT XboxMenuWrapInt( INT Value, INT Delta, INT Count )
{
    if( Count <= 0 )
        return 0;
    return (Value + Delta + Count) % Count;
}

static INT XboxMenuButtonLayoutIndex( INT Layout )
{
    for( INT i=0; i<ARRAY_COUNT(GXboxButtonLayoutValues); i++ )
    {
        if( GXboxButtonLayoutValues[i] == Layout )
            return i;
    }
    return 0;
}

static INT XboxMenuButtonLayoutValue( INT Index )
{
    return GXboxButtonLayoutValues[XboxMenuWrapInt(Index, 0, ARRAY_COUNT(GXboxButtonLayoutValues))];
}

static void XboxMenuCleanExportedText( FString& Value )
{
    const TCHAR* Text = *Value;
    if( Value.Len() >= 2 && Text[0] == '"' && Text[Value.Len()-1] == '"' )
        Value = Value.Mid( 1, Value.Len() - 2 );
    if( appStricmp( *Value, TEXT("None") ) == 0 )
        Value = TEXT("");
}

static UBOOL XboxMenuClassDefaultString( UClass* Class, const TCHAR* PropertyName, FString& OutValue )
{
    OutValue = TEXT("");
    if( !Class || !Class->Defaults.Num() )
        return 0;

    UProperty* Property = FindField<UProperty>( Class, PropertyName );
    if( !Property )
        return 0;

    TCHAR Temp[256] = TEXT("");
    Property->ExportText( 0, Temp, &Class->Defaults(0), &Class->Defaults(0), 0 );
    OutValue = Temp;
    XboxMenuCleanExportedText( OutValue );
    return OutValue.Len() > 0;
}

static INT XboxMenuClassDefaultInt( UClass* Class, const TCHAR* PropertyName, INT DefaultValue )
{
    FString Value;
    if( XboxMenuClassDefaultString( Class, PropertyName, Value ) )
        return appAtoi( *Value );
    return DefaultValue;
}

static UBOOL XboxSetObjectPropertyText( UObject* Object, const TCHAR* PropertyName, const TCHAR* Value )
{
    if( !Object || !PropertyName || !Value )
        return 0;

    UProperty* Property = FindField<UProperty>( Object->GetClass(), PropertyName );
    if( !Property )
        return 0;

    Property->ImportText( Value, (BYTE*)Object + Property->Offset, 0 );
    return 1;
}

static UBOOL XboxSetClassDefaultPropertyText( const TCHAR* ClassName, const TCHAR* PropertyName, const TCHAR* Value )
{
    UClass* Class = UObject::StaticLoadClass( UObject::StaticClass(), NULL, ClassName, NULL, LOAD_NoWarn, NULL );
    if( !Class || !Class->Defaults.Num() )
        return 0;

    UProperty* Property = FindField<UProperty>( Class, PropertyName );
    if( !Property )
        return 0;

    Property->ImportText( Value, &Class->Defaults(0) + Property->Offset, 0 );
    return 1;
}

static void XboxMenuItemName( const FString& FullName, FString& OutItem )
{
    OutItem = FullName;
    const TCHAR* Dot = NULL;
    for( const TCHAR* Scan=*FullName; *Scan; Scan++ )
        if( *Scan == '.' )
            Dot = Scan;
    if( Dot )
        OutItem = Dot + 1;
}

static void XboxMenuPackagePrefix( const FString& FullName, FString& OutPrefix )
{
    OutPrefix = FullName;
    const TCHAR* Dot = NULL;
    for( const TCHAR* Scan=*FullName; *Scan; Scan++ )
        if( *Scan == '.' )
            Dot = Scan;
    if( Dot )
        OutPrefix = FullName.Left( (INT)(Dot - *FullName) + 1 );
}

static INT XboxMenuFindURLValue( const TArray<FXboxDiscoveredOption>& List, const FString& Value )
{
    for( INT i=0; i<List.Num(); i++ )
        if( appStricmp( *List(i).URLValue, *Value ) == 0 )
            return i;
    return 0;
}

static INT XboxMenuFindPlayerClass( const FString& Value )
{
    for( INT i=0; i<GXboxPlayerClasses.Num(); i++ )
        if( appStricmp( *GXboxPlayerClasses(i).URLValue, *Value ) == 0 )
            return i;
    return 0;
}

static void XboxMenuAddFallbackPlayerClass()
{
    FXboxPlayerClassOption& Option = *new(GXboxPlayerClasses)FXboxPlayerClassOption;
    Option.Label = TEXT("MALE SOLDIER");
    Option.URLValue = TEXT("Botpack.TMale2");
    Option.MeshName = TEXT("Soldier");
    Option.MeshPath = TEXT("Botpack.Soldier");
    Option.SelectionMesh = TEXT("Botpack.SelectionMale2");
    Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
    Option.DefaultVoice = TEXT("BotPack.VoiceMaleTwo");
    Option.bMultiSkinned = 1;
}

static void XboxMenuLoadPlayerClasses()
{
    if( GXboxPlayerListsLoaded )
        return;

    XboxMenuEnsureRegistryCache();
    GXboxPlayerListsLoaded = 1;
    GXboxPlayerClasses.Empty();

    UClass* TournamentPlayerClass = FindObject<UClass>( ANY_PACKAGE, TEXT("TournamentPlayer") );
    if( !TournamentPlayerClass )
        TournamentPlayerClass = UObject::StaticLoadClass( APawn::StaticClass(), NULL, TEXT("Botpack.TournamentPlayer"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    if( TournamentPlayerClass )
    {
        TArray<FRegistryObjectInfo> Players;
        UObject::GetRegistryObjects( Players, UClass::StaticClass(), TournamentPlayerClass, 0 );
        if( Players.Num() <= 1 )
            XboxMenuCollectIntObjects( Players, TEXT("Class"), TEXT("TournamentPlayer") );
        for( INT i=0; i<Players.Num(); i++ )
        {
            UClass* PlayerClass = UObject::StaticLoadClass( TournamentPlayerClass, NULL, *Players(i).Object, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
            if( !PlayerClass )
                continue;

            APawn* Defaults = (APawn*)PlayerClass->GetDefaultObject();
            FXboxPlayerClassOption& Option = *new(GXboxPlayerClasses)FXboxPlayerClassOption;
            Option.URLValue = Players(i).Object;
            XboxMenuStripDescriptionLabel( Players(i).Description, Option.Label );
            if( Option.Label == TEXT("UNKNOWN") )
                Option.Label = Players(i).Object.Caps();
            Option.MeshName = (Defaults && Defaults->Mesh) ? FString(Defaults->Mesh->GetName()) : FString(TEXT(""));
            Option.MeshPath = TEXT("");
            if( Defaults && Defaults->Mesh )
            {
                TCHAR MeshPath[256] = TEXT("");
                Defaults->Mesh->GetPathName( NULL, MeshPath );
                Option.MeshPath = MeshPath;
            }
            Option.SelectionMesh = Defaults ? Defaults->SelectionMesh : FString(TEXT(""));
            XboxMenuClassDefaultString( PlayerClass, TEXT("VoicePackMetaClass"), Option.VoiceMetaClass );
            if( Option.VoiceMetaClass.Len() == 0 )
                Option.VoiceMetaClass = TEXT("BotPack.ChallengeVoicePack");
            Option.DefaultVoice = Defaults ? Defaults->VoiceType : FString(TEXT(""));
            Option.bMultiSkinned = Defaults ? Defaults->bIsMultiSkinned : 1;
        }
    }

    if( GXboxPlayerClasses.Num() == 0 )
        XboxMenuAddFallbackPlayerClass();

    GXboxLog.Write( "XMENU discovered %d player classes from .int registry", GXboxPlayerClasses.Num() );
}

static const FXboxPlayerClassOption& XboxMenuPlayerClass( INT Index )
{
    XboxMenuLoadPlayerClasses();
    Index = Clamp<INT>( Index, 0, GXboxPlayerClasses.Num()-1 );
    return GXboxPlayerClasses(Index);
}

static void XboxMenuLoadPlayerSkins( INT ClassIndex )
{
    XboxMenuLoadPlayerClasses();
    ClassIndex = Clamp<INT>( ClassIndex, 0, GXboxPlayerClasses.Num()-1 );
    if( GXboxPlayerSkinsClass == ClassIndex )
        return;

    GXboxPlayerSkinsClass = ClassIndex;
    GXboxPlayerFacesClass = -1;
    GXboxPlayerSkins.Empty();

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( ClassIndex );
    if( Player.MeshName.Len() > 0 )
    {
        TArray<FRegistryObjectInfo> Textures;
        UObject::GetRegistryObjects( Textures, UTexture::StaticClass(), NULL, 0 );
        if( Textures.Num() <= 1 )
            XboxMenuCollectIntObjects( Textures, TEXT("Texture"), NULL );
        INT PrefixLen = Player.MeshName.Len();

        for( INT i=0; i<Textures.Num(); i++ )
        {
            if( Textures(i).Description.Len() == 0 )
                continue;

            FString Item;
            FString Prefix;
            if( appStrnicmp( *Textures(i).Object, *Player.MeshName, PrefixLen ) != 0 )
                continue;
            XboxMenuItemName( Textures(i).Object, Item );
            XboxMenuPackagePrefix( Textures(i).Object, Prefix );

            if( Player.bMultiSkinned )
            {
                if( Item.Len() > 5 )
                    continue;

                FString SkinValue = Prefix + Item.Left(4);
                UBOOL bExists = 0;
                for( INT Existing=0; Existing<GXboxPlayerSkins.Num(); Existing++ )
                    if( appStricmp( *GXboxPlayerSkins(Existing).URLValue, *SkinValue ) == 0 )
                        bExists = 1;
                if( bExists )
                    continue;

                FXboxDiscoveredOption& Option = *new(GXboxPlayerSkins)FXboxDiscoveredOption;
                XboxMenuStripDescriptionLabel( Textures(i).Description, Option.Label );
                Option.URLValue = SkinValue;
            }
            else if( appStrnicmp( *Item, TEXT("T_"), 2 ) != 0 )
            {
                FXboxDiscoveredOption& Option = *new(GXboxPlayerSkins)FXboxDiscoveredOption;
                Option.Label = Item.Caps();
                Option.URLValue = Textures(i).Object;
            }
        }
    }

    if( GXboxPlayerSkins.Num() == 0 )
    {
        FXboxDiscoveredOption& Option = *new(GXboxPlayerSkins)FXboxDiscoveredOption;
        Option.Label = TEXT("DEFAULT");
        XboxMenuClassDefaultString( FindObject<UClass>( ANY_PACKAGE, *Player.URLValue ), TEXT("DefaultSkinName"), Option.URLValue );
        if( Option.URLValue.Len() == 0 )
            Option.URLValue = TEXT("SoldierSkins.blkt");
    }

    GXboxLog.Write( "XMENU discovered %d skins for player=%s mesh=%s",
        GXboxPlayerSkins.Num(), TCHAR_TO_ANSI(*Player.URLValue), TCHAR_TO_ANSI(*Player.MeshName) );
}

static void XboxMenuLoadPlayerFaces( INT ClassIndex, INT SkinIndex )
{
    XboxMenuLoadPlayerSkins( ClassIndex );
    ClassIndex = Clamp<INT>( ClassIndex, 0, GXboxPlayerClasses.Num()-1 );
    SkinIndex = Clamp<INT>( SkinIndex, 0, GXboxPlayerSkins.Num()-1 );
    if( GXboxPlayerFacesClass == ClassIndex && GXboxPlayerFacesSkin == SkinIndex )
        return;

    GXboxPlayerFacesClass = ClassIndex;
    GXboxPlayerFacesSkin = SkinIndex;
    GXboxPlayerFaces.Empty();

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( ClassIndex );
    if( Player.bMultiSkinned && Player.MeshName.Len() > 0 )
    {
        FString SkinItem;
        XboxMenuItemName( GXboxPlayerSkins(SkinIndex).URLValue, SkinItem );
        SkinItem = SkinItem.Left(4);

        TArray<FRegistryObjectInfo> Textures;
        UObject::GetRegistryObjects( Textures, UTexture::StaticClass(), NULL, 0 );
        if( Textures.Num() <= 1 )
            XboxMenuCollectIntObjects( Textures, TEXT("Texture"), NULL );
        INT PrefixLen = Player.MeshName.Len();

        for( INT i=0; i<Textures.Num(); i++ )
        {
            if( Textures(i).Description.Len() == 0 )
                continue;

            FString Item;
            FString Prefix;
            if( appStrnicmp( *Textures(i).Object, *Player.MeshName, PrefixLen ) != 0 )
                continue;
            XboxMenuItemName( Textures(i).Object, Item );
            XboxMenuPackagePrefix( Textures(i).Object, Prefix );
            if( Item.Len() <= 5 || appStrnicmp( *Item, *SkinItem, 4 ) != 0 )
                continue;

            FXboxDiscoveredOption& Option = *new(GXboxPlayerFaces)FXboxDiscoveredOption;
            XboxMenuStripDescriptionLabel( Textures(i).Description, Option.Label );
            Option.URLValue = Prefix + Item.Mid(5);
        }
    }

    if( GXboxPlayerFaces.Num() == 0 )
    {
        FXboxDiscoveredOption& Option = *new(GXboxPlayerFaces)FXboxDiscoveredOption;
        Option.Label = TEXT("OTHELLO");
        Option.URLValue = TEXT("SoldierSkins.Othello");
    }

    GXboxLog.Write( "XMENU discovered %d faces for player=%s skin=%s",
        GXboxPlayerFaces.Num(), TCHAR_TO_ANSI(*Player.URLValue), TCHAR_TO_ANSI(*GXboxPlayerSkins(SkinIndex).URLValue) );
}

static void XboxMenuLoadPlayerVoices( INT ClassIndex )
{
    XboxMenuLoadPlayerClasses();
    ClassIndex = Clamp<INT>( ClassIndex, 0, GXboxPlayerClasses.Num()-1 );
    if( GXboxPlayerVoicesClass == ClassIndex )
        return;

    GXboxPlayerVoicesClass = ClassIndex;
    GXboxPlayerVoices.Empty();

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( ClassIndex );
    UClass* VoiceMetaClass = FindObject<UClass>( ANY_PACKAGE, *Player.VoiceMetaClass );
    if( !VoiceMetaClass )
        VoiceMetaClass = UObject::StaticLoadClass( UObject::StaticClass(), NULL, *Player.VoiceMetaClass, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );

    if( VoiceMetaClass )
    {
        TArray<FRegistryObjectInfo> Voices;
        UObject::GetRegistryObjects( Voices, UClass::StaticClass(), VoiceMetaClass, 0 );
        if( Voices.Num() <= 1 )
            XboxMenuCollectIntObjects( Voices, TEXT("Class"), *Player.VoiceMetaClass );
        for( INT i=0; i<Voices.Num(); i++ )
        {
            FXboxDiscoveredOption& Option = *new(GXboxPlayerVoices)FXboxDiscoveredOption;
            Option.URLValue = Voices(i).Object;
            XboxMenuStripDescriptionLabel( Voices(i).Description, Option.Label );
        }
    }

    if( GXboxPlayerVoices.Num() == 0 )
    {
        FXboxDiscoveredOption& Option = *new(GXboxPlayerVoices)FXboxDiscoveredOption;
        Option.Label = TEXT("DEFAULT");
        Option.URLValue = Player.DefaultVoice.Len() ? Player.DefaultVoice : FString(TEXT("BotPack.VoiceMaleOne"));
    }

    GXboxLog.Write( "XMENU discovered %d voices for player=%s meta=%s",
        GXboxPlayerVoices.Num(), TCHAR_TO_ANSI(*Player.URLValue), TCHAR_TO_ANSI(*Player.VoiceMetaClass) );
}

static const TCHAR* XboxMenuUserString( const TCHAR* Section, const TCHAR* Key, const TCHAR* Fallback )
{
    if( !GConfig )
        return Fallback;
    const TCHAR* Value = GConfig->GetStr( Section, Key, TEXT("User.ini") );
    return (Value && Value[0]) ? Value : Fallback;
}

static void XboxMenuSaveDefaultPlayerString( const TCHAR* Key, const TCHAR* Value )
{
    if( !GConfig )
        return;
    GConfig->SetString( TEXT("DefaultPlayer"), Key, Value, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
}

static void XboxMenuSaveDefaultPlayer()
{
    XboxMenuLoadPlayerClasses();
    GXboxMenu.PlayerClass = Clamp<INT>( GXboxMenu.PlayerClass, 0, GXboxPlayerClasses.Num()-1 );
    XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
    GXboxMenu.PlayerSkin = Clamp<INT>( GXboxMenu.PlayerSkin, 0, GXboxPlayerSkins.Num()-1 );
    XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
    GXboxMenu.PlayerFace = Clamp<INT>( GXboxMenu.PlayerFace, 0, GXboxPlayerFaces.Num()-1 );
    XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
    GXboxMenu.PlayerVoice = Clamp<INT>( GXboxMenu.PlayerVoice, 0, GXboxPlayerVoices.Num()-1 );

    TCHAR TeamValue[16];
    appSprintf( TeamValue, TEXT("%i"), Clamp<INT>(GXboxMenu.PlayerTeam, 0, 255) );
    XboxMenuSaveDefaultPlayerString( TEXT("Class"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue );
    XboxMenuSaveDefaultPlayerString( TEXT("Skin"), *GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue );
    XboxMenuSaveDefaultPlayerString( TEXT("Face"), *GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue );
    XboxMenuSaveDefaultPlayerString( TEXT("Voice"), *GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue );
    XboxMenuSaveDefaultPlayerString( TEXT("Team"), TeamValue );
    GXboxLog.Write( "XMENU saved DefaultPlayer class=%s skin=%s face=%s voice=%s team=%s",
        TCHAR_TO_ANSI(*GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue),
        TCHAR_TO_ANSI(*GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue),
        TCHAR_TO_ANSI(*GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue),
        TCHAR_TO_ANSI(*GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue),
        TCHAR_TO_ANSI(TeamValue) );
}

static void XboxMenuLoadPlayerState()
{
    if( GXboxPlayerStateLoaded )
        return;
    GXboxPlayerStateLoaded = 1;

    XboxMenuLoadPlayerClasses();
    FString ClassValue = XboxMenuUserString( TEXT("DefaultPlayer"), TEXT("Class"), TEXT("Botpack.TMale2") );
    GXboxMenu.PlayerClass = XboxMenuFindPlayerClass( ClassValue );

    XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
    FString SkinValue = XboxMenuUserString( TEXT("DefaultPlayer"), TEXT("Skin"), TEXT("SoldierSkins.blkt") );
    GXboxMenu.PlayerSkin = XboxMenuFindURLValue( GXboxPlayerSkins, SkinValue );

    XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
    FString FaceValue = XboxMenuUserString( TEXT("DefaultPlayer"), TEXT("Face"), TEXT("SoldierSkins.Othello") );
    GXboxMenu.PlayerFace = XboxMenuFindURLValue( GXboxPlayerFaces, FaceValue );

    XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
    FString VoiceValue = XboxMenuUserString( TEXT("DefaultPlayer"), TEXT("Voice"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultVoice );
    GXboxMenu.PlayerVoice = XboxMenuFindURLValue( GXboxPlayerVoices, VoiceValue );

    GXboxMenu.PlayerTeam = appAtoi( XboxMenuUserString( TEXT("DefaultPlayer"), TEXT("Team"), TEXT("255") ) );
    GXboxMenu.PlayerTeam = Clamp<INT>( GXboxMenu.PlayerTeam, 0, 255 );
}

static void XboxMenuBuildPlayerURL( TCHAR* Out, INT OutCount )
{
    XboxMenuLoadPlayerState();
    XboxMenuSaveDefaultPlayer();
    appSprintf
    (
        Out,
        TEXT("?Class=%s?Skin=%s?Face=%s?Voice=%s?Team=%i"),
        *GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue,
        *GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue,
        *GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue,
        *GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue,
        Clamp<INT>(GXboxMenu.PlayerTeam, 0, 255)
    );
    Out[OutCount-1] = 0;
}

static UTexture* XboxMenuLoadTexture( const FString& Name )
{
    if( Name.Len() == 0 )
        return NULL;
    return Cast<UTexture>( UObject::StaticLoadObject( UTexture::StaticClass(), NULL, *Name, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) );
}

static UBOOL XboxMenuSetSkinElement( AActor* Actor, INT SkinNo, const FString& SkinName, const FString& DefaultSkinName )
{
    if( !Actor || SkinNo < 0 || SkinNo >= ARRAY_COUNT(Actor->MultiSkins) )
        return 0;

    UTexture* NewSkin = XboxMenuLoadTexture( SkinName );
    if( NewSkin )
    {
        Actor->MultiSkins[SkinNo] = NewSkin;
        return 1;
    }

    if( DefaultSkinName.Len() )
        Actor->MultiSkins[SkinNo] = XboxMenuLoadTexture( DefaultSkinName );
    return 0;
}

static void XboxMenuAppendInt( FString& Value, INT Number )
{
    TCHAR Tmp[16];
    appSprintf( Tmp, TEXT("%i"), Number );
    Value += Tmp;
}

static UBOOL XboxMenuIsBossPlayerClass( UClass* PlayerClass )
{
    if( !PlayerClass )
        return 0;

    for( UClass* Test=PlayerClass; Test; Test=Test->GetSuperClass() )
    {
        const TCHAR* Name = Test->GetName();
        if( appStricmp( Name, TEXT("TBoss") ) == 0 || appStricmp( Name, TEXT("TBossBot") ) == 0 )
            return 1;
    }
    return 0;
}

static void XboxMenuApplyBossPreviewSkin( AActor* Actor, const FString& InSkinName )
{
    FString SkinName = InSkinName;
    FString SkinItem;
    FString SkinPackage;
    XboxMenuItemName( SkinName, SkinItem );
    XboxMenuPackagePrefix( SkinName, SkinPackage );
    if( SkinPackage.Len() == 0 )
    {
        SkinPackage = TEXT("BossSkins.");
        SkinName = SkinPackage + SkinName;
    }

    GXboxLog.Write( "XMENU preview boss skin begin skin=%s team=%d",
        TCHAR_TO_ANSI(*SkinName),
        GXboxMenu.PlayerTeam );

    if( GXboxMenu.PlayerTeam != 255 )
    {
        FString Team0 = SkinName;
        Team0 += TEXT("1T_");
        XboxMenuAppendInt( Team0, GXboxMenu.PlayerTeam );
        if( !XboxMenuSetSkinElement( Actor, 0, Team0, TEXT("") ) )
        {
            FString Base0 = SkinName;
            Base0 += TEXT("1");
            if( !XboxMenuSetSkinElement( Actor, 0, Base0, TEXT("") ) )
            {
                FString FallbackTeam0 = TEXT("BossSkins.boss1T_");
                XboxMenuAppendInt( FallbackTeam0, GXboxMenu.PlayerTeam );
                XboxMenuSetSkinElement( Actor, 0, FallbackTeam0, TEXT("BossSkins.boss1") );
                SkinName = TEXT("BossSkins.boss");
            }
        }

        for( INT i=1; i<4; i++ )
        {
            FString TeamSkin = SkinName;
            XboxMenuAppendInt( TeamSkin, i + 1 );
            TeamSkin += TEXT("T_");
            XboxMenuAppendInt( TeamSkin, GXboxMenu.PlayerTeam );
            FString Fallback = SkinName;
            XboxMenuAppendInt( Fallback, i + 1 );
            XboxMenuSetSkinElement( Actor, i, TeamSkin, Fallback );
        }
    }
    else
    {
        FString Base0 = SkinName;
        Base0 += TEXT("1");
        if( !XboxMenuSetSkinElement( Actor, 0, Base0, TEXT("BossSkins.boss1") ) )
            SkinName = TEXT("BossSkins.boss");

        for( INT i=1; i<4; i++ )
        {
            FString BaseSkin = SkinName;
            XboxMenuAppendInt( BaseSkin, i + 1 );
            XboxMenuSetSkinElement( Actor, i, BaseSkin, TEXT("") );
        }
    }

    GXboxLog.Write( "XMENU preview boss skin end" );
}

static void XboxMenuApplyPreviewSkin( AActor* Actor )
{
    if( !Actor )
        return;

    GXboxLog.Write( "XMENU preview skin begin" );
    XboxMenuLoadPlayerState();
    XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
    XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
    FString SkinName = GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue;
    FString FaceName = GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue;

    Actor->Skin = NULL;
    for( INT i=0; i<ARRAY_COUNT(Actor->MultiSkins); i++ )
        Actor->MultiSkins[i] = NULL;

    UClass* PlayerClass = FindObject<UClass>( ANY_PACKAGE, *Player.URLValue );
    if( !PlayerClass )
        PlayerClass = UObject::StaticLoadClass( APawn::StaticClass(), NULL, *Player.URLValue, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );

    GXboxLog.Write( "XMENU preview skin class=%s loaded=%s multi=%d skin=%s face=%s team=%d",
        TCHAR_TO_ANSI(*Player.URLValue),
        PlayerClass ? "yes" : "no",
        Player.bMultiSkinned,
        TCHAR_TO_ANSI(*SkinName),
        TCHAR_TO_ANSI(*FaceName),
        GXboxMenu.PlayerTeam );

    if( !Player.bMultiSkinned || !PlayerClass )
    {
        Actor->Skin = XboxMenuLoadTexture( SkinName );
        GXboxLog.Write( "XMENU preview skin simple end" );
        return;
    }

    if( XboxMenuIsBossPlayerClass( PlayerClass ) )
    {
        XboxMenuApplyBossPreviewSkin( Actor, SkinName );
        GXboxLog.Write( "XMENU preview skin boss end" );
        return;
    }

    FString SkinItem;
    FString FaceItem;
    FString SkinPackage;
    FString FacePackage;
    XboxMenuItemName( SkinName, SkinItem );
    XboxMenuItemName( FaceName, FaceItem );
    XboxMenuPackagePrefix( SkinName, SkinPackage );
    XboxMenuPackagePrefix( FaceName, FacePackage );

    FString DefaultPackage;
    FString DefaultSkinName;
    XboxMenuClassDefaultString( PlayerClass, TEXT("DefaultPackage"), DefaultPackage );
    XboxMenuClassDefaultString( PlayerClass, TEXT("DefaultSkinName"), DefaultSkinName );
    if( DefaultSkinName.Len() == 0 )
        DefaultSkinName = SkinName;
    if( SkinPackage.Len() == 0 )
    {
        SkinPackage = DefaultPackage;
        SkinName = SkinPackage + SkinName;
    }
    if( FacePackage.Len() == 0 )
    {
        FacePackage = DefaultPackage;
        FaceName = FacePackage + FaceName;
    }

    INT FixedSkin = XboxMenuClassDefaultInt( PlayerClass, TEXT("FixedSkin"), 2 );
    INT FaceSkin = XboxMenuClassDefaultInt( PlayerClass, TEXT("FaceSkin"), 3 );
    INT TeamSkin1 = XboxMenuClassDefaultInt( PlayerClass, TEXT("TeamSkin1"), 0 );
    INT TeamSkin2 = XboxMenuClassDefaultInt( PlayerClass, TEXT("TeamSkin2"), 1 );

    FString FixedName = SkinName;
    FString FixedFallback = DefaultSkinName;
    XboxMenuAppendInt( FixedName, FixedSkin + 1 );
    XboxMenuAppendInt( FixedFallback, FixedSkin + 1 );
    if( !XboxMenuSetSkinElement( Actor, FixedSkin, FixedName, FixedFallback ) )
    {
        SkinName = DefaultSkinName;
        FaceName = TEXT("");
        FaceItem = TEXT("");
        XboxMenuPackagePrefix( FaceName, FacePackage );
    }

    FString FaceTex = FacePackage + SkinItem;
    XboxMenuAppendInt( FaceTex, FaceSkin + 1 );
    FaceTex += FaceItem;
    FString FaceFallback = SkinName;
    XboxMenuAppendInt( FaceFallback, FaceSkin + 1 );
    XboxMenuSetSkinElement( Actor, FaceSkin, FaceTex, FaceFallback );

    if( GXboxMenu.PlayerTeam != 255 )
    {
        FString Team1 = SkinName;
        FString Team1Fallback = SkinName;
        XboxMenuAppendInt( Team1, TeamSkin1 + 1 );
        Team1 += TEXT("T_");
        XboxMenuAppendInt( Team1, GXboxMenu.PlayerTeam );
        XboxMenuAppendInt( Team1Fallback, TeamSkin1 + 1 );
        XboxMenuSetSkinElement( Actor, TeamSkin1, Team1, Team1Fallback );

        FString Team2 = SkinName;
        FString Team2Fallback = SkinName;
        XboxMenuAppendInt( Team2, TeamSkin2 + 1 );
        Team2 += TEXT("T_");
        XboxMenuAppendInt( Team2, GXboxMenu.PlayerTeam );
        XboxMenuAppendInt( Team2Fallback, TeamSkin2 + 1 );
        XboxMenuSetSkinElement( Actor, TeamSkin2, Team2, Team2Fallback );
    }
    else
    {
        FString Team1 = SkinName;
        FString Team2 = SkinName;
        XboxMenuAppendInt( Team1, TeamSkin1 + 1 );
        XboxMenuAppendInt( Team2, TeamSkin2 + 1 );
        XboxMenuSetSkinElement( Actor, TeamSkin1, Team1, TEXT("") );
        XboxMenuSetSkinElement( Actor, TeamSkin2, Team2, TEXT("") );
    }

    GXboxLog.Write( "XMENU preview skin generic end" );
}

static AActor* XboxMenuGetPlayerPreviewActor( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor || !Viewport->Actor->XLevel )
        return NULL;

    if( GXboxPlayerPreviewActor && GXboxPlayerPreviewLevel == Viewport->Actor->XLevel )
        return GXboxPlayerPreviewActor;

    if( GXboxPlayerPreviewActor )
        XboxMenuResetPlayerPreviewCache();

    UClass* MeshActorClass = FindObject<UClass>( ANY_PACKAGE, TEXT("MeshActor") );
    if( !MeshActorClass )
        MeshActorClass = UObject::StaticLoadClass( AActor::StaticClass(), NULL, TEXT("UMenu.MeshActor"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    if( !MeshActorClass )
        MeshActorClass = AInfo::StaticClass();

    GXboxPlayerPreviewActor = Viewport->Actor->XLevel->SpawnActor( MeshActorClass, NAME_None, NULL, NULL, FVector(0,0,0), FRotator(0,0,0), NULL, 1 );
    GXboxPlayerPreviewLevel = Viewport->Actor->XLevel;
    GXboxPlayerPreviewClass = -1;
    GXboxPlayerPreviewSkin = -1;
    GXboxPlayerPreviewFace = -1;
    GXboxPlayerPreviewTeam = -1;

    if( GXboxPlayerPreviewActor )
    {
        GXboxPlayerPreviewActor->DrawType = DT_Mesh;
        GXboxPlayerPreviewActor->bHidden = 1;
        GXboxPlayerPreviewActor->bUnlit = 1;
        GXboxPlayerPreviewActor->bCollideActors = 0;
        GXboxPlayerPreviewActor->bCollideWorld = 0;
        GXboxPlayerPreviewActor->bBlockActors = 0;
        GXboxPlayerPreviewActor->bBlockPlayers = 0;
        GXboxPlayerPreviewActor->DrawScale = 0.10f;
        GXboxPlayerPreviewActor->AmbientGlow = 255;
    }

    GXboxLog.Write( "XMENU player preview actor %s",
        GXboxPlayerPreviewActor ? "created" : "missing" );
    return GXboxPlayerPreviewActor;
}

static void XboxMenuUpdatePlayerPreviewActor( UXboxViewport* Viewport )
{
    AActor* Actor = XboxMenuGetPlayerPreviewActor( Viewport );
    if( !Actor )
        return;

    XboxMenuLoadPlayerState();
    if( GXboxPlayerPreviewClass == GXboxMenu.PlayerClass
    &&  GXboxPlayerPreviewSkin == GXboxMenu.PlayerSkin
    &&  GXboxPlayerPreviewFace == GXboxMenu.PlayerFace
    &&  GXboxPlayerPreviewTeam == GXboxMenu.PlayerTeam )
        return;

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
    FString MeshName = Player.SelectionMesh.Len() ? Player.SelectionMesh : Player.MeshPath;
    GXboxLog.Write( "XMENU player preview mesh load begin %s",
        TCHAR_TO_ANSI(*MeshName) );
    UMesh* Mesh = MeshName.Len()
        ? Cast<UMesh>( UObject::StaticLoadObject( UMesh::StaticClass(), NULL, *MeshName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) )
        : NULL;
    GXboxLog.Write( "XMENU player preview mesh load end %s",
        Mesh ? "OK" : "missing" );
    Actor->Mesh = Mesh;
    Actor->DrawScale = 0.10f;
    Actor->AmbientGlow = 255;
    Actor->bMeshEnviroMap = 0;
    GXboxLog.Write( "XMENU player preview skin apply begin" );
    XboxMenuApplyPreviewSkin( Actor );
    GXboxLog.Write( "XMENU player preview skin apply end" );

    GXboxPlayerPreviewClass = GXboxMenu.PlayerClass;
    GXboxPlayerPreviewSkin = GXboxMenu.PlayerSkin;
    GXboxPlayerPreviewFace = GXboxMenu.PlayerFace;
    GXboxPlayerPreviewTeam = GXboxMenu.PlayerTeam;
    GXboxLog.Write( "XMENU player preview mesh=%s skin=%s face=%s team=%d %s",
        TCHAR_TO_ANSI(*MeshName),
        TCHAR_TO_ANSI(*GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue),
        TCHAR_TO_ANSI(*GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue),
        GXboxMenu.PlayerTeam,
        Mesh ? "OK" : "missing" );
}

static void XboxMenuDrawPlayerPreviewActor( UXboxViewport* Viewport, UCanvas* Canvas, FLOAT X, FLOAT Y, FLOAT W, FLOAT H )
{
    if( !Viewport || !Canvas || !Canvas->Frame || !Canvas->Render || !Viewport->Actor || !Viewport->RenDev )
        return;

    XboxMenuUpdatePlayerPreviewActor( Viewport );
    AActor* Actor = XboxMenuGetPlayerPreviewActor( Viewport );
    if( !Actor || !Actor->Mesh )
        return;

    FLOAT OldFov = Viewport->Actor->FovAngle;
    Viewport->Actor->FovAngle = 30.0f;
    FLOAT FovRadians = Viewport->Actor->FovAngle * PI / 180.0f;
    Actor->Location = FVector( 3.1f / appTan(FovRadians * 0.5f), 0.0f, 0.0f );
    Actor->Rotation = FRotator( 0, GXboxPlayerPreviewYaw, 0 );

    INT OldX = Canvas->Frame->X;
    INT OldY = Canvas->Frame->Y;
    INT OldXB = Canvas->Frame->XB;
    INT OldYB = Canvas->Frame->YB;
    INT OldRendMap = Viewport->Actor->RendMap;
    UBOOL bOldHidden = Actor->bHidden;

    Canvas->Frame->X = (INT)W;
    Canvas->Frame->Y = (INT)H;
    Canvas->Frame->XB = (INT)X;
    Canvas->Frame->YB = (INT)Y;
    Canvas->Frame->ComputeRenderCoords( FVector(0,0,0), FRotator(0,0,0) );
    Canvas->Frame->ComputeRenderSize();

    Actor->bHidden = 0;
    Viewport->RenDev->ClearZ( Canvas->Frame );
    Canvas->Render->DrawActor( Canvas->Frame, Actor );
    Actor->bHidden = bOldHidden;
    Viewport->Actor->RendMap = OldRendMap;

    Canvas->Frame->X = OldX;
    Canvas->Frame->Y = OldY;
    Canvas->Frame->XB = OldXB;
    Canvas->Frame->YB = OldYB;
    Canvas->Frame->ComputeRenderSize();
    Viewport->Actor->FovAngle = OldFov;
}

static void XboxMenuPlayVoiceSample( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor )
        return;

    UXboxClient* Client = XboxMenuGetClient( Viewport );
    if( !Client || !Client->Engine || !Client->Engine->Audio )
        return;

    XboxMenuLoadPlayerState();
    XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
    UClass* VoiceClass = UObject::StaticLoadClass( UObject::StaticClass(), NULL, *GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    if( !VoiceClass || !VoiceClass->Defaults.Num() )
        return;

    INT NumAcks = XboxMenuClassDefaultInt( VoiceClass, TEXT("NumAcks"), 0 );
    UProperty* AckProp = FindField<UProperty>( VoiceClass, TEXT("AckSound") );
    UObjectProperty* AckObjectProp = Cast<UObjectProperty>( AckProp );
    if( !AckObjectProp || NumAcks <= 0 )
        return;

    INT AckIndex = appRand() % Min<INT>( NumAcks, AckProp->ArrayDim );
    BYTE* AckData = &VoiceClass->Defaults(0) + AckProp->Offset + AckIndex * AckProp->ElementSize;
    USound* Sound = *(USound**)AckData;
    if( !Sound )
        return;

    Client->Engine->Audio->PlaySound( Viewport->Actor, SLOT_Interface, Sound, Viewport->Actor->Location, 16.0f, 1600.0f, 1.0f );
    GXboxLog.Write( "XMENU voice sample class=%s ack=%d sound=%s",
        TCHAR_TO_ANSI(*GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue),
        AckIndex,
        Sound ? Sound->GetName() : "None" );
}

static void XboxMenuSaveTripletColor( const TCHAR* Key, INT ColorIndex )
{
    if( !GConfig )
        return;
    ColorIndex = Clamp<INT>( ColorIndex, 0, ARRAY_COUNT(GXboxColorNames)-1 );
    TCHAR Value[64];
    appSprintf( Value, TEXT("(R=%i,G=%i,B=%i)"),
        GXboxColorTriples[ColorIndex][0],
        GXboxColorTriples[ColorIndex][1],
        GXboxColorTriples[ColorIndex][2] );
    GConfig->SetString( TEXT("Botpack.ChallengeHUD"), Key, Value, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
}

static UBOOL XboxMenuGetUserBool( const TCHAR* Section, const TCHAR* Key, UBOOL DefaultValue )
{
    UBOOL Value = DefaultValue;
    if( GConfig )
        GConfig->GetBool( Section, Key, Value, TEXT("User.ini") );
    return Value;
}

static void XboxMenuSetUserBool( const TCHAR* Section, const TCHAR* Key, UBOOL Value )
{
    if( !GConfig )
        return;
    GConfig->SetBool( Section, Key, Value, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
}

static void XboxMenuSetUserInt( const TCHAR* Section, const TCHAR* Key, INT Value )
{
    if( !GConfig )
        return;
    GConfig->SetInt( Section, Key, Value, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
}

static void XboxMenuLoadSettings()
{
    if( GXboxSettingsLoaded )
        return;
    GXboxSettingsLoaded = 1;

    if( GConfig )
    {
        GConfig->GetInt( TEXT("XboxAudio.XboxAudioDevice"), TEXT("MusicVolume"), GXboxSettingsMusicVolume );
        GConfig->GetInt( TEXT("XboxAudio.XboxAudioDevice"), TEXT("SoundVolume"), GXboxSettingsSoundVolume );
        GConfig->GetInt( TEXT("Botpack.TournamentPlayer"), TEXT("AnnouncerVolume"), GXboxSettingsAnnouncerVolume, TEXT("User.ini") );
        GConfig->GetInt( TEXT("Botpack.ChallengeHUD"), TEXT("Opacity"), GXboxSettingsHudOpacity, TEXT("User.ini") );
    }

    GXboxSettingsMusicVolume = Clamp<INT>( GXboxSettingsMusicVolume, 0, 255 );
    GXboxSettingsSoundVolume = Clamp<INT>( GXboxSettingsSoundVolume, 0, 255 );
    GXboxSettingsAnnouncerVolume = Clamp<INT>( GXboxSettingsAnnouncerVolume, 0, 4 );
    GXboxSettingsHudOpacity = Clamp<INT>( GXboxSettingsHudOpacity, 1, 16 );
}

static INT XboxMenuWeaponHandIndex( APlayerPawn* Player )
{
    FLOAT Hand = Player ? Player->Handedness : -1.0f;
    if( Hand == 0.0f )
        return 1;
    if( Hand == 1.0f )
        return 2;
    if( Hand == 2.0f )
        return 3;
    return 0;
}

static void XboxMenuSetWeaponHand( APlayerPawn* Player, INT Index )
{
    if( !Player )
        return;
    Index = Clamp<INT>( Index, 0, ARRAY_COUNT(GXboxWeaponHands)-1 );
    Player->Handedness = GXboxWeaponHandValues[Index];
    Player->SaveConfig();
}

static void XboxMenuApplyHudColor( UXboxViewport* Viewport, INT Index )
{
    GXboxSettingsHudColor = XboxMenuWrapInt( Index, 0, ARRAY_COUNT(GXboxColorNames) );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    if( Player && Player->myHUD )
        Player->myHUD->SaveConfig();
    XboxMenuSaveTripletColor( TEXT("FavoriteHUDColor"), GXboxSettingsHudColor );
}

static void XboxMenuApplyCrosshairColor( UXboxViewport* Viewport, INT Index )
{
    GXboxSettingsCrosshairColor = XboxMenuWrapInt( Index, 0, ARRAY_COUNT(GXboxColorNames) );
    XboxMenuSaveTripletColor( TEXT("CrosshairColor"), GXboxSettingsCrosshairColor );
    if( Viewport && Viewport->Actor && Viewport->Actor->myHUD )
        Viewport->Actor->myHUD->SaveConfig();
}

static UBOOL XboxMenuShouldPauseMatch( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor || !Viewport->Actor->Level )
        return 0;

    ALevelInfo* Info = Viewport->Actor->Level;
    if( Info->NetMode != NM_Standalone || !Info->Game )
        return 0;

    UClass* GameClass = Info->Game->GetClass();
    const TCHAR* GameName = GameClass ? GameClass->GetName() : TEXT("");
    if( appStricmp( GameName, TEXT("UTIntro") ) == 0 )
        return 0;

    return 1;
}

static void XboxMenuSetMusicPaused( UXboxViewport* Viewport, UBOOL bPaused )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client && Client->Engine && Client->Engine->Audio )
        Client->Engine->Audio->Exec( bPaused ? TEXT("XAUDIOPAUSEMUSIC 1") : TEXT("XAUDIOPAUSEMUSIC 0") );
}

static void XboxMenuApplyMatchPause( UXboxViewport* Viewport )
{
    if( GXboxMenu.PausedMatch || !XboxMenuShouldPauseMatch(Viewport) )
        return;

    ALevelInfo* Info = Viewport->Actor->Level;
    if( Info->Pauser != TEXT("") )
        return;

    Info->Pauser = TEXT("Player");
    GXboxMenu.PausedMatch = 1;
    XboxMenuSetMusicPaused( Viewport, 1 );
    GXboxLog.Write( "XMENU match paused game=%s", Info->Game && Info->Game->GetClass() ? TCHAR_TO_ANSI(Info->Game->GetClass()->GetName()) : "None" );
}

static void XboxMenuReleaseMatchPause( UXboxViewport* Viewport )
{
    if( !GXboxMenu.PausedMatch )
        return;

    if( Viewport && Viewport->Actor && Viewport->Actor->Level )
        Viewport->Actor->Level->Pauser = TEXT("");
    GXboxMenu.PausedMatch = 0;
    XboxMenuSetMusicPaused( Viewport, 0 );
    GXboxLog.Write( "XMENU match resumed" );
}

static void XboxMenuOpen( UXboxViewport* Viewport )
{
    if( !GXboxMenu.Active )
        GXboxLog.Write( "XMENU opened" );
    GXboxMenu.Active = 1;
    GXboxMenu.Screen = XboxMenuShouldPauseMatch(Viewport) ? XMS_Pause : XMS_Main;
    GXboxMenu.PauseFocus = 0;
    GXboxMenu.MainFocus = 0;

    XboxMenuApplyMatchPause( Viewport );

    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client && Client->Engine && Client->Engine->Audio )
    {
        Client->Engine->Audio->Exec( TEXT("XAUDIOSETMENUMODE 1") );
        Client->Engine->Audio->Exec( TEXT("XAUDIOSTOPFX") );
    }
}

static void XboxMenuClose( UXboxViewport* Viewport )
{
    if( GXboxMenu.Active )
        GXboxLog.Write( "XMENU closed" );
    GXboxMenu.Active = 0;
    XboxMenuDestroyPlayerPreview();

    XboxMenuReleaseMatchPause( Viewport );

    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client && Client->Engine && Client->Engine->Audio )
        Client->Engine->Audio->Exec( TEXT("XAUDIOSETMENUMODE 0") );
}

static UBOOL XboxMenuIsActive()
{
    return GXboxMenu.Active;
}

extern "C" UBOOL XboxMenuWantsEffectSuppression()
{
    return GXboxMenu.Active;
}

static void XboxMenuBack( UXboxViewport* Viewport )
{
    if( GXboxMenu.Screen == XMS_Pause || GXboxMenu.Screen == XMS_Main )
    {
        XboxMenuClose( Viewport );
    }
    else if( GXboxMenu.Screen == XMS_Mutators )
    {
        GXboxMenu.Screen = XMS_InstantAction;
        GXboxLog.Write( "XMENU close mutator overlay" );
    }
    else if( GXboxMenu.Screen == XMS_SystemLink )
    {
        XboxSystemLinkStop();
        GXboxMenu.Screen = XMS_Main;
        GXboxLog.Write( "XMENU back from System Link probe" );
    }
    else
    {
        GXboxMenu.Screen = XMS_Main;
        GXboxLog.Write( "XMENU back to main" );
    }
}

static void XboxMenuComingSoon( const TCHAR* Title )
{
    GXboxMenu.Screen = XMS_ComingSoon;
    appStrncpy( GXboxMenu.ComingSoonTitle, Title, ARRAY_COUNT(GXboxMenu.ComingSoonTitle) );
    GXboxMenu.ComingSoonTitle[ARRAY_COUNT(GXboxMenu.ComingSoonTitle)-1] = 0;
    GXboxLog.Write( "XMENU coming soon: %s", TCHAR_TO_ANSI(Title) );
}

static void XboxMenuStartInstantAction( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    INT GameTypeCount = XboxMenuGameTypeCount();
    GXboxMenu.InstantGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, GameTypeCount-1 );
    INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
    if( MapCount <= 0 )
    {
        const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
        GXboxLog.Write( "XMENU Begin Match blocked: no maps for game=%s prefix=%s",
            TCHAR_TO_ANSI(*Game.URLValue), TCHAR_TO_ANSI(*Game.MapPrefix) );
        return;
    }
    INT MapIndex = Clamp<INT>( GXboxMenu.InstantMap[GXboxMenu.InstantGameType], 0, MapCount-1 );
    const FXboxDiscoveredOption& Map = XboxMenuMap( GXboxMenu.InstantGameType, MapIndex );
    const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
    INT Bots = GXboxBotCounts[Clamp<INT>(GXboxMenu.InstantBots, 0, ARRAY_COUNT(GXboxBotCounts)-1)];
    INT FragLimit = GXboxFragLimits[Clamp<INT>(GXboxMenu.InstantFragLimit, 0, ARRAY_COUNT(GXboxFragLimits)-1)];
    INT TimeLimit = GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
    INT Skill = Clamp<INT>( GXboxMenu.InstantSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );
    TCHAR MutatorURL[256];
    TCHAR PlayerURL[512];
    TCHAR URL[1024];
    XboxMenuBuildMutatorURL( MutatorURL, ARRAY_COUNT(MutatorURL) );
    XboxMenuBuildPlayerURL( PlayerURL, ARRAY_COUNT(PlayerURL) );

    appSprintf
    (
        URL,
        TEXT("%s?Game=%s?FragLimit=%i?TimeLimit=%i?MinPlayers=%i?Difficulty=%i%s%s%s"),
        *Map.URLValue,
        *Game.URLValue,
        FragLimit,
        TimeLimit,
        Bots + 1,
        Skill,
        PlayerURL,
        MutatorURL[0] ? TEXT("?Mutator=") : TEXT(""),
        MutatorURL[0] ? MutatorURL : TEXT("")
    );

    XboxMenuClose( Viewport );
    XboxSplitResetRuntime( Client, "InstantAction" );
    GXboxLog.Write( "XMENU Begin Match travel: %s", TCHAR_TO_ANSI(URL) );
    Client->Engine->SetClientTravel( Viewport, URL, 0, TRAVEL_Absolute );
}

static void XboxMenuStartSplitScreen( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    XboxSetClassDefaultPropertyText( TEXT("Botpack.DeathMatchPlus"), TEXT("InitialBots"), TEXT("0") );
    XboxSetClassDefaultPropertyText( TEXT("Botpack.DeathMatchPlus"), TEXT("MinPlayers"), TEXT("0") );

    XboxSplitResetRuntime( Client, "StartSplitScreen" );
    GXboxSplitPending = 1;

    TCHAR PlayerURL[512];
    TCHAR URL[1024];
    XboxMenuBuildPlayerURL( PlayerURL, ARRAY_COUNT(PlayerURL) );
    appSprintf
    (
        URL,
        TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=0?MaxPlayers=4?Difficulty=1%s"),
        PlayerURL
    );

    XboxMenuClose( Viewport );
    GXboxLog.Write( "XSPLIT queued travel: %s", TCHAR_TO_ANSI(URL) );
    Client->Engine->SetClientTravel( Viewport, URL, 0, TRAVEL_Absolute );
}

static void XboxMenuReturnToFrontend( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    XboxMenuDestroyPlayerPreview();
    XboxMenuReleaseMatchPause( Viewport );
    XboxSplitResetRuntime( Client, "ReturnToFrontend" );
    Client->Engine->Flush( 0 );
    GXboxMenu.Active = 1;
    GXboxMenu.Screen = XMS_Main;
    GXboxMenu.MainFocus = 0;
    GXboxMenu.PauseFocus = 0;

    if( Client->Engine->Audio )
    {
        Client->Engine->Audio->Exec( TEXT("XAUDIOSETMENUMODE 1") );
        Client->Engine->Audio->Exec( TEXT("XAUDIOSTOPFX") );
    }

    GXboxLog.Write( "XMENU return to frontend: CityIntro.unr" );
    Client->Engine->SetClientTravel( Viewport, TEXT("CityIntro.unr"), 0, TRAVEL_Absolute );
}

static void XboxMenuMove( INT Delta );
static void XboxMenuAdjustInstantAction( INT Delta );
static void XboxMenuAdjustPlayerSetup( UXboxViewport* Viewport, INT Delta );
static void XboxMenuAdjustSettings( UXboxViewport* Viewport, INT Delta );

static void XboxMenuActivate( UXboxViewport* Viewport )
{
    if( GXboxMenu.Screen == XMS_Pause )
    {
        switch( GXboxMenu.PauseFocus )
        {
            case 0:
                XboxMenuClose( Viewport );
                break;
            case 1:
                XboxMenuReturnToFrontend( Viewport );
                break;
            case 2:
                GXboxMenu.Screen = XMS_Settings;
                GXboxMenu.SettingsFocus = 0;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU screen: Settings" );
                break;
        }
    }
    else if( GXboxMenu.Screen == XMS_Main )
    {
        switch( GXboxMenu.MainFocus )
        {
            case 0:
                GXboxMenu.Screen = XMS_InstantAction;
                GXboxMenu.InstantFocus = 0;
                GXboxLog.Write( "XMENU screen: Instant Action" );
                break;
            case 1:
                GXboxMenu.Screen = XMS_SystemLink;
                XboxSystemLinkStart();
                GXboxLog.Write( "XMENU screen: System Link probe" );
                break;
            case 2:
                XboxMenuStartSplitScreen( Viewport );
                break;
            case 3:
                GXboxMenu.Screen = XMS_PlayerSetup;
                GXboxMenu.PlayerFocus = 0;
                XboxMenuLoadPlayerState();
                GXboxLog.Write( "XMENU screen: Player Setup" );
                break;
            case 4:
                GXboxMenu.Screen = XMS_Settings;
                GXboxMenu.SettingsFocus = 0;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU screen: Settings" );
                break;
        }
    }
    else if( GXboxMenu.Screen == XMS_InstantAction )
    {
        if( GXboxMenu.InstantFocus == 7 )
        {
            XboxMenuStartInstantAction( Viewport );
        }
        else if( GXboxMenu.InstantFocus == 6 )
        {
            GXboxMenu.Screen = XMS_Mutators;
            GXboxLog.Write( "XMENU open mutator overlay" );
        }
        else
        {
            XboxMenuMove( 1 );
        }
    }
    else if( GXboxMenu.Screen == XMS_Mutators )
    {
        XboxMenuToggleCurrentMutator();
    }
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
    {
        XboxMenuAdjustPlayerSetup( Viewport, 1 );
    }
    else if( GXboxMenu.Screen == XMS_Settings )
    {
        XboxMenuAdjustSettings( Viewport, 1 );
    }
    else
    {
        XboxMenuBack( Viewport );
    }
}

static void XboxMenuAdjustInstantAction( INT Delta )
{
    if( GXboxMenu.Screen != XMS_InstantAction || Delta == 0 )
        return;

    switch( GXboxMenu.InstantFocus )
    {
        case 0:
            GXboxMenu.InstantGameType = XboxMenuWrap( GXboxMenu.InstantGameType, Delta, XboxMenuGameTypeCount() );
            break;
        case 1:
        {
            INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
            GXboxMenu.InstantMap[GXboxMenu.InstantGameType] = XboxMenuWrap( GXboxMenu.InstantMap[GXboxMenu.InstantGameType], Delta, MapCount );
            break;
        }
        case 2:
            GXboxMenu.InstantBots = XboxMenuWrap( GXboxMenu.InstantBots, Delta, ARRAY_COUNT(GXboxBotCounts) );
            break;
        case 3:
            GXboxMenu.InstantSkill = XboxMenuWrap( GXboxMenu.InstantSkill, Delta, ARRAY_COUNT(GXboxSkillLabels) );
            break;
        case 4:
            GXboxMenu.InstantFragLimit = XboxMenuWrap( GXboxMenu.InstantFragLimit, Delta, ARRAY_COUNT(GXboxFragLimits) );
            break;
        case 5:
            GXboxMenu.InstantTimeLimit = XboxMenuWrap( GXboxMenu.InstantTimeLimit, Delta, ARRAY_COUNT(GXboxTimeLimits) );
            break;
    }

    GXboxLog.Write( "XMENU instant adjust row=%d delta=%d", GXboxMenu.InstantFocus, Delta );
}

static void XboxMenuAdjustPlayerSetup( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_PlayerSetup || Delta == 0 )
        return;

    XboxMenuLoadPlayerState();

    switch( GXboxMenu.PlayerFocus )
    {
        case 0:
            GXboxMenu.PlayerClass = XboxMenuWrapInt( GXboxMenu.PlayerClass, Delta, GXboxPlayerClasses.Num() );
            GXboxPlayerSkinsClass = -1;
            GXboxPlayerVoicesClass = -1;
            GXboxMenu.PlayerSkin = 0;
            GXboxMenu.PlayerFace = 0;
            GXboxMenu.PlayerVoice = 0;
            XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
            XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
            XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
            if( GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultVoice.Len() )
                GXboxMenu.PlayerVoice = XboxMenuFindURLValue( GXboxPlayerVoices, GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultVoice );
            break;
        case 1:
            XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
            GXboxMenu.PlayerSkin = XboxMenuWrapInt( GXboxMenu.PlayerSkin, Delta, GXboxPlayerSkins.Num() );
            GXboxPlayerFacesClass = -1;
            GXboxMenu.PlayerFace = 0;
            XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
            break;
        case 2:
            XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
            GXboxMenu.PlayerFace = XboxMenuWrapInt( GXboxMenu.PlayerFace, Delta, GXboxPlayerFaces.Num() );
            break;
        case 3:
            XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
            GXboxMenu.PlayerVoice = XboxMenuWrapInt( GXboxMenu.PlayerVoice, Delta, GXboxPlayerVoices.Num() );
            XboxMenuPlayVoiceSample( Viewport );
            break;
        case 4:
            if( GXboxMenu.PlayerTeam == 255 )
                GXboxMenu.PlayerTeam = Delta > 0 ? 0 : 3;
            else
            {
                GXboxMenu.PlayerTeam += Delta;
                if( GXboxMenu.PlayerTeam > 3 )
                    GXboxMenu.PlayerTeam = 255;
                else if( GXboxMenu.PlayerTeam < 0 )
                    GXboxMenu.PlayerTeam = 255;
            }
            break;
    }

    XboxMenuSaveDefaultPlayer();
    GXboxLog.Write( "XMENU player adjust row=%d delta=%d", GXboxMenu.PlayerFocus, Delta );
}

static void XboxMenuAdjustSettings( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_Settings || Delta == 0 )
        return;

    XboxMenuLoadSettings();
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;

    switch( GXboxMenu.SettingsFocus )
    {
        case XSR_LookSensitivity:
            if( Client )
            {
                Client->ScaleRUV = Clamp<FLOAT>( Client->ScaleRUV + Delta * 5.0f, 25.0f, 200.0f );
                Client->SaveConfig();
            }
            break;
        case XSR_MoveSensitivity:
            if( Client )
            {
                Client->ScaleXYZ = Clamp<FLOAT>( Client->ScaleXYZ + Delta * 5.0f, 25.0f, 200.0f );
                Client->SaveConfig();
            }
            break;
        case XSR_InvertY:
            if( Client )
            {
                Client->InvertVertical = !Client->InvertVertical;
                Client->SaveConfig();
            }
            break;
        case XSR_DeadZone:
            if( Client )
            {
                Client->DeadZone = Clamp<FLOAT>( Client->DeadZone + Delta * 0.05f, 0.05f, 0.40f );
                Client->SaveConfig();
            }
            break;
        case XSR_ButtonLayout:
            if( Client )
            {
                INT LayoutIndex = XboxMenuButtonLayoutIndex( Client->ButtonLayout );
                Client->ButtonLayout = XboxMenuButtonLayoutValue( LayoutIndex + Delta );
                Client->SaveConfig();
            }
            break;
        case XSR_MusicVolume:
            GXboxSettingsMusicVolume = Clamp<INT>( GXboxSettingsMusicVolume + Delta * 16, 0, 255 );
            if( Client && Client->Engine && Client->Engine->Audio )
            {
                TCHAR Cmd[48];
                appSprintf( Cmd, TEXT("XAUDIOSETMUSICVOLUME %i"), GXboxSettingsMusicVolume );
                Client->Engine->Audio->Exec( Cmd );
            }
            break;
        case XSR_SoundVolume:
            GXboxSettingsSoundVolume = Clamp<INT>( GXboxSettingsSoundVolume + Delta * 16, 0, 255 );
            if( Client && Client->Engine && Client->Engine->Audio )
            {
                TCHAR Cmd[48];
                appSprintf( Cmd, TEXT("XAUDIOSETSOUNDVOLUME %i"), GXboxSettingsSoundVolume );
                Client->Engine->Audio->Exec( Cmd );
            }
            break;
        case XSR_AnnouncerVolume:
            GXboxSettingsAnnouncerVolume = Clamp<INT>( GXboxSettingsAnnouncerVolume + Delta, 0, 4 );
            XboxMenuSetUserInt( TEXT("Botpack.TournamentPlayer"), TEXT("AnnouncerVolume"), GXboxSettingsAnnouncerVolume );
            break;
        case XSR_Crosshair:
            if( Player && Player->myHUD )
            {
                Player->myHUD->Crosshair = XboxMenuWrapInt( Player->myHUD->Crosshair, Delta, 9 );
                Player->myHUD->SaveConfig();
            }
            break;
        case XSR_HudColor:
            XboxMenuApplyHudColor( Viewport, GXboxSettingsHudColor + Delta );
            break;
        case XSR_CrosshairColor:
            XboxMenuApplyCrosshairColor( Viewport, GXboxSettingsCrosshairColor + Delta );
            break;
        case XSR_HudOpacity:
            GXboxSettingsHudOpacity = Clamp<INT>( GXboxSettingsHudOpacity + Delta, 1, 16 );
            XboxMenuSetUserInt( TEXT("Botpack.ChallengeHUD"), TEXT("Opacity"), GXboxSettingsHudOpacity );
            break;
        case XSR_WeaponHand:
            XboxMenuSetWeaponHand( Player, XboxMenuWrapInt( XboxMenuWeaponHandIndex(Player), Delta, ARRAY_COUNT(GXboxWeaponHands) ) );
            break;
        case XSR_AutoSwitch:
            if( Player )
            {
                Player->bNeverAutoSwitch = !Player->bNeverAutoSwitch;
                Player->bNeverSwitchOnPickup = Player->bNeverAutoSwitch;
                Player->SaveConfig();
            }
            break;
        case XSR_MatureLanguage:
        {
            UBOOL bNoMature = XboxMenuGetUserBool( TEXT("Botpack.TournamentPlayer"), TEXT("bNoMatureLanguage"), 0 );
            XboxMenuSetUserBool( TEXT("Botpack.TournamentPlayer"), TEXT("bNoMatureLanguage"), !bNoMature );
            break;
        }
    }

    GXboxLog.Write( "XMENU settings adjust row=%d delta=%d", GXboxMenu.SettingsFocus, Delta );
}

static void XboxMenuMove( INT Delta )
{
    if( GXboxMenu.Screen == XMS_Pause )
    {
        GXboxMenu.PauseFocus = (GXboxMenu.PauseFocus + Delta + 3) % 3;
    }
    else if( GXboxMenu.Screen == XMS_Main )
    {
        GXboxMenu.MainFocus = (GXboxMenu.MainFocus + Delta + 5) % 5;
    }
    else if( GXboxMenu.Screen == XMS_InstantAction )
    {
        GXboxMenu.InstantFocus = (GXboxMenu.InstantFocus + Delta + 8) % 8;
        GXboxLog.Write( "XMENU instant focus=%d", GXboxMenu.InstantFocus );
    }
    else if( GXboxMenu.Screen == XMS_Mutators )
    {
        GXboxMenu.InstantMutatorChoice = XboxMenuWrap( GXboxMenu.InstantMutatorChoice, Delta, XboxMenuMutatorCount() );
        GXboxLog.Write( "XMENU mutator focus=%d", GXboxMenu.InstantMutatorChoice );
    }
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
    {
        GXboxMenu.PlayerFocus = XboxMenuWrapInt( GXboxMenu.PlayerFocus, Delta, 5 );
        GXboxLog.Write( "XMENU player focus=%d", GXboxMenu.PlayerFocus );
    }
    else if( GXboxMenu.Screen == XMS_Settings )
    {
        GXboxMenu.SettingsFocus = XboxMenuWrapInt( GXboxMenu.SettingsFocus, Delta, XSR_Count );
    }
}

static UBOOL XboxButtonPressed( WORD Cur, WORD Prev, WORD Mask )
{
    return (Cur & Mask) && !(Prev & Mask);
}

static UBOOL XboxAnalogPressed( const XINPUT_GAMEPAD& CurPad, const XINPUT_GAMEPAD& PrevPad, INT Index )
{
    const BYTE Threshold = XINPUT_GAMEPAD_MAX_CROSSTALK;
    return CurPad.bAnalogButtons[Index] > Threshold && PrevPad.bAnalogButtons[Index] <= Threshold;
}

static UBOOL XboxThumbPressed( SHORT Cur, SHORT Prev, SHORT Threshold )
{
    if( Threshold > 0 )
        return Cur > Threshold && Prev <= Threshold;
    return Cur < Threshold && Prev >= Threshold;
}

static void XboxSendGameplayButton( UXboxViewport* Viewport, EInputKey Key, UBOOL bDown, UBOOL bWasDown )
{
    if( !Viewport || bDown == bWasDown )
        return;

    UXboxClient* Client = (UXboxClient*)Viewport->GetOuter();
    if( !Client || !Client->Engine )
        return;

    Client->Engine->InputEvent( Viewport, Key, bDown ? IST_Press : IST_Release, 0.0f );
}

static UBOOL XboxMenuHandleInput( UXboxViewport* Viewport, const XINPUT_GAMEPAD& Pad, const XINPUT_GAMEPAD& PrevPad )
{
    WORD CurDigital  = Pad.wButtons;
    WORD PrevDigital = PrevPad.wButtons;

    if( !GXboxMenu.Active )
    {
        if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_START ) )
        {
            XboxMenuOpen( Viewport );
            return 1;
        }
        return 0;
    }

    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_UP )
    ||  XboxThumbPressed( Pad.sThumbLY, PrevPad.sThumbLY, 18000 ) )
        XboxMenuMove( -1 );
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_DOWN )
    ||  XboxThumbPressed( Pad.sThumbLY, PrevPad.sThumbLY, -18000 ) )
        XboxMenuMove( 1 );
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_LEFT )
    ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, -18000 ) )
    {
        XboxMenuAdjustInstantAction( -1 );
        XboxMenuAdjustPlayerSetup( Viewport, -1 );
        XboxMenuAdjustSettings( Viewport, -1 );
    }
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_RIGHT )
    ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, 18000 ) )
    {
        XboxMenuAdjustInstantAction( 1 );
        XboxMenuAdjustPlayerSetup( Viewport, 1 );
        XboxMenuAdjustSettings( Viewport, 1 );
    }
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_START ) )
    {
        if( GXboxMenu.PausedMatch )
            XboxMenuClose( Viewport );
        else
            XboxMenuActivate( Viewport );
    }
    if( XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_A )
    ||  XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_X ) )
        XboxMenuActivate( Viewport );
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_BACK )
    ||  XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_B ) )
        XboxMenuBack( Viewport );

    return 1;
}

static void XboxMenuDrawRect( UCanvas* Canvas, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, FLOAT A=1.0f )
{
    if( Canvas && Canvas->Frame )
        XboxRenderDrawMenuRect( Canvas->Frame, X1, Y1, X2, Y2, R, G, B, (BYTE)Clamp<INT>((INT)(A * 255.0f), 0, 255) );
}

static void XboxMenuDrawImage( UCanvas* Canvas, const char* Name, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha=1.0f )
{
    if( Canvas && Canvas->Frame )
        XboxRenderDrawMenuTexture( Canvas->Frame, Name, X, Y, XL, YL, Alpha );
}

static void XboxMenuText( UCanvas* Canvas, UFont* Font, FLOAT X, FLOAT Y, BYTE R, BYTE G, BYTE B, const TCHAR* Text )
{
    if( !Canvas || !Font )
        return;
    FLOAT OldCurX = Canvas->CurX;
    FLOAT OldCurY = Canvas->CurY;
    if( Canvas->Frame )
        XboxRenderPrepareMenuText( Canvas->Frame, TCHAR_TO_ANSI(Text) );
    Canvas->CurX = X;
    Canvas->CurY = Y;
    Canvas->Color = FColor(R,G,B);
    Canvas->WrappedPrintf( Font, 0, TEXT("%s"), Text );
    if( Canvas->Frame )
        XboxRenderFinishMenuText( Canvas->Frame );
    Canvas->CurX = OldCurX;
    Canvas->CurY = OldCurY;
}

static void XboxMenuCenteredText( UCanvas* Canvas, UFont* Font, FLOAT Y, BYTE R, BYTE G, BYTE B, const TCHAR* Text )
{
    if( !Canvas || !Font )
        return;
    FLOAT OldCurX = Canvas->CurX;
    FLOAT OldCurY = Canvas->CurY;
    if( Canvas->Frame )
        XboxRenderPrepareMenuText( Canvas->Frame, TCHAR_TO_ANSI(Text) );
    Canvas->CurX = 0;
    Canvas->CurY = Y;
    Canvas->Color = FColor(R,G,B);
    Canvas->WrappedPrintf( Font, 1, TEXT("%s"), Text );
    if( Canvas->Frame )
        XboxRenderFinishMenuText( Canvas->Frame );
    Canvas->CurX = OldCurX;
    Canvas->CurY = OldCurY;
}

static void XboxMenuTextSize( UCanvas* Canvas, UFont* Font, const TCHAR* Text, INT& XL, INT& YL )
{
    XL = 0;
    YL = 0;
    if( Canvas && Font && Text )
    {
        FLOAT OldCurX = Canvas->CurX;
        FLOAT OldCurY = Canvas->CurY;
        Canvas->CurX = 0;
        Canvas->CurY = 0;
        Canvas->WrappedStrLenf( Font, XL, YL, TEXT("%s"), Text );
        Canvas->CurX = OldCurX;
        Canvas->CurY = OldCurY;
    }
}

static void XboxMenuDrawButtonPrompt( UCanvas* Canvas, FLOAT X, FLOAT Y, const char* ButtonImage, const TCHAR* Label )
{
    if( !Canvas )
        return;

    UFont* PromptFont = Canvas->MedFont;
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, PromptFont, Label, XL, YL );

    FLOAT IconSize = 18.0f;
    FLOAT IconY = (FLOAT)(INT)(Y - 1.0f);
    FLOAT TextY = (FLOAT)(INT)(Y + (IconSize - (FLOAT)YL) * 0.5f);
    XboxMenuDrawImage( Canvas, ButtonImage, X, IconY, IconSize, IconSize );
    XboxMenuText( Canvas, PromptFont, X + 26.0f, TextY, 200, 220, 238, Label );
}

static void XboxMenuDrawTexture( UCanvas* Canvas, UTexture* Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL )
{
    if( !Canvas || !Texture )
        return;

    Canvas->DrawTile
    (
        Texture,
        X,
        Y,
        XL,
        YL,
        0.0f,
        0.0f,
        Texture->USize,
        Texture->VSize,
        NULL,
        Canvas->Z,
        FPlane(1,1,1,1),
        FPlane(0,0,0,0),
        PF_TwoSided
    );
}

static void XboxMenuDrawTextureTint( UCanvas* Canvas, UTexture* Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, BYTE R, BYTE G, BYTE B, BYTE A )
{
    if( !Canvas || !Texture )
        return;

    DWORD Flags = PF_TwoSided | PF_Masked;
    if( A < 255 )
        Flags |= PF_Translucent;

    Canvas->DrawTile
    (
        Texture,
        X,
        Y,
        XL,
        YL,
        0.0f,
        0.0f,
        Texture->USize,
        Texture->VSize,
        NULL,
        Canvas->Z,
        FPlane(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f),
        FPlane(0,0,0,0),
        Flags
    );
}

static FLOAT XboxWeaponWheelNormalizeAngle( FLOAT Angle )
{
    const FLOAT TwoPi = 6.28318530718f;
    while( Angle < 0.0f )
        Angle += TwoPi;
    while( Angle >= TwoPi )
        Angle -= TwoPi;
    return Angle;
}

static FLOAT XboxWeaponWheelAngleFromTopCCW( FLOAT DX, FLOAT DY )
{
    // Screen Y grows downward; this returns 0 at 12 o'clock, increasing toward 9 o'clock.
    return XboxWeaponWheelNormalizeAngle( appAtan2( -DX, -DY ) );
}

static void XboxWeaponWheelSliceBounds( INT Slot, FLOAT& StartAngle, FLOAT& EndAngle )
{
    const FLOAT Slice = 6.28318530718f / 16.0f;
    const FLOAT GapHalf = Slice * 1.5f;
    Slot = Clamp<INT>( Slot, 0, ARRAY_COUNT(GXboxWeaponWheelSlots)-1 );
    StartAngle = GapHalf + Slot * Slice;
    EndAngle   = StartAngle + Slice;
}

static void XboxWeaponWheelDrawSlice( UCanvas* Canvas, FLOAT CX, FLOAT CY, FLOAT InnerR, FLOAT OuterR, INT Slot, BYTE R, BYTE G, BYTE B, BYTE A )
{
    if( !Canvas || !Canvas->Frame )
        return;

    FLOAT StartAngle, EndAngle;
    XboxWeaponWheelSliceBounds( Slot, StartAngle, EndAngle );
    const FLOAT AngularInset = 0.020f;
    StartAngle += AngularInset;
    EndAngle   -= AngularInset;

    XboxRenderDrawMenuRingSlice( Canvas->Frame, CX, CY, InnerR, OuterR, StartAngle, EndAngle, R, G, B, A );
}

static UTexture* XboxWeaponWheelLoadTexture( const TCHAR* IconName )
{
    if( !IconName )
        return NULL;

    UTexture* Texture = Cast<UTexture>( UObject::StaticLoadObject( UTexture::StaticClass(), NULL, IconName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) );
    if( Texture )
        return Texture;

    TCHAR* IconsGroup = appStrstr( IconName, TEXT(".Icons.") );
    if( IconsGroup )
    {
        TCHAR Fallback[128];
        INT PrefixLen = Min<INT>( IconsGroup - IconName, ARRAY_COUNT(Fallback) - 2 );
        appStrncpy( Fallback, IconName, PrefixLen + 1 );
        Fallback[PrefixLen] = 0;
        appStrcat( Fallback, TEXT(".") );
        appStrcat( Fallback, IconsGroup + 7 );
        Texture = Cast<UTexture>( UObject::StaticLoadObject( UTexture::StaticClass(), NULL, Fallback, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) );
    }

    return Texture;
}

static void XboxWeaponWheelLoadIcons()
{
    for( INT i=0; i<ARRAY_COUNT(GXboxWeaponWheelSlots); i++ )
    {
        if( !GXboxWeaponWheelSlots[i].Icon )
        {
            GXboxWeaponWheelSlots[i].Icon = XboxWeaponWheelLoadTexture( GXboxWeaponWheelSlots[i].IconName );
            if( !GXboxWeaponWheelSlots[i].Icon && GXboxWeaponWheelLogCount < 32 )
            {
                GXboxWeaponWheelLogCount++;
                GXboxLog.Write( "XWHEEL missing icon %s", TCHAR_TO_ANSI(GXboxWeaponWheelSlots[i].IconName) );
            }
        }
    }
}

static UClass* XboxWeaponWheelLoadClass( const TCHAR* ClassName )
{
    return ClassName ? UObject::StaticLoadClass( AWeapon::StaticClass(), NULL, ClassName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) : NULL;
}

static AActor* XboxWeaponWheelGetPreviewActor( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor || !Viewport->Actor->XLevel )
        return NULL;

    if( GXboxWeaponWheelPreviewActor && GXboxWeaponWheelPreviewLevel == Viewport->Actor->XLevel )
        return GXboxWeaponWheelPreviewActor;

    GXboxWeaponWheelPreviewActor = NULL;
    GXboxWeaponWheelPreviewLevel = NULL;

    UClass* MeshActorClass = FindObject<UClass>( ANY_PACKAGE, TEXT("MeshActor") );
    if( !MeshActorClass )
        MeshActorClass = UObject::StaticLoadClass( AActor::StaticClass(), NULL, TEXT("UMenu.MeshActor"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    if( !MeshActorClass )
        MeshActorClass = AInfo::StaticClass();

    GXboxWeaponWheelPreviewActor = Viewport->Actor->XLevel->SpawnActor( MeshActorClass, NAME_None, NULL, NULL, FVector(0,0,0), FRotator(0,0,0), NULL, 1 );
    GXboxWeaponWheelPreviewLevel = Viewport->Actor->XLevel;
    if( GXboxWeaponWheelPreviewActor )
    {
        GXboxWeaponWheelPreviewActor->DrawType = DT_Mesh;
        GXboxWeaponWheelPreviewActor->bHidden = 1;
        GXboxWeaponWheelPreviewActor->bUnlit = 1;
        GXboxWeaponWheelPreviewActor->bCollideActors = 0;
        GXboxWeaponWheelPreviewActor->bCollideWorld = 0;
        GXboxWeaponWheelPreviewActor->bBlockActors = 0;
        GXboxWeaponWheelPreviewActor->bBlockPlayers = 0;
        GXboxWeaponWheelPreviewActor->AmbientGlow = 255;
        GXboxWeaponWheelPreviewActor->bMeshEnviroMap = 0;
    }

    if( GXboxWeaponWheelLogCount < 64 )
    {
        GXboxWeaponWheelLogCount++;
        GXboxLog.Write( "XWHEEL preview actor %s", GXboxWeaponWheelPreviewActor ? "created" : "missing" );
    }
    return GXboxWeaponWheelPreviewActor;
}

static void XboxWeaponWheelLoadPickupMeshes()
{
    for( INT i=0; i<ARRAY_COUNT(GXboxWeaponWheelSlots); i++ )
    {
        if( GXboxWeaponWheelSlots[i].PickupMesh )
            continue;

        UClass* WeaponClass = XboxWeaponWheelLoadClass( GXboxWeaponWheelSlots[i].ClassName );
        AInventory* Defaults = WeaponClass ? Cast<AInventory>( WeaponClass->GetDefaultObject() ) : NULL;
        if( Defaults )
        {
            GXboxWeaponWheelSlots[i].PickupMesh = Defaults->PickupViewMesh ? Defaults->PickupViewMesh : Defaults->Mesh;
            GXboxWeaponWheelSlots[i].PickupScale = Defaults->PickupViewScale > 0.0f ? Defaults->PickupViewScale : 1.0f;
            GXboxWeaponWheelSlots[i].PickupRotation = Defaults->Rotation;
        }

        if( GXboxWeaponWheelLogCount < 64 )
        {
            GXboxWeaponWheelLogCount++;
            GXboxLog.Write( "XWHEEL pickup mesh slot=%d weapon=%s mesh=%s scale=%.3f",
                i,
                TCHAR_TO_ANSI(GXboxWeaponWheelSlots[i].DisplayName),
                GXboxWeaponWheelSlots[i].PickupMesh ? TCHAR_TO_ANSI(GXboxWeaponWheelSlots[i].PickupMesh->GetName()) : "missing",
                GXboxWeaponWheelSlots[i].PickupScale );
        }
    }
}

static void XboxWeaponWheelDrawPickupMesh( UXboxViewport* Viewport, UCanvas* Canvas, INT SlotIndex, FLOAT X, FLOAT Y, FLOAT W, FLOAT H, UBOOL bAvailable, UBOOL bFocus )
{
    if( !Viewport || !Canvas || !Canvas->Frame || !Canvas->Render || !Viewport->Actor || !Viewport->RenDev )
        return;

    SlotIndex = Clamp<INT>( SlotIndex, 0, ARRAY_COUNT(GXboxWeaponWheelSlots)-1 );
    UMesh* Mesh = GXboxWeaponWheelSlots[SlotIndex].PickupMesh;
    if( !Mesh )
        return;

    AActor* Actor = XboxWeaponWheelGetPreviewActor( Viewport );
    if( !Actor )
        return;

    FLOAT OldFov = Viewport->Actor->FovAngle;
    UBOOL bOldHidden = Actor->bHidden;
    UBOOL bOldUnlit = Actor->bUnlit;
    UBOOL bOldMeshEnviroMap = Actor->bMeshEnviroMap;
    UBOOL bOldMeshCurvy = Actor->bMeshCurvy;
    BYTE OldAmbientGlow = Actor->AmbientGlow;
    BYTE OldStyle = Actor->Style;
    FLOAT OldScaleGlow = Actor->ScaleGlow;
    UMesh* OldMesh = Actor->Mesh;
    UTexture* OldTexture = Actor->Texture;
    UTexture* OldSkin = Actor->Skin;
    UTexture* OldMultiSkins[8];
    INT SkinIndex;
    for( SkinIndex=0; SkinIndex<8; SkinIndex++ )
        OldMultiSkins[SkinIndex] = Actor->MultiSkins[SkinIndex];
    FLOAT OldDrawScale = Actor->DrawScale;
    FVector OldLocation = Actor->Location;
    FRotator OldRotation = Actor->Rotation;
    INT OldX = Canvas->Frame->X;
    INT OldY = Canvas->Frame->Y;
    INT OldXB = Canvas->Frame->XB;
    INT OldYB = Canvas->Frame->YB;
    INT OldRendMap = Viewport->Actor->RendMap;

    UClass* WeaponClass = XboxWeaponWheelLoadClass( GXboxWeaponWheelSlots[SlotIndex].ClassName );
    AActor* Defaults = WeaponClass ? Cast<AActor>( WeaponClass->GetDefaultObject() ) : NULL;

    Viewport->Actor->FovAngle = 32.0f;
    FLOAT FovRadians = Viewport->Actor->FovAngle * PI / 180.0f;

    Actor->Mesh = Mesh;
    Actor->DrawType = DT_Mesh;
    Actor->DrawScale = GXboxWeaponWheelSlots[SlotIndex].PickupScale;
    Actor->Rotation = GXboxWeaponWheelSlots[SlotIndex].PickupRotation + FRotator(0, -16384, 0);
    Actor->bHidden = 0;
    Actor->bUnlit = 1;
    Actor->Style = Defaults ? Defaults->Style : STY_Normal;
    Actor->Texture = Defaults ? Defaults->Texture : NULL;
    Actor->Skin = Defaults ? Defaults->Skin : NULL;
    Actor->bMeshEnviroMap = Defaults ? Defaults->bMeshEnviroMap : 0;
    Actor->bMeshCurvy = Defaults ? Defaults->bMeshCurvy : 0;
    for( SkinIndex=0; SkinIndex<8; SkinIndex++ )
        Actor->MultiSkins[SkinIndex] = Defaults ? Defaults->MultiSkins[SkinIndex] : NULL;
    Actor->AmbientGlow = bAvailable ? (bFocus ? 255 : 224) : 100;
    Actor->ScaleGlow = bAvailable ? 1.0f : 0.34f;

    FSphere BaseSphere = Mesh->GetRenderBoundingSphere( Actor, 0 );
    FLOAT BaseRadius = Max<FLOAT>( BaseSphere.W, 1.0f );
    FLOAT WantedRadius = Min<FLOAT>( W, H ) * (bFocus ? 0.34f : 0.30f);
    FLOAT Distance = 18.0f;
    Actor->DrawScale = Clamp<FLOAT>( (WantedRadius * Distance * appTan(FovRadians * 0.5f)) / (BaseRadius * Max<FLOAT>(H, 1.0f)), 0.015f, 0.45f );
    FSphere Sphere = Mesh->GetRenderBoundingSphere( Actor, 0 );
    Actor->Location = FVector( Distance, -Sphere.Y * Actor->DrawScale, -Sphere.Z * Actor->DrawScale );

    Canvas->Frame->X = (INT)W;
    Canvas->Frame->Y = (INT)H;
    Canvas->Frame->XB = (INT)X;
    Canvas->Frame->YB = (INT)Y;
    Canvas->Frame->ComputeRenderCoords( FVector(0,0,0), FRotator(0,0,0) );
    Canvas->Frame->ComputeRenderSize();

    XboxRenderBeginMenuMeshSlot( Canvas->Frame, X, Y, W, H );
    Canvas->Render->DrawActor( Canvas->Frame, Actor );
    XboxRenderEndMenuMeshSlot( Canvas->Frame );

    Actor->bHidden = bOldHidden;
    Actor->bUnlit = bOldUnlit;
    Actor->bMeshEnviroMap = bOldMeshEnviroMap;
    Actor->bMeshCurvy = bOldMeshCurvy;
    Actor->AmbientGlow = OldAmbientGlow;
    Actor->Style = OldStyle;
    Actor->ScaleGlow = OldScaleGlow;
    Actor->Mesh = OldMesh;
    Actor->Texture = OldTexture;
    Actor->Skin = OldSkin;
    for( SkinIndex=0; SkinIndex<8; SkinIndex++ )
        Actor->MultiSkins[SkinIndex] = OldMultiSkins[SkinIndex];
    Actor->DrawScale = OldDrawScale;
    Actor->Location = OldLocation;
    Actor->Rotation = OldRotation;
    Viewport->Actor->RendMap = OldRendMap;
    Canvas->Frame->X = OldX;
    Canvas->Frame->Y = OldY;
    Canvas->Frame->XB = OldXB;
    Canvas->Frame->YB = OldYB;
    Canvas->Frame->ComputeRenderCoords( FVector(0,0,0), FRotator(0,0,0) );
    Canvas->Frame->ComputeRenderSize();
    Viewport->Actor->FovAngle = OldFov;
}

static AWeapon* XboxWeaponWheelFindWeapon( APlayerPawn* Player, INT SlotIndex )
{
    if( !Player || SlotIndex < 0 || SlotIndex >= ARRAY_COUNT(GXboxWeaponWheelSlots) )
        return NULL;

    UClass* WeaponClass = XboxWeaponWheelLoadClass( GXboxWeaponWheelSlots[SlotIndex].ClassName );
    if( !WeaponClass )
        return NULL;

    for( AInventory* Inv=Player->Inventory; Inv; Inv=Inv->Inventory )
    {
        AWeapon* Weapon = Cast<AWeapon>( Inv );
        if( Weapon && Weapon->IsA(WeaponClass) )
            return Weapon;
    }
    return NULL;
}

static INT XboxWeaponWheelAmmoAmount( AWeapon* Weapon )
{
    if( !Weapon || !Weapon->AmmoType )
        return 0;

    UProperty* AmmoProp = FindField<UProperty>( Weapon->AmmoType->GetClass(), TEXT("AmmoAmount") );
    if( !AmmoProp )
        return 0;

    BYTE* Data = (BYTE*)Weapon->AmmoType + AmmoProp->Offset;
    if( Cast<UIntProperty>(AmmoProp) )
        return *(INT*)Data;
    if( Cast<UByteProperty>(AmmoProp) )
        return *(BYTE*)Data;

    TCHAR Value[64]=TEXT("");
    AmmoProp->ExportText( 0, Value, (BYTE*)Weapon->AmmoType, (BYTE*)Weapon->AmmoType, PPF_Localized );
    return appAtoi( Value );
}

static UBOOL XboxWeaponWheelCanSelect( AWeapon* Weapon )
{
    if( !Weapon )
        return 0;
    return !Weapon->AmmoType || XboxWeaponWheelAmmoAmount( Weapon ) > 0;
}

static void XboxWeaponWheelSelect( UXboxViewport* Viewport, APlayerPawn* Player, INT SlotIndex )
{
    if( !Viewport || !Player || !Viewport->Input )
        return;

    AWeapon* Weapon = XboxWeaponWheelFindWeapon( Player, SlotIndex );
    if( !XboxWeaponWheelCanSelect( Weapon ) )
        return;

    if( Player->Weapon == Weapon )
        return;

    TCHAR Cmd[128];
    appSprintf( Cmd, TEXT("GetWeapon %s"), GXboxWeaponWheelSlots[SlotIndex].ClassName );
    Viewport->Input->Exec( Cmd, *GLog );

    if( GXboxWeaponWheelLogCount < 64 )
    {
        GXboxWeaponWheelLogCount++;
        GXboxLog.Write( "XWHEEL selected slot=%d weapon=%s ammo=%d",
            SlotIndex,
            TCHAR_TO_ANSI(GXboxWeaponWheelSlots[SlotIndex].DisplayName),
            XboxWeaponWheelAmmoAmount(Weapon) );
    }
}

static void XboxWeaponCycle( UXboxViewport* Viewport, APlayerPawn* Player, UBOOL bForward )
{
    if( !Viewport || !Player || !Viewport->Input )
        return;

    Viewport->Input->Exec( bForward ? TEXT("NextWeapon") : TEXT("PrevWeapon"), *GLog );
    if( GXboxWeaponWheelLogCount < 64 )
    {
        GXboxWeaponWheelLogCount++;
        GXboxLog.Write( "XWHEEL tap cycle %s player=0x%08X", bForward ? "next" : "prev", (DWORD)Player );
    }
}

static INT XboxWeaponWheelSlotFromStick( const XINPUT_GAMEPAD& Pad, INT CurrentSlot )
{
    const SHORT Threshold = 9000;
    FLOAT X = (FLOAT)Pad.sThumbRX;
    FLOAT Y = (FLOAT)Pad.sThumbRY;
    if( X > -Threshold && X < Threshold && Y > -Threshold && Y < Threshold )
        return CurrentSlot;

    const FLOAT Slice = 6.28318530718f / 16.0f;
    const FLOAT GapHalf = Slice * 1.5f;
    FLOAT Angle = XboxWeaponWheelAngleFromTopCCW( X, -Y );
    if( Angle < GapHalf )
        return 0;
    if( Angle >= 6.28318530718f - GapHalf )
        return ARRAY_COUNT(GXboxWeaponWheelSlots)-1;

    INT Slot = appFloor( (Angle - GapHalf) / Slice );
    return Clamp<INT>( Slot, 0, ARRAY_COUNT(GXboxWeaponWheelSlots)-1 );
}

static BYTE XboxDodgeDirectionFromStick( const XINPUT_GAMEPAD& Pad )
{
    const SHORT Threshold = 14000;
    SHORT LX = Pad.sThumbLX;
    SHORT LY = Pad.sThumbLY;
    if( LX > -Threshold && LX < Threshold && LY > -Threshold && LY < Threshold )
        return DODGE_None;

    if( Abs<INT>(LX) > Abs<INT>(LY) )
        return LX < 0 ? DODGE_Left : DODGE_Right;
    return LY < 0 ? DODGE_Back : DODGE_Forward;
}

static void XboxTriggerDodge( APlayerPawn* Player, const XINPUT_GAMEPAD& Pad )
{
    if( !Player || Player->Physics != PHYS_Walking )
        return;

    BYTE DodgeMove = XboxDodgeDirectionFromStick( Pad );
    if( DodgeMove == DODGE_None )
        return;

    UFunction* DodgeFunc = Player->FindFunction( TEXT("Dodge") );
    if( DodgeFunc )
    {
        struct { BYTE DodgeMove; } Parms;
        Parms.DodgeMove = DodgeMove;
        Player->ProcessEvent( DodgeFunc, &Parms );
    }
    else
    {
        // Fallback through the stock double-click state machine if the current
        // state function table does not expose Dodge() to native ProcessEvent.
        Player->DodgeClickTime = 0.25f;
        Player->DodgeDir = DodgeMove;
        Player->bEdgeForward = DodgeMove == DODGE_Forward;
        Player->bWasForward  = DodgeMove == DODGE_Forward;
        Player->bEdgeBack    = DodgeMove == DODGE_Back;
        Player->bWasBack     = DodgeMove == DODGE_Back;
        Player->bEdgeLeft    = DodgeMove == DODGE_Left;
        Player->bWasLeft     = DodgeMove == DODGE_Left;
        Player->bEdgeRight   = DodgeMove == DODGE_Right;
        Player->bWasRight    = DodgeMove == DODGE_Right;
    }

    if( GXboxWeaponWheelLogCount < 64 )
    {
        GXboxWeaponWheelLogCount++;
        GXboxLog.Write( "XDODGE dir=%d player=0x%08X", DodgeMove, (DWORD)Player );
    }
}

static void XboxWeaponWheelDraw( UXboxViewport* Viewport, UCanvas* Canvas )
{
    if( !Viewport || !Canvas || !Canvas->Frame )
        return;

    INT ViewIndex = Clamp<INT>( XboxViewportIndex(Viewport), 0, 3 );
    if( !GXboxWeaponWheelActive[ViewIndex] )
        return;

    APlayerPawn* Player = Viewport->Actor;
    if( !Player )
        return;

    FLOAT CX = Canvas->ClipX * 0.5f;
    FLOAT CY = Canvas->ClipY * 0.46f;
    FLOAT OuterR = Min<FLOAT>( Canvas->ClipX, Canvas->ClipY ) * 0.31f;
    FLOAT InnerR = OuterR * 0.58f;
    FLOAT IconR = (OuterR + InnerR) * 0.5f;
    FLOAT IconSize = Max<FLOAT>( 24.0f, Min<FLOAT>( Canvas->ClipX, Canvas->ClipY ) * 0.062f );

    INT Focus = Clamp<INT>( GXboxWeaponWheelFocus[ViewIndex], 0, ARRAY_COUNT(GXboxWeaponWheelSlots)-1 );
    AWeapon* FocusWeapon = XboxWeaponWheelFindWeapon( Player, Focus );
    INT FocusAmmo = XboxWeaponWheelAmmoAmount( FocusWeapon );

    for( INT i=0; i<ARRAY_COUNT(GXboxWeaponWheelSlots); i++ )
    {
        AWeapon* Weapon = XboxWeaponWheelFindWeapon( Player, i );
        UBOOL bAvailable = XboxWeaponWheelCanSelect( Weapon );
        UBOOL bFocus = i == Focus;
        XboxWeaponWheelDrawSlice( Canvas, CX, CY, InnerR-2.0f, OuterR+2.0f, i, 18, 32, 58, bFocus ? 132 : 86 );

        BYTE SliceR = bFocus ? 214 : 184;
        BYTE SliceG = bFocus ? 224 : 218;
        BYTE SliceB = bFocus ? 228 : 212;
        BYTE SliceA = bAvailable ? (bFocus ? 162 : 122) : 66;
        XboxWeaponWheelDrawSlice( Canvas, CX, CY, InnerR, OuterR, i, SliceR, SliceG, SliceB, SliceA );

        if( bFocus )
            XboxWeaponWheelDrawSlice( Canvas, CX, CY, InnerR+5.0f, OuterR-5.0f, i, 80, 166, 255, 52 );
    }

    for( INT i=0; i<ARRAY_COUNT(GXboxWeaponWheelSlots); i++ )
    {
        AWeapon* Weapon = XboxWeaponWheelFindWeapon( Player, i );
        UBOOL bAvailable = XboxWeaponWheelCanSelect( Weapon );
        FLOAT StartAngle, EndAngle;
        XboxWeaponWheelSliceBounds( i, StartAngle, EndAngle );
        FLOAT MidAngle = (StartAngle + EndAngle) * 0.5f;
        FLOAT SlotIconSize = IconSize * GXboxWeaponWheelSlots[i].SpriteScale;
        FLOAT X = CX - appSin( MidAngle ) * IconR - SlotIconSize * 0.5f;
        FLOAT Y = CY - appCos( MidAngle ) * IconR - SlotIconSize * 0.5f;


        if( GXboxWeaponWheelSlots[i].SpriteName )
        {
            if( i == Focus && bAvailable )
                XboxRenderDrawMenuTexture( Canvas->Frame, GXboxWeaponWheelSlots[i].SpriteName, X-3.0f, Y-3.0f, SlotIconSize+6.0f, SlotIconSize+6.0f, 0.34f );
            if( XboxRenderDrawMenuTexture( Canvas->Frame, GXboxWeaponWheelSlots[i].SpriteName, X, Y, SlotIconSize, SlotIconSize, bAvailable ? 1.0f : 0.32f ) )
                continue;
            if( GXboxWeaponWheelSpriteFailLogCount < 32 )
            {
                GXboxWeaponWheelSpriteFailLogCount++;
                GXboxLog.Write( "XWHEEL sprite draw failed slot=%d asset=%s", i, GXboxWeaponWheelSlots[i].SpriteName );
            }
            continue;
        }
        if( GXboxWeaponWheelSlots[i].Icon )
        {
            if( bAvailable )
            {
                if( i == Focus )
                    XboxMenuDrawTextureTint( Canvas, GXboxWeaponWheelSlots[i].Icon, X-5.0f, Y-5.0f, IconSize+10.0f, IconSize+10.0f, 62, 154, 255, 118 );
                XboxMenuDrawTextureTint( Canvas, GXboxWeaponWheelSlots[i].Icon, X, Y, IconSize, IconSize, 255, 255, 255, 255 );
            }
            else
                XboxMenuDrawTextureTint( Canvas, GXboxWeaponWheelSlots[i].Icon, X, Y, IconSize, IconSize, 94, 98, 102, 175 );
        }
    }

    XboxMenuDrawRect( Canvas, CX-8, CY-1, CX+8, CY+1, 215, 235, 255, 0.88f );
    XboxMenuDrawRect( Canvas, CX-1, CY-8, CX+1, CY+8, 215, 235, 255, 0.88f );

    TCHAR Label[128];
    appSprintf( Label, TEXT("%s - (%d)"), GXboxWeaponWheelSlots[Focus].DisplayName, FocusAmmo );
    INT XL=0, YL=0;
    XboxMenuTextSize( Canvas, Canvas->MedFont, Label, XL, YL );
    UBOOL bFocusAvailable = XboxWeaponWheelCanSelect( FocusWeapon );
    BYTE TextR = bFocusAvailable ? 255 : 120;
    BYTE TextG = bFocusAvailable ? 255 : 130;
    BYTE TextB = bFocusAvailable ? 255 : 140;
    XboxMenuText( Canvas, Canvas->MedFont, CX - XL * 0.5f, Canvas->ClipY - 58.0f, TextR, TextG, TextB, Label );
}

static BYTE XboxMenuColorByte( INT ColorIndex, INT Component )
{
    ColorIndex = Clamp<INT>( ColorIndex, 0, ARRAY_COUNT(GXboxColorNames)-1 );
    return (BYTE)Clamp<INT>( GXboxColorTriples[ColorIndex][Component] * 16, 0, 255 );
}

static void XboxMenuDrawSlider( UCanvas* Canvas, FLOAT X, FLOAT Y, FLOAT W, FLOAT Value, FLOAT MinValue, FLOAT MaxValue )
{
    FLOAT T = 0.0f;
    if( MaxValue > MinValue )
        T = Clamp<FLOAT>( (Value - MinValue) / (MaxValue - MinValue), 0.0f, 1.0f );

    XboxMenuDrawRect( Canvas, X, Y, X+W, Y+4, 5, 16, 32, 0.88f );
    XboxMenuDrawRect( Canvas, X, Y, X+W*T, Y+4, 38, 142, 220, 0.92f );
    XboxMenuDrawRect( Canvas, X-1, Y-2, X+1, Y+6, 145, 185, 220, 0.65f );
    XboxMenuDrawRect( Canvas, X+W-1, Y-2, X+W+1, Y+6, 145, 185, 220, 0.65f );
    XboxMenuDrawRect( Canvas, X+W*T-2, Y-4, X+W*T+2, Y+8, 220, 235, 250, 0.95f );
}

static void XboxMenuDrawPreviewCrosshair( UCanvas* Canvas, FLOAT CX, FLOAT CY, INT Crosshair, INT ColorIndex )
{
    BYTE R = XboxMenuColorByte( ColorIndex, 0 );
    BYTE G = XboxMenuColorByte( ColorIndex, 1 );
    BYTE B = XboxMenuColorByte( ColorIndex, 2 );
    Crosshair = Clamp<INT>( Crosshair, 0, 8 );

    if( Crosshair == 0 )
    {
        XboxMenuDrawRect( Canvas, CX-2, CY-18, CX+2, CY-7, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY+7, CX+2, CY+18, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-18, CY-2, CX-7, CY+2, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+7, CY-2, CX+18, CY+2, R, G, B, 1.0f );
    }
    else if( Crosshair == 1 )
    {
        XboxMenuDrawRect( Canvas, CX-20, CY-20, CX-14, CY-14, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+14, CY-20, CX+20, CY-14, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-20, CY+14, CX-14, CY+20, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+14, CY+14, CX+20, CY+20, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY-2, CX+2, CY+2, R, G, B, 1.0f );
    }
    else if( Crosshair == 2 )
    {
        XboxMenuDrawRect( Canvas, CX-24, CY-2, CX-10, CY+2, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+10, CY-2, CX+24, CY+2, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY-24, CX+2, CY-10, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY+10, CX+2, CY+24, R, G, B, 1.0f );
    }
    else if( Crosshair == 3 )
    {
        XboxMenuDrawRect( Canvas, CX-18, CY-18, CX+18, CY-14, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-18, CY+14, CX+18, CY+18, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-18, CY-18, CX-14, CY+18, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+14, CY-18, CX+18, CY+18, R, G, B, 1.0f );
    }
    else if( Crosshair == 4 )
    {
        XboxMenuDrawRect( Canvas, CX-3, CY-22, CX+3, CY+22, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-22, CY-3, CX+22, CY+3, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY-2, CX+2, CY+2, 0, 0, 0, 1.0f );
    }
    else if( Crosshair == 5 )
    {
        XboxMenuDrawRect( Canvas, CX-28, CY-2, CX-16, CY+2, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+16, CY-2, CX+28, CY+2, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY-28, CX+2, CY-16, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-2, CY+16, CX+2, CY+28, R, G, B, 1.0f );
    }
    else if( Crosshair == 6 )
    {
        XboxMenuDrawRect( Canvas, CX-12, CY-12, CX+12, CY-8, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-12, CY+8, CX+12, CY+12, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-12, CY-12, CX-8, CY+12, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+8, CY-12, CX+12, CY+12, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-3, CY-3, CX+3, CY+3, R, G, B, 1.0f );
    }
    else if( Crosshair == 7 )
    {
        XboxMenuDrawRect( Canvas, CX-26, CY-26, CX-18, CY-18, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+18, CY-26, CX+26, CY-18, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-26, CY+18, CX-18, CY+26, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX+18, CY+18, CX+26, CY+26, R, G, B, 1.0f );
    }
    else
    {
        XboxMenuDrawRect( Canvas, CX-20, CY-1, CX+20, CY+1, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-1, CY-20, CX+1, CY+20, R, G, B, 1.0f );
        XboxMenuDrawRect( Canvas, CX-10, CY-10, CX+10, CY+10, R, G, B, 0.28f );
    }
}

static void XboxMenuDrawSettingsPreview( UCanvas* Canvas, UFont* Font, INT Crosshair )
{
    FLOAT X1 = 460.0f;
    FLOAT Y1 = 92.0f;
    FLOAT X2 = 600.0f;
    FLOAT Y2 = 282.0f;
    BYTE HudR = XboxMenuColorByte( GXboxSettingsHudColor, 0 );
    BYTE HudG = XboxMenuColorByte( GXboxSettingsHudColor, 1 );
    BYTE HudB = XboxMenuColorByte( GXboxSettingsHudColor, 2 );
    FLOAT HudA = Clamp<FLOAT>( (FLOAT)GXboxSettingsHudOpacity / 16.0f, 0.06f, 1.0f );

    XboxMenuDrawRect( Canvas, X1-8, Y1-8, X2+8, Y2+8, 0, 0, 0, 0.62f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y2, 6, 24, 48, 0.78f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y1+3, 28, 108, 205, 0.86f );
    XboxMenuText( Canvas, Font, X1+14, Y1+14, 135, 170, 205, TEXT("PREVIEW") );

    XboxMenuDrawRect( Canvas, X1+18, Y1+42, X2-18, Y1+118, 0, 0, 0, 0.46f );
    XboxMenuDrawPreviewCrosshair( Canvas, (X1+X2)*0.5f, Y1+80.0f, Crosshair, GXboxSettingsCrosshairColor );

    XboxMenuDrawRect( Canvas, X1+18, Y1+138, X1+78, Y1+174, HudR, HudG, HudB, HudA );
    XboxMenuDrawRect( Canvas, X1+24, Y1+144, X1+70, Y1+151, 255, 255, 255, 0.22f );
    XboxMenuDrawRect( Canvas, X1+88, Y1+138, X2-18, Y1+174, HudR, HudG, HudB, HudA );
    XboxMenuDrawRect( Canvas, X1+96, Y1+148, X2-28, Y1+156, 255, 255, 255, 0.24f );
    XboxMenuText( Canvas, Font, X1+20, Y1+154, 235, 245, 255, TEXT("100") );
    XboxMenuText( Canvas, Font, X1+96, Y1+154, 235, 245, 255, TEXT("AMMO") );
}

static void XboxMenuDrawChrome( UCanvas* Canvas, const TCHAR* Section, UBOOL bShowBack )
{
    FLOAT W = Canvas->ClipX;
    FLOAT H = Canvas->ClipY;

    XboxMenuDrawRect( Canvas, 0, 0, W, H, 2, 6, 14, 0.45f );
    XboxMenuDrawRect( Canvas, 18, 14, W-18, 42, 9, 42, 89, 0.72f );
    XboxMenuDrawRect( Canvas, 18, H-42, W-18, H-14, 9, 42, 89, 0.72f );
    XboxMenuDrawRect( Canvas, 20, 16, W-20, 19, 31, 112, 205, 0.85f );
    XboxMenuDrawRect( Canvas, 20, H-20, W-20, H-17, 31, 112, 205, 0.85f );

    FLOAT ButtonY = (FLOAT)(INT)(H - 40.0f);
    XboxMenuDrawButtonPrompt( Canvas, 38, ButtonY, "button_a.xui", TEXT("SELECT") );
    if( bShowBack )
    {
        XboxMenuDrawButtonPrompt( Canvas, 150, ButtonY, "button_b.xui", TEXT("BACK") );
    }
}

static void XboxMenuDrawMain( UCanvas* Canvas )
{
    static const TCHAR* Items[] =
    {
        TEXT("INSTANT ACTION"),
        TEXT("SYSTEM LINK"),
        TEXT("SPLITSCREEN"),
        TEXT("PLAYER SETUP"),
        TEXT("SETTINGS")
    };

    XboxMenuDrawChrome( Canvas, TEXT("MAIN MENU"), 0 );
    UFont* MainFont = Canvas->MedFont;
    if( !Canvas->Frame || !XboxRenderDrawMenuTexture( Canvas->Frame, "ut_logo.xui", 44, 58, 270, 135, 1.0f ) )
    {
        XboxMenuText( Canvas, Canvas->MedFont, 52, 78, 255, 255, 255, TEXT("UNREAL") );
        XboxMenuText( Canvas, Canvas->MedFont, 52, 120, 255, 255, 255, TEXT("TOURNAMENT") );
    }

    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 202.0f + i * 42.0f;
        if( i == GXboxMenu.MainFocus )
        {
            INT XL = 0;
            INT YL = 0;
            XboxMenuTextSize( Canvas, MainFont, Items[i], XL, YL );
            FLOAT BarX1 = 44.0f;
            FLOAT BarX2 = 62.0f + XL * 1.05f + 16.0f;
            XboxMenuDrawRect( Canvas, BarX1, Y-8, BarX2, Y+24, 12, 82, 166, 0.3f );
            XboxMenuDrawRect( Canvas, BarX1, Y+24, BarX2, Y+28, 28, 108, 205, 0.3f );
            XboxMenuText( Canvas, MainFont, 62, Y, 255, 255, 255, Items[i] );
        }
        else
        {
            XboxMenuText( Canvas, MainFont, 62, Y, 135, 170, 205, Items[i] );
        }
    }
}

static void XboxMenuDrawPause( UCanvas* Canvas )
{
    static const TCHAR* Items[] =
    {
        TEXT("RESUME"),
        TEXT("MAIN MENU"),
        TEXT("SETTINGS")
    };

    XboxMenuDrawChrome( Canvas, TEXT("PAUSED"), 0 );
    XboxMenuDrawButtonPrompt( Canvas, 150, (FLOAT)(INT)(Canvas->ClipY - 40.0f), "button_b.xui", TEXT("RESUME") );
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 58, 96, 255, 255, 255, TEXT("PAUSED") );

    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 170.0f + i * 40.0f;
        if( i == GXboxMenu.PauseFocus )
        {
            INT XL = 0;
            INT YL = 0;
            XboxMenuTextSize( Canvas, MenuFont, Items[i], XL, YL );
            FLOAT BarX1 = 44.0f;
            FLOAT BarX2 = 62.0f + XL * 1.05f + 16.0f;
            XboxMenuDrawRect( Canvas, BarX1, Y-8, BarX2, Y+24, 12, 82, 166, 0.35f );
            XboxMenuDrawRect( Canvas, BarX1, Y+24, BarX2, Y+28, 28, 108, 205, 0.35f );
            XboxMenuText( Canvas, MenuFont, 62, Y, 255, 255, 255, Items[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 62, Y, 135, 170, 205, Items[i] );
        }
    }

    XboxMenuText( Canvas, MenuFont, 58, 372, 135, 170, 205, TEXT("START OR B RESUMES") );
}

static void XboxMenuDrawSplitPause( UViewport* Viewport, UCanvas* Canvas )
{
    if( !Canvas )
        return;

    INT ViewportIndex = XboxViewportIndex( Cast<UXboxViewport>(Viewport) );
    UFont* MenuFont = Canvas->MedFont;

    XboxMenuDrawRect( Canvas, 0, 0, Canvas->ClipX, Canvas->ClipY, 2, 6, 14, 0.42f );
    XboxMenuText( Canvas, MenuFont, 58, 72, 135, 255, 120, TEXT("PAUSED") );

    if( ViewportIndex != 0 )
        return;

    static const TCHAR* Items[] =
    {
        TEXT("RESUME"),
        TEXT("MAIN MENU"),
        TEXT("SETTINGS")
    };

    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 118.0f + i * 30.0f;
        if( i == GXboxMenu.PauseFocus )
        {
            INT XL = 0;
            INT YL = 0;
            XboxMenuTextSize( Canvas, MenuFont, Items[i], XL, YL );
            FLOAT BarX1 = 44.0f;
            FLOAT BarX2 = Min<FLOAT>( Canvas->ClipX - 14.0f, 62.0f + XL * 1.05f + 16.0f );
            XboxMenuDrawRect( Canvas, BarX1, Y-6, BarX2, Y+21, 12, 82, 166, 0.35f );
            XboxMenuDrawRect( Canvas, BarX1, Y+21, BarX2, Y+24, 28, 108, 205, 0.35f );
            XboxMenuText( Canvas, MenuFont, 62, Y, 255, 255, 255, Items[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 62, Y, 135, 170, 205, Items[i] );
        }
    }

    FLOAT ButtonY = Canvas->ClipY - 28.0f;
    XboxMenuDrawButtonPrompt( Canvas, 38, ButtonY, "button_a.xui", TEXT("SELECT") );
    XboxMenuDrawButtonPrompt( Canvas, 150, ButtonY, "button_b.xui", TEXT("RESUME") );
}

static void XboxMenuDrawInstantAction( UCanvas* Canvas )
{
    static const TCHAR* Labels[] =
    {
        TEXT("GAMETYPE"),
        TEXT("ARENA"),
        TEXT("BOTS"),
        TEXT("BOT SKILL"),
        TEXT("FRAG LIMIT"),
        TEXT("TIME LIMIT"),
        TEXT("MUTATORS"),
        TEXT("BEGIN MATCH")
    };

    GXboxMenu.InstantGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1 );
    INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
    const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
    const FXboxDiscoveredOption* Map = NULL;
    if( MapCount > 0 )
    {
        INT MapIndex = Clamp<INT>( GXboxMenu.InstantMap[GXboxMenu.InstantGameType], 0, MapCount-1 );
        Map = &XboxMenuMap( GXboxMenu.InstantGameType, MapIndex );
    }
    UTexture* Preview = Map ? XboxMenuGetMapPreview( *Map->URLValue ) : NULL;
    TCHAR BotValue[16];
    TCHAR FragValue[16];
    TCHAR TimeValue[24];
    TCHAR MutatorValue[64];
    appSprintf( BotValue, TEXT("%i"), GXboxBotCounts[Clamp<INT>(GXboxMenu.InstantBots, 0, ARRAY_COUNT(GXboxBotCounts)-1)] );
    XboxMenuBuildMutatorLabel( MutatorValue, ARRAY_COUNT(MutatorValue) );

    INT FragLimit = GXboxFragLimits[Clamp<INT>(GXboxMenu.InstantFragLimit, 0, ARRAY_COUNT(GXboxFragLimits)-1)];
    if( FragLimit > 0 )
        appSprintf( FragValue, TEXT("%i"), FragLimit );
    else
        appStrcpy( FragValue, TEXT("NONE") );

    INT TimeLimit = GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
    if( TimeLimit > 0 )
        appSprintf( TimeValue, TEXT("%i MINUTES"), TimeLimit );
    else
        appStrcpy( TimeValue, TEXT("NONE") );

    const TCHAR* Values[] =
    {
        *Game.Label,
        Map ? *Map->Label : TEXT("NO MAPS"),
        BotValue,
        GXboxSkillLabels[Clamp<INT>(GXboxMenu.InstantSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1)],
        FragValue,
        TimeValue,
        MutatorValue,
        TEXT("")
    };

    XboxMenuDrawChrome( Canvas, TEXT("INSTANT ACTION"), 1 );
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 46, 70, 255, 255, 255, TEXT("INSTANT ACTION") );
    XboxMenuDrawRect( Canvas, 382, 92, 590, 300, 25, 34, 48, 0.88f );
    XboxMenuDrawRect( Canvas, 392, 102, 580, 290, 0, 0, 0, 0.88f );
    if( Preview )
    {
        FLOAT PW = 188.0f;
        FLOAT PH = 188.0f;
        FLOAT SrcW = Max<FLOAT>( 1.0f, (FLOAT)Preview->USize );
        FLOAT SrcH = Max<FLOAT>( 1.0f, (FLOAT)Preview->VSize );
        FLOAT Scale = Min<FLOAT>( 188.0f / SrcW, 188.0f / SrcH );
        PW = SrcW * Scale;
        PH = SrcH * Scale;
        XboxMenuDrawTexture( Canvas, Preview, 392.0f + (188.0f - PW) * 0.5f, 102.0f + (188.0f - PH) * 0.5f, PW, PH );
    }
    else
    {
        XboxMenuText( Canvas, MenuFont, 416, 180, 210, 230, 245, TEXT("NO PREVIEW") );
    }

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        FLOAT Y = 150.0f + i * 30.0f;
        if( i == GXboxMenu.InstantFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-6, 350, Y+18, 12, 82, 166, 0.55f );
            if( i < 6 )
            {
                XboxMenuText( Canvas, MenuFont, 192, Y, 180, 215, 245, TEXT("<") );
                XboxMenuText( Canvas, MenuFont, 334, Y, 180, 215, 245, TEXT(">") );
            }
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 210, Y, 255, 255, 255, Values[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 210, Y, 180, 205, 230, Values[i] );
        }
    }

    if( GXboxMenu.InstantFocus == 6 )
    {
        XboxMenuText( Canvas, MenuFont, 58, 402, 135, 170, 205, TEXT("A OPENS MUTATOR LIST") );
    }
    else
    {
        XboxMenuText( Canvas, MenuFont, 58, 402, 135, 170, 205, TEXT("DPAD LEFT/RIGHT CHANGES OPTIONS") );
    }
}

static void XboxMenuDrawMutators( UCanvas* Canvas )
{
    XboxMenuDrawInstantAction( Canvas );

    UFont* MenuFont = Canvas->MedFont;
    FLOAT X1 = 116.0f;
    FLOAT Y1 = 72.0f;
    FLOAT X2 = 560.0f;
    FLOAT Y2 = 374.0f;
    FLOAT ListTop = Y1 + 72.0f;
    FLOAT RowStep = 34.0f;
    FLOAT FooterY = Y2 - 44.0f;
    const INT MutatorCount = XboxMenuMutatorCount();
    const INT VisibleRows = 5;
    INT Focus = Clamp<INT>( GXboxMenu.InstantMutatorChoice, 0, MutatorCount-1 );
    INT First = Focus - VisibleRows / 2;
    First = Clamp<INT>( First, 0, Max<INT>(0, MutatorCount - VisibleRows) );
    INT Last = Min<INT>( MutatorCount, First + VisibleRows );

    XboxMenuDrawRect( Canvas, X1-10, Y1-10, X2+10, Y2+10, 0, 0, 0, 0.88f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y2, 7, 34, 76, 0.96f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y1+4, 28, 108, 205, 0.92f );
    XboxMenuDrawRect( Canvas, X1+22, ListTop-10, X2-22, FooterY-20, 5, 25, 58, 0.68f );
    XboxMenuDrawRect( Canvas, X1+22, Y1+54, X2-22, Y1+56, 31, 90, 150, 0.42f );
    XboxMenuDrawRect( Canvas, X1+22, FooterY-12, X2-22, FooterY-10, 31, 90, 150, 0.42f );
    XboxMenuText( Canvas, MenuFont, X1+18, Y1+18, 255, 255, 255, TEXT("MUTATORS") );

    if( First > 0 )
        XboxMenuText( Canvas, MenuFont, X2-84, Y1+18, 135, 190, 225, TEXT("MORE ^") );

    for( INT i=First; i<Last; i++ )
    {
        FLOAT Y = ListTop + (i - First) * RowStep;
        UBOOL bOn = (GXboxMenu.InstantMutatorMask[i >> 5] & (1 << (i & 31))) != 0;
        UBOOL bFocus = (i == Focus);

        if( bFocus )
            XboxMenuDrawRect( Canvas, X1+22, Y-6, X2-22, Y+24, 12, 82, 166, 0.62f );

        XboxMenuText( Canvas, MenuFont, X1+44, Y, bOn ? 255 : 140, bOn ? 255 : 178, bOn ? 255 : 212, bOn ? TEXT("[X]") : TEXT("[ ]") );
        XboxMenuText( Canvas, MenuFont, X1+108, Y, bFocus ? 255 : 180, bFocus ? 255 : 205, bFocus ? 255 : 230, *XboxMenuMutator(i).Label );
    }

    if( Last < MutatorCount )
        XboxMenuText( Canvas, MenuFont, X2-84, FooterY-34, 135, 190, 225, TEXT("MORE v") );

    XboxMenuDrawButtonPrompt( Canvas, X1+30, FooterY, "button_a.xui", TEXT("TOGGLE") );
    XboxMenuDrawButtonPrompt( Canvas, X1+198, FooterY, "button_b.xui", TEXT("BACK") );
}

static void XboxMenuDrawPlayerSetup( UXboxViewport* Viewport, UCanvas* Canvas )
{
    static const TCHAR* Labels[] =
    {
        TEXT("CLASS"),
        TEXT("SKIN"),
        TEXT("FACE"),
        TEXT("VOICE"),
        TEXT("TEAM")
    };

    XboxMenuLoadPlayerState();
    XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
    XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
    XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );

    GXboxMenu.PlayerClass = Clamp<INT>( GXboxMenu.PlayerClass, 0, GXboxPlayerClasses.Num()-1 );
    GXboxMenu.PlayerSkin = Clamp<INT>( GXboxMenu.PlayerSkin, 0, GXboxPlayerSkins.Num()-1 );
    GXboxMenu.PlayerFace = Clamp<INT>( GXboxMenu.PlayerFace, 0, GXboxPlayerFaces.Num()-1 );
    GXboxMenu.PlayerVoice = Clamp<INT>( GXboxMenu.PlayerVoice, 0, GXboxPlayerVoices.Num()-1 );

    const TCHAR* TeamValue = (GXboxMenu.PlayerTeam >= 0 && GXboxMenu.PlayerTeam < ARRAY_COUNT(GXboxPlayerTeams))
        ? GXboxPlayerTeams[GXboxMenu.PlayerTeam]
        : TEXT("NONE");

    const TCHAR* Values[] =
    {
        *GXboxPlayerClasses(GXboxMenu.PlayerClass).Label,
        *GXboxPlayerSkins(GXboxMenu.PlayerSkin).Label,
        *GXboxPlayerFaces(GXboxMenu.PlayerFace).Label,
        *GXboxPlayerVoices(GXboxMenu.PlayerVoice).Label,
        TeamValue
    };

    XboxMenuDrawChrome( Canvas, TEXT("PLAYER SETUP"), 1 );
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 46, 70, 255, 255, 255, TEXT("PLAYER SETUP") );

    XboxMenuDrawRect( Canvas, 350, 58, 626, 422, 25, 34, 48, 0.72f );
    XboxMenuDrawRect( Canvas, 360, 68, 616, 412, 0, 0, 0, 0.52f );
    XboxMenuDrawPlayerPreviewActor( Viewport, Canvas, 360.0f, 68.0f, 256.0f, 344.0f );
    if( !GXboxPlayerPreviewActor || !GXboxPlayerPreviewActor->Mesh )
        XboxMenuText( Canvas, MenuFont, 430, 210, 135, 170, 205, TEXT("NO PREVIEW") );

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        FLOAT Y = 150.0f + i * 34.0f;
        if( i == GXboxMenu.PlayerFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-6, 342, Y+20, 12, 82, 166, 0.55f );
            XboxMenuText( Canvas, MenuFont, 178, Y, 180, 215, 245, TEXT("<") );
            XboxMenuText( Canvas, MenuFont, 326, Y, 180, 215, 245, TEXT(">") );
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 202, Y, 255, 255, 255, Values[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 202, Y, 180, 205, 230, Values[i] );
        }
    }

    XboxMenuText( Canvas, MenuFont, 58, 402, 135, 170, 205, TEXT("DPAD LEFT/RIGHT CHANGES PLAYER") );
}

static void XboxMenuDrawSettings( UXboxViewport* Viewport, UCanvas* Canvas )
{
    static const TCHAR* Labels[] =
    {
        TEXT("LOOK SENSITIVITY"),
        TEXT("MOVE SENSITIVITY"),
        TEXT("INVERT Y"),
        TEXT("STICK DEADZONE"),
        TEXT("BUTTON LAYOUT"),
        TEXT("MUSIC VOLUME"),
        TEXT("SOUND VOLUME"),
        TEXT("ANNOUNCER VOLUME"),
        TEXT("CROSSHAIR"),
        TEXT("HUD COLOR"),
        TEXT("CROSSHAIR COLOR"),
        TEXT("HUD OPACITY"),
        TEXT("WEAPON HAND"),
        TEXT("WEAPON AUTO-SWITCH"),
        TEXT("MATURE LANGUAGE")
    };

    XboxMenuLoadSettings();
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    UFont* MenuFont = Canvas->MedFont;

    TCHAR LookValue[32];
    TCHAR MoveValue[32];
    TCHAR DeadZoneValue[32];
    TCHAR MusicValue[32];
    TCHAR SoundValue[32];
    TCHAR AnnouncerValue[32];
    TCHAR CrosshairValue[32];
    TCHAR OpacityValue[32];
    TCHAR MatureValue[32];
    INT Layout = Client ? XboxMenuButtonLayoutIndex( Client->ButtonLayout ) : 0;
    INT Hand = XboxMenuWeaponHandIndex( Player );
    INT Crosshair = (Player && Player->myHUD) ? Player->myHUD->Crosshair : 0;
    UBOOL AutoSwitch = Player ? !Player->bNeverAutoSwitch : 1;
    UBOOL bNoMature = XboxMenuGetUserBool( TEXT("Botpack.TournamentPlayer"), TEXT("bNoMatureLanguage"), 0 );

    appSprintf( LookValue, TEXT("%i"), Client ? (INT)Client->ScaleRUV : 100 );
    appSprintf( MoveValue, TEXT("%i"), Client ? (INT)Client->ScaleXYZ : 100 );
    appSprintf( DeadZoneValue, TEXT("%i%%"), Client ? (INT)(Client->DeadZone * 100.0f + 0.5f) : 20 );
    appSprintf( MusicValue, TEXT("%i"), GXboxSettingsMusicVolume );
    appSprintf( SoundValue, TEXT("%i"), GXboxSettingsSoundVolume );
    appSprintf( AnnouncerValue, TEXT("%i"), GXboxSettingsAnnouncerVolume );
    appSprintf( CrosshairValue, TEXT("%i"), Crosshair );
    appSprintf( OpacityValue, TEXT("%i"), GXboxSettingsHudOpacity );
    appStrcpy( MatureValue, bNoMature ? TEXT("FILTERED") : TEXT("ON") );

    const TCHAR* Values[] =
    {
        LookValue,
        MoveValue,
        Client && Client->InvertVertical ? TEXT("ON") : TEXT("OFF"),
        DeadZoneValue,
        GXboxButtonLayouts[Layout],
        MusicValue,
        SoundValue,
        AnnouncerValue,
        CrosshairValue,
        GXboxColorNames[GXboxSettingsHudColor],
        GXboxColorNames[GXboxSettingsCrosshairColor],
        OpacityValue,
        GXboxWeaponHands[Hand],
        AutoSwitch ? TEXT("ON") : TEXT("OFF"),
        MatureValue
    };

    XboxMenuDrawChrome( Canvas, TEXT("SETTINGS"), 1 );
    XboxMenuText( Canvas, MenuFont, 46, 62, 255, 255, 255, TEXT("SETTINGS") );
    XboxMenuText( Canvas, MenuFont, 58, 414, 135, 170, 205, TEXT("DPAD LEFT/RIGHT CHANGES OPTIONS") );
    XboxMenuDrawSettingsPreview( Canvas, MenuFont, Crosshair );

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        FLOAT Y = 92.0f + i * 20.0f;
        UBOOL bSlider =
            i == XSR_LookSensitivity ||
            i == XSR_MoveSensitivity ||
            i == XSR_DeadZone ||
            i == XSR_MusicVolume ||
            i == XSR_SoundVolume ||
            i == XSR_AnnouncerVolume;
        FLOAT SliderValue = 0.0f;
        FLOAT SliderMin = 0.0f;
        FLOAT SliderMax = 1.0f;

        if( i == XSR_LookSensitivity )
        {
            SliderValue = Client ? Client->ScaleRUV : 100.0f;
            SliderMin = 25.0f;
            SliderMax = 200.0f;
        }
        else if( i == XSR_MoveSensitivity )
        {
            SliderValue = Client ? Client->ScaleXYZ : 100.0f;
            SliderMin = 25.0f;
            SliderMax = 200.0f;
        }
        else if( i == XSR_DeadZone )
        {
            SliderValue = Client ? Client->DeadZone : 0.20f;
            SliderMin = 0.05f;
            SliderMax = 0.40f;
        }
        else if( i == XSR_MusicVolume )
        {
            SliderValue = (FLOAT)GXboxSettingsMusicVolume;
            SliderMax = 255.0f;
        }
        else if( i == XSR_SoundVolume )
        {
            SliderValue = (FLOAT)GXboxSettingsSoundVolume;
            SliderMax = 255.0f;
        }
        else if( i == XSR_AnnouncerVolume )
        {
            SliderValue = (FLOAT)GXboxSettingsAnnouncerVolume;
            SliderMax = 4.0f;
        }

        if( i == GXboxMenu.SettingsFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-5, 430, Y+17, 12, 82, 166, 0.55f );
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, Labels[i] );
            if( bSlider )
            {
                XboxMenuDrawSlider( Canvas, 248, Y+8, 112, SliderValue, SliderMin, SliderMax );
                XboxMenuText( Canvas, MenuFont, 374, Y, 255, 255, 255, Values[i] );
            }
            else
            {
                XboxMenuText( Canvas, MenuFont, 270, Y, 255, 255, 255, Values[i] );
            }
            XboxMenuText( Canvas, MenuFont, 232, Y, 180, 215, 245, TEXT("<") );
            XboxMenuText( Canvas, MenuFont, 408, Y, 180, 215, 245, TEXT(">") );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, Labels[i] );
            if( bSlider )
            {
                XboxMenuDrawSlider( Canvas, 248, Y+8, 112, SliderValue, SliderMin, SliderMax );
                XboxMenuText( Canvas, MenuFont, 374, Y, 180, 205, 230, Values[i] );
            }
            else
            {
                XboxMenuText( Canvas, MenuFont, 270, Y, 180, 205, 230, Values[i] );
            }
        }
    }
}

static void XboxMenuDrawSystemLink( UCanvas* Canvas )
{
    XboxMenuDrawChrome( Canvas, TEXT("SYSTEM LINK"), 1 );
    UFont* MenuFont = Canvas->MedFont;

    XboxMenuText( Canvas, MenuFont, 58, 74, 255, 255, 255, TEXT("SYSTEM LINK TEST") );
    XboxMenuText( Canvas, MenuFont, 58, 114, 135, 170, 205, TEXT("DISCOVERY PROBE") );

    TCHAR Status[128];
    if( GXboxSystemLink.Started )
        appSprintf( Status, TEXT("LOCAL ID  %08X    PORT %i"), GXboxSystemLink.LocalId, GXboxSystemLink.LocalPort );
    else
        appSprintf( Status, TEXT("NOT STARTED    ERROR %i"), GXboxSystemLink.LastError );
    XboxMenuText( Canvas, MenuFont, 58, 146, 180, 215, 245, Status );

    appSprintf( Status, TEXT("PEERS FOUND  %i"), GXboxSystemLink.Peers.Num() );
    XboxMenuText( Canvas, MenuFont, 58, 186, 255, 255, 255, Status );

    FLOAT Y = 222.0f;
    for( INT i=0; i<GXboxSystemLink.Peers.Num() && i<GXboxSystemLinkMaxPeers; i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        TCHAR AddrText[32];
        XboxSystemLinkFormatAddress( Peer.Address, AddrText, ARRAY_COUNT(AddrText) );
        appSprintf( Status, TEXT("%08X    %s:%i    PACKETS %i"), Peer.Id, AddrText, Peer.Port, Peer.Packets );
        XboxMenuDrawRect( Canvas, 48, Y-5, 578, Y+20, 12, 82, 166, 0.22f );
        XboxMenuText( Canvas, MenuFont, 62, Y, 180, 215, 245, Status );
        Y += 32.0f;
    }

    if( GXboxSystemLink.Peers.Num() == 0 )
        XboxMenuText( Canvas, MenuFont, 62, Y, 135, 170, 205, TEXT("WAITING FOR ANOTHER INSTANCE...") );

    XboxMenuText( Canvas, MenuFont, 58, 402, 135, 170, 205, TEXT("OPEN THIS SCREEN ON BOTH INSTANCES") );
}

static void XboxMenuDrawComingSoon( UCanvas* Canvas )
{
    XboxMenuDrawChrome( Canvas, GXboxMenu.ComingSoonTitle, 1 );
    XboxMenuCenteredText( Canvas, Canvas->MedFont, 150, 255, 255, 255, GXboxMenu.ComingSoonTitle );
    XboxMenuCenteredText( Canvas, Canvas->MedFont, 230, 160, 205, 240, TEXT("COMING SOON") );
}

void XboxMenuPostRender( UViewport* Viewport, UCanvas* Canvas )
{
    guard(XboxMenuPostRender);
    UXboxViewport* XboxViewport = Cast<UXboxViewport>(Viewport);
    INT WheelViewportIndex = XboxViewport ? Clamp<INT>( XboxViewportIndex(XboxViewport), 0, 3 ) : 0;
    if( !Viewport || !Canvas )
        return;
    if( !GXboxMenu.Active )
    {
        if( XboxViewport && GXboxWeaponWheelActive[WheelViewportIndex] )
            XboxWeaponWheelDraw( XboxViewport, Canvas );
        return;
    }

    GXboxMenu.Pulse += 0.04f;
    XboxSystemLinkTick();

    if( GXboxMenu.Screen == XMS_Pause )
    {
        if( GXboxSplitActive )
            XboxMenuDrawSplitPause( Viewport, Canvas );
        else
            XboxMenuDrawPause( Canvas );
    }
    else if( GXboxMenu.Screen == XMS_Main )
        XboxMenuDrawMain( Canvas );
    else if( GXboxMenu.Screen == XMS_InstantAction )
        XboxMenuDrawInstantAction( Canvas );
    else if( GXboxMenu.Screen == XMS_Mutators )
        XboxMenuDrawMutators( Canvas );
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
        XboxMenuDrawPlayerSetup( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_Settings )
        XboxMenuDrawSettings( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_SystemLink )
        XboxMenuDrawSystemLink( Canvas );
    else
        XboxMenuDrawComingSoon( Canvas );

    unguard;
}

static UBOOL XboxAutoFireSmokeEnabled()
{
    return GetFileAttributesA( "D:\\XboxAutoFireSmoke.ini" ) != 0xFFFFFFFF;
}

static void XboxAutoFireSmokeTick( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    static INT   AutoFireFrame = 0;
    static UBOOL AutoFireDown  = 0;
    static UBOOL AutoFireWasOn = 0;

    UBOOL Enabled = XboxAutoFireSmokeEnabled();
    if( Enabled )
    {
        if( !AutoFireWasOn )
        {
            GXboxLog.Write( "XSMOKEINPUT enabled: pulsing LeftMouse from D:\\XboxAutoFireSmoke.ini" );
            AutoFireWasOn = 1;
        }

        AutoFireFrame++;
        UBOOL WantDown = (AutoFireFrame & 7) < 4;
        if( WantDown != AutoFireDown )
        {
            Client->Engine->InputEvent( Viewport, IK_LeftMouse, WantDown ? IST_Press : IST_Release, 0.0f );
            AutoFireDown = WantDown;
        }
    }
    else
    {
        if( AutoFireDown )
            Client->Engine->InputEvent( Viewport, IK_LeftMouse, IST_Release, 0.0f );
        AutoFireFrame = 0;
        AutoFireDown  = 0;
        AutoFireWasOn = 0;
    }
}

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

    // Viewport index maps directly to physical controller port index.
    ControllerPort      = XboxViewportControllerPort( this );
    ControllerHandle    = NULL;
    ControllerConnected = 0;
    appMemzero( &ControllerState,     sizeof(ControllerState)     );
    appMemzero( &PrevControllerState, sizeof(PrevControllerState) );

    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    if( !ControllerHandle )
    {
        ControllerHandle = XboxOpenViewportController( this, DeviceMask );
        ControllerConnected = ControllerHandle ? 1 : 0;
    }
    if( ControllerHandle )
        GXboxLog.Write( "OpenWindow: controller port %d selected (handle=0x%08X)", ControllerPort, (DWORD)ControllerHandle );
    else
        GXboxLog.Write( "OpenWindow: no gamepad available (mask=0x%08X)", DeviceMask );

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
    if( bXboxSplitDummy )
        RenDev = NULL;
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

    if( GXboxSplitActive && bXboxSplitDummy )
        return;

    XboxAutoFireSmokeTick( this );

    DWORD Insertions = 0;
    DWORD Removals = 0;
    XGetDeviceChanges( XDEVICE_TYPE_GAMEPAD, &Insertions, &Removals );
    if( Insertions || Removals )
        GXboxLog.Write( "XINPUT changes insert=0x%08X remove=0x%08X currentPort=%d",
            Insertions, Removals, ControllerPort );

    if( ControllerHandle && ControllerPort >= 0 && (Removals & (1 << ControllerPort)) )
    {
        GXboxLog.Write( "XINPUT current controller removed port=%d", ControllerPort );
        XInputClose( ControllerHandle );
        ControllerHandle = NULL;
        ControllerConnected = 0;
        ControllerPort = XboxViewportControllerPort( this );
        appMemzero( &ControllerState,     sizeof(ControllerState)     );
        appMemzero( &PrevControllerState, sizeof(PrevControllerState) );
    }

    // Try to open the controller if we don't have a handle yet.
    if( !ControllerHandle )
    {
        DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
        static INT NoPadLogCount = 0;
        if( !DeviceMask && NoPadLogCount < 16 )
        {
            NoPadLogCount++;
            GXboxLog.Write( "PollController: no gamepads mask=0x%08X attempt=%d",
                DeviceMask, NoPadLogCount );
        }
        if( DeviceMask )
        {
            ControllerHandle = XboxOpenViewportController( this, DeviceMask );
            ControllerConnected = ControllerHandle ? 1 : 0;
        }
        if( !ControllerHandle )
            return;
    }

    PrevControllerState = ControllerState;
    DWORD PollResult = 0;
    DWORD Result = XInputGetState( ControllerHandle, &ControllerState );
    if( Result == ERROR_SUCCESS )
    {
        ControllerConnected = 1;
        static INT StateLogCount = 0;
        if( StateLogCount < 8
        ||  ControllerState.Gamepad.wButtons
        ||  ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A]
        ||  ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B]
        ||  ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER]
        ||  ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] )
        {
            if( StateLogCount < 32 )
            {
                StateLogCount++;
                GXboxLog.Write( "XINPUT state #%d port=%d poll=0x%08X packet=%lu buttons=0x%04X a=%u b=%u lt=%u rt=%u sticks=%d,%d,%d,%d",
                    StateLogCount, ControllerPort, PollResult, ControllerState.dwPacketNumber,
                    ControllerState.Gamepad.wButtons,
                    ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A],
                    ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B],
                    ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER],
                    ControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER],
                    ControllerState.Gamepad.sThumbLX, ControllerState.Gamepad.sThumbLY,
                    ControllerState.Gamepad.sThumbRX, ControllerState.Gamepad.sThumbRY );
            }
        }
        ProcessControllerInput( ControllerState.Gamepad );
    }
    else
    {
        // Controller disconnected — close handle so we re-open next poll.
        GXboxLog.Write( "XINPUT getstate failed port=%d result=0x%08X poll=0x%08X",
            ControllerPort, Result, PollResult );
        ControllerConnected = 0;
        XInputClose( ControllerHandle );
        ControllerHandle = NULL;
        ControllerPort = XboxViewportControllerPort( this );
    }

    unguard;
}

void UXboxViewport::ProcessControllerInput( const XINPUT_GAMEPAD& Pad )
{
    guard(UXboxViewport::ProcessControllerInput);

    UXboxClient* Client = (UXboxClient*)GetOuter();
    if( !Client || !Client->Engine )
        return;

    APlayerPawn* Player = Actor;
    INT WheelViewportIndex = Clamp<INT>( XboxViewportIndex(this), 0, 3 );
    const BYTE AnalogThreshold = XINPUT_GAMEPAD_MAX_CROSSTALK; // 30
    UBOOL WhiteNow = Pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE] > AnalogThreshold;
    UBOOL WhitePrev = PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_WHITE] > AnalogThreshold;
    UBOOL BlackNow = Pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] > AnalogThreshold;
    UBOOL BlackPrev = PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] > AnalogThreshold;
    UBOOL bWheelInputActive = Player && !GXboxMenu.Active && (GXboxWeaponWheelActive[WheelViewportIndex] || WhiteNow || WhitePrev || BlackNow || BlackPrev);

    if( GXboxSplitActive && GXboxMenu.Active )
    {
        if( XboxMenuHandleInput( this, Pad, PrevControllerState.Gamepad ) )
            return;
    }

    if( !GXboxSplitActive && !bWheelInputActive && XboxMenuHandleInput( this, Pad, PrevControllerState.Gamepad ) )
        return;
    if( GXboxSplitActive
    &&  !bXboxSplitDummy
    &&  !GXboxMenu.Active
    &&  !bWheelInputActive
    &&  (Pad.wButtons & XINPUT_GAMEPAD_START)
    && !(PrevControllerState.Gamepad.wButtons & XINPUT_GAMEPAD_START) )
    {
        XboxMenuOpen( this );
        return;
    }

    if( Player && !GXboxMenu.Active )
    {
        const DOUBLE NowSeconds = appSeconds();
        const DOUBLE HoldSeconds = 0.24;

        if( WhiteNow && !WhitePrev )
            GXboxWeaponWheelPressTime[WheelViewportIndex][0] = NowSeconds;
        if( BlackNow && !BlackPrev )
            GXboxWeaponWheelPressTime[WheelViewportIndex][1] = NowSeconds;

        if( WhiteNow && (NowSeconds - GXboxWeaponWheelPressTime[WheelViewportIndex][0]) >= HoldSeconds )
            GXboxWeaponWheelActive[WheelViewportIndex] = 1;
        if( BlackNow && (NowSeconds - GXboxWeaponWheelPressTime[WheelViewportIndex][1]) >= HoldSeconds )
            GXboxWeaponWheelActive[WheelViewportIndex] = 1;

        if( GXboxWeaponWheelActive[WheelViewportIndex] )
        {
            GXboxWeaponWheelFocus[WheelViewportIndex] = XboxWeaponWheelSlotFromStick( Pad, GXboxWeaponWheelFocus[WheelViewportIndex] );
        }

        if( GXboxWeaponWheelActive[WheelViewportIndex] && !WhiteNow && !BlackNow )
        {
            INT ChosenSlot = Clamp<INT>( GXboxWeaponWheelFocus[WheelViewportIndex], 0, ARRAY_COUNT(GXboxWeaponWheelSlots)-1 );
            GXboxWeaponWheelActive[WheelViewportIndex] = 0;
            XboxWeaponWheelSelect( this, Player, ChosenSlot );
        }
        else if( !GXboxWeaponWheelActive[WheelViewportIndex] )
        {
            if( WhitePrev && !WhiteNow && (NowSeconds - GXboxWeaponWheelPressTime[WheelViewportIndex][0]) < HoldSeconds )
                XboxWeaponCycle( this, Player, 0 );
            if( BlackPrev && !BlackNow && (NowSeconds - GXboxWeaponWheelPressTime[WheelViewportIndex][1]) < HoldSeconds )
                XboxWeaponCycle( this, Player, 1 );
        }
    }

    if( GXboxSplitActive && Player )
    {
        Player->bShowMenu = 0;
        Player->bSpecialMenu = 0;
        if( Player->Level && Player->Level->Pauser != TEXT("") )
        {
            GXboxLog.Write( "XSPLIT cleared unexpected pauser='%s' before input", TCHAR_TO_ANSI(*Player->Level->Pauser) );
            Player->Level->Pauser = TEXT("");
        }
    }

    // ---- Digital buttons (bitmask in wButtons) ----
    // Route edge-triggered digital controls through Unreal's normal binding
    // layer for this viewport.
    struct FDigitalMap { WORD Mask; EInputKey Key; };
    static const FDigitalMap DigitalMap[] =
    {
        { XINPUT_GAMEPAD_DPAD_UP,     IK_JoyPovUp },
        { XINPUT_GAMEPAD_DPAD_DOWN,   IK_JoyPovDown },
        { XINPUT_GAMEPAD_DPAD_LEFT,   IK_JoyPovLeft },
        { XINPUT_GAMEPAD_DPAD_RIGHT,  IK_JoyPovRight },
        { XINPUT_GAMEPAD_BACK,        IK_Tab },        // Scoreboard
        { XINPUT_GAMEPAD_RIGHT_THUMB, IK_Joy6 },       // Center view
    };

    WORD CurDigital  = Pad.wButtons;
    WORD PrevDigital = PrevControllerState.Gamepad.wButtons;
    WORD DigChanged  = CurDigital ^ PrevDigital;
    static INT InputLogCount = 0;

    for( INT i = 0; i < ARRAY_COUNT(DigitalMap); i++ )
    {
        if( GXboxWeaponWheelActive[WheelViewportIndex] && DigitalMap[i].Key == IK_Tab )
            continue;

        if( DigChanged & DigitalMap[i].Mask )
        {
            EInputAction Action = (CurDigital & DigitalMap[i].Mask) ? IST_Press : IST_Release;
            if( InputLogCount < 32 )
            {
                InputLogCount++;
                GXboxLog.Write( "XINPUT digital #%d mask=0x%04X key=%d action=%d",
                    InputLogCount, DigitalMap[i].Mask, DigitalMap[i].Key, Action );
            }
            Client->Engine->InputEvent( this, DigitalMap[i].Key, Action, 0.0f );
        }
    }

    // ---- Analog buttons (bAnalogButtons[0..7], 0-255 value, index not bitmask) ----
    // X remains use/activate. White/Black tap-cycle weapons and hold the
    // weapon wheel; Y is dodge.
    struct FAnalogBtnMap { INT Index; EInputKey Key; };
    static const FAnalogBtnMap AnalogMap[] =
    {
        { XINPUT_GAMEPAD_X,              IK_Enter },      // Use / accept
    };

    for( INT i = 0; i < ARRAY_COUNT(AnalogMap); i++ )
    {
        UBOOL Now  = Pad.bAnalogButtons[AnalogMap[i].Index] > AnalogThreshold;
        UBOOL Prev = PrevControllerState.Gamepad.bAnalogButtons[AnalogMap[i].Index] > AnalogThreshold;
        if( Now != Prev )
        {
            if( InputLogCount < 32 )
            {
                InputLogCount++;
                GXboxLog.Write( "XINPUT analog #%d index=%d value=%d key=%d action=%d",
                    InputLogCount, AnalogMap[i].Index, Pad.bAnalogButtons[AnalogMap[i].Index],
                    AnalogMap[i].Key, Now ? IST_Press : IST_Release );
            }
            Client->Engine->InputEvent( this, AnalogMap[i].Key, Now ? IST_Press : IST_Release, 0.0f );
        }
    }

    // ---- Analog sticks ----
    // WinDrv feeds Unreal a normalized joystick delta multiplied by the
    // configured joystick scale (Default.ini: ScaleXYZ=1000, ScaleRUV=2000).
    // Feeding raw -1..1 Xbox values makes UInput's 0.01 axis multiplier crawl.
    FLOAT DeadZone = Client->DeadZone;
    if( DeadZone < 0.0f )
        DeadZone = 0.0f;
    if( DeadZone > 0.95f )
        DeadZone = 0.95f;
    FLOAT Sensitivity = Client->ControllerSensitivity;

    // Left stick: movement (IK_JoyX = strafe, IK_JoyY = forward/back).
    FLOAT LX = XboxStickAxis( Pad.sThumbLX, DeadZone ) * Client->ScaleXYZ * Sensitivity;
    FLOAT LY = XboxStickAxis( Pad.sThumbLY, DeadZone ) * Client->ScaleXYZ * Sensitivity;

    // Right stick: look (IK_JoyU = yaw, IK_JoyV = pitch).
    FLOAT RX = XboxStickAxis( Pad.sThumbRX, DeadZone ) * Client->ScaleRUV * Sensitivity;
    FLOAT RY = XboxStickAxis( Pad.sThumbRY, DeadZone ) * Client->ScaleRUV * Sensitivity;
    if( GXboxWeaponWheelActive[WheelViewportIndex] )
    {
        RX = 0.0f;
        RY = 0.0f;
    }
    if( Client->InvertVertical )
        RY = -RY;

    if( Player )
    {
        FLOAT OldForward = Player->aForward;
        FLOAT OldBaseY   = Player->aBaseY;
        FLOAT OldStrafe  = Player->aStrafe;
        FLOAT OldTurn    = Player->aTurn;
        FLOAT OldLookUp  = Player->aLookUp;

        UBOOL FaceFireLayout = Client->ButtonLayout == 2;
        UBOOL FireNow =
            Pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] > AnalogThreshold
        ||  (FaceFireLayout && Pad.bAnalogButtons[XINPUT_GAMEPAD_A] > AnalogThreshold);
        UBOOL FirePrev =
            PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] > AnalogThreshold
        ||  (FaceFireLayout && PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A] > AnalogThreshold);
        UBOOL AltFireNow =
            Pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] > AnalogThreshold;
        UBOOL AltFirePrev =
            PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] > AnalogThreshold;
        UBOOL DuckNow = Pad.bAnalogButtons[XINPUT_GAMEPAD_B] > AnalogThreshold;
        UBOOL DuckPrev = PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_B] > AnalogThreshold;
        UBOOL JumpNow = FaceFireLayout
            ? Pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] > AnalogThreshold
            : Pad.bAnalogButtons[XINPUT_GAMEPAD_A] > AnalogThreshold;
        UBOOL JumpPrev = FaceFireLayout
            ? PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] > AnalogThreshold
            : PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_A] > AnalogThreshold;
        UBOOL DodgeNow = Pad.bAnalogButtons[XINPUT_GAMEPAD_Y] > AnalogThreshold;
        UBOOL DodgePrev = PrevControllerState.Gamepad.bAnalogButtons[XINPUT_GAMEPAD_Y] > AnalogThreshold;

        XboxSendGameplayButton( this, IK_Joy1, FireNow, FirePrev );
        XboxSendGameplayButton( this, IK_Joy2, JumpNow, JumpPrev );
        XboxSendGameplayButton( this, IK_Joy3, AltFireNow, AltFirePrev );
        XboxSendGameplayButton( this, IK_Joy4, DuckNow, DuckPrev );
        if( DodgeNow && !DodgePrev )
            XboxTriggerDodge( Player, Pad );
        if( FireNow || AltFireNow )
            Player->bReadyToPlay = 1;

        XboxViewportInputAxis( this, TEXT("aStrafe"), LX, 2.0f );
        XboxViewportInputAxis( this, TEXT("aBaseY"),  LY, 2.0f );
        XboxViewportInputAxis( this, TEXT("aTurn"),   RX, 5.9f );
        XboxViewportInputAxis( this, TEXT("aLookUp"), RY, 3.0f );

        if( GXboxSplitActive )
        {
            static INT SplitInputLogCount = 0;
            if( SplitInputLogCount < 80
            &&  (FireNow || AltFireNow || DuckNow || JumpNow
            ||   Pad.sThumbLX || Pad.sThumbLY || Pad.sThumbRX || Pad.sThumbRY) )
            {
                SplitInputLogCount++;
                const TCHAR* StateName = (Player->GetStateFrame() && Player->GetStateFrame()->StateNode)
                    ? *Player->GetStateFrame()->StateNode->GetFName()
                    : TEXT("None");
                GXboxLog.Write(
                    "XSPLIT input #%d actor=0x%08X state=%s phys=%d showMenu=%d hud=0x%08X loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f acc=%.1f,%.1f,%.1f raw=%d,%d,%d,%d axes=%.2f,%.2f,%.2f,%.2f old=%.2f/%.2f/%.2f/%.2f/%.2f new=%.2f/%.2f/%.2f/%.2f/%.2f fire=%d alt=%d",
                    SplitInputLogCount,
                    (DWORD)Player,
                    TCHAR_TO_ANSI(StateName),
                    (INT)Player->Physics,
                    Player->bShowMenu ? 1 : 0,
                    (DWORD)Player->myHUD,
                    Player->Location.X, Player->Location.Y, Player->Location.Z,
                    Player->Velocity.X, Player->Velocity.Y, Player->Velocity.Z,
                    Player->Acceleration.X, Player->Acceleration.Y, Player->Acceleration.Z,
                    Pad.sThumbLX, Pad.sThumbLY, Pad.sThumbRX, Pad.sThumbRY,
                    LX, LY, RX, RY,
                    OldForward, OldBaseY, OldStrafe, OldTurn, OldLookUp,
                    Player->aForward, Player->aBaseY, Player->aStrafe, Player->aTurn, Player->aLookUp,
                    FireNow ? 1 : 0,
                    AltFireNow ? 1 : 0 );
            }
        }

        if( InputLogCount < 32
        &&  (FireNow || AltFireNow || DuckNow || JumpNow
        ||   LX > 0.01f || LX < -0.01f || LY > 0.01f || LY < -0.01f
        ||   RX > 0.01f || RX < -0.01f || RY > 0.01f || RY < -0.01f) )
        {
            InputLogCount++;
            GXboxLog.Write( "XINPUT gameplay #%d player=0x%08X fire=%d alt=%d duck=%d jump=%d axes=%.2f,%.2f,%.2f,%.2f",
                InputLogCount, Player, FireNow, AltFireNow, DuckNow, JumpNow, LX, LY, RX, RY );
        }
    }

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
    if( GXboxSplitActive && bXboxSplitDummy )
        return;
    XboxSplitSmokeFeedInput( this );
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
