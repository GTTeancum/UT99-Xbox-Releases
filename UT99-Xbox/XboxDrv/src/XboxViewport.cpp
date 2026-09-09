// XboxViewport.cpp

extern "C" UBOOL XboxRenderDrawMenuTexture( FSceneNode* Frame, const char* Name, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha );
extern "C" UBOOL XboxRenderDrawMenuUTexture( FSceneNode* Frame, UTexture* Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha );
extern "C" void  XboxRenderDrawMenuRect( FSceneNode* Frame, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, BYTE A );
extern "C" void  XboxRenderDrawMenuRingSlice( FSceneNode* Frame, FLOAT CX, FLOAT CY, FLOAT InnerR, FLOAT OuterR, FLOAT StartAngle, FLOAT EndAngle, BYTE R, BYTE G, BYTE B, BYTE A );
extern "C" void  XboxRenderBeginMenuMeshSlot( FSceneNode* Frame, FLOAT X, FLOAT Y, FLOAT W, FLOAT H );
extern "C" void  XboxRenderEndMenuMeshSlot( FSceneNode* Frame );
extern "C" void  XboxRenderPrepareMenuText( FSceneNode* Frame, const char* Label );
extern "C" void  XboxRenderFinishMenuText( FSceneNode* Frame );
extern "C" void  XboxRenderSetPendingViewRegion( INT X, INT Y, INT W, INT H, UBOOL ClearFullTarget );
extern "C" void  XboxRenderClearRegion( URenderDevice* RenderDevice, INT X, INT Y, INT W, INT H );
extern "C" void  XboxRenderSetDisplayCalibration( FLOAT Brightness, FLOAT Contrast, FLOAT Gamma );
extern "C" void  XboxRenderReleaseMenuTexture( const char* Name );
extern "C" void  XboxRenderReleaseMenuTextures();
extern "C" void  XboxRenderGetMenuTextureStats( INT* OutCount, INT* OutApproxKB, INT* OutFailures );
extern "C" volatile LONG GXboxAudioToneSmokeState;
extern "C" volatile LONG GXboxAudioMusicLoadState;
extern "C" volatile LONG GXboxAudioMusicPacketState;
extern "C" volatile LONG GXboxAudioMusicStreamState;
extern "C" UBOOL XboxEnsureConsoleClass( UViewport* Viewport, const TCHAR* ConsoleClassName, const char* Reason );
extern UBOOL InitSockets( FString& Error );
extern DWORD GXboxMallocLiveBytes;
extern DWORD GXboxMallocPeakBytes;
extern DWORD GXboxMallocTotalBytes;

#ifndef XBOX_ENABLE_AUDIO_TONE_SMOKE
#define XBOX_ENABLE_AUDIO_TONE_SMOKE 0
#endif

#if TARGET_XBOX
extern "C" void  XboxIpDrvSetSecureTravelHost( const XNADDR* XnAddr, const XNKID* SessionKeyId, const XNKEY* SessionKey, DWORD PreferredAddress );
extern "C" void  XboxIpDrvClearSecureTravelHost();
#endif

static FLOAT XboxStickAxis( SHORT Raw, FLOAT DeadZone )
{
    FLOAT Delta = (FLOAT)Raw / 32768.0f;
    if( Delta > DeadZone )
        return (Delta - DeadZone) / (1.0f - DeadZone);
    if( Delta < -DeadZone )
        return (Delta + DeadZone) / (1.0f - DeadZone);
    return 0.0f;
}

static INT XboxStickLayoutClamp( INT Layout )
{
    return Clamp<INT>( Layout, XSL_Default, XSL_LegacySouthpaw );
}

static const TCHAR* XboxStickLayoutLabel( INT Layout )
{
    switch( XboxStickLayoutClamp(Layout) )
    {
        case XSL_Southpaw:       return TEXT("SOUTHPAW");
        case XSL_Legacy:         return TEXT("LEGACY");
        case XSL_LegacySouthpaw: return TEXT("LEGACY SOUTHPAW");
        default:                 return TEXT("DEFAULT");
    }
}

static void XboxStickLayoutAxes
(
    INT Layout,
    FLOAT LeftX,
    FLOAT LeftY,
    FLOAT RightX,
    FLOAT RightY,
    FLOAT& MoveX,
    FLOAT& MoveY,
    FLOAT& LookX,
    FLOAT& LookY
)
{
    switch( XboxStickLayoutClamp(Layout) )
    {
        case XSL_Southpaw:
            MoveX = RightX;
            MoveY = RightY;
            LookX = LeftX;
            LookY = LeftY;
            break;

        case XSL_Legacy:
            MoveX = RightX;
            MoveY = LeftY;
            LookX = LeftX;
            LookY = RightY;
            break;

        case XSL_LegacySouthpaw:
            MoveX = LeftX;
            MoveY = RightY;
            LookX = RightX;
            LookY = LeftY;
            break;

        default:
            MoveX = LeftX;
            MoveY = LeftY;
            LookX = RightX;
            LookY = RightY;
            break;
    }
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
        static INT FailLogCount[4] = {0,0,0,0};
        INT LogPort = (Port >= 0 && Port < 4) ? Port : 0;
        if( FailLogCount[LogPort] < 4 )
        {
            FailLogCount[LogPort]++;
            GXboxLog.Write( "XINPUT open failed port=%d mask=0x%08X count=%d", Port, DeviceMask, FailLogCount[LogPort] );
        }
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
    INT PortCount = (INT)XGetPortCount();
    if( PortCount <= 0 )
        return 0;

    if( Viewport && Viewport->ControllerPort >= 0 && Viewport->ControllerPort < PortCount )
        return Viewport->ControllerPort;

    INT Port = XboxViewportIndex( Viewport );
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
    XMS_SystemLinkMapSelect,
    XMS_Tournament,
    XMS_TournamentPostMatch,
    XMS_SplitReady,
    XMS_SplitMapSelect,
    XMS_ProfileSelect,
    XMS_PlayerSetup,
    XMS_ProfileName,
    XMS_Controls,
    XMS_Settings,
    XMS_SettingsAudio,
    XMS_SettingsVideo,
    XMS_ComingSoon
};

struct FXboxMenuState
{
    UBOOL Active;
    EXboxMenuScreen Screen;
    INT PauseFocus;
    INT MainFocus;
    INT InstantFocus;
    INT TournamentFocus;
    INT TournamentLadder;
    INT TournamentMatch;
    INT TournamentSkill;
    INT SplitFocus;
    INT PlayerFocus;
    INT ControlsFocus;
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

struct FXboxTournamentPostMatchState
{
    UBOOL Valid;
    UBOOL Advanced;
    INT LadderIndex;
    INT CompletedMatch;
    INT NextMatch;
    INT PendingRank;
    INT PreviousPosition;
    INT NewPosition;
    TCHAR MatchTitle[96];
    TCHAR MapName[96];
    TCHAR RankTitle[64];
};

struct FXboxPendingMatchRules
{
    UBOOL Active;
    ULevel* AppliedLevel;
    TCHAR GameClass[96];
    INT ScoreLimit;
    INT TimeLimit;
    INT MinPlayers;
    INT Skill;
    INT DesiredBots;
    UBOOL TeamScore;
    UBOOL ObjectiveRules;
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
enum { XBOX_CONTROL_BUTTON_COUNT = 10 };
static DOUBLE GXboxWeaponWheelPressTime[4][XBOX_CONTROL_BUTTON_COUNT];
static INT   GXboxWeaponWheelLogCount  = 0;
static INT   GXboxWeaponWheelSpriteFailLogCount = 0;
static ALevelInfo* GXboxWeaponCycleProofLevel = NULL;
static APlayerPawn* GXboxWeaponCycleProofPlayer = NULL;
static INT GXboxWeaponCycleProofStage = 0;
static INT GXboxWeaponCycleProofIteration = 0;
static INT GXboxWeaponCycleProofPasses = 0;
static INT GXboxWeaponCycleProofFailures = 0;
static DOUBLE GXboxWeaponCycleProofStageTime = 0.0;
static AWeapon* GXboxWeaponCycleProofBefore = NULL;
static UBOOL GXboxWeaponCycleProofStageLogged = 0;
static AActor* GXboxWeaponWheelPreviewActor = NULL;
static ULevel* GXboxWeaponWheelPreviewLevel = NULL;
static INT   GXboxMenuVoiceSampleBypass = 0;
static INT   GXboxMenuOwnerViewport = 0;
static UBOOL GXboxMenuGameplayContinues = 0;
static UBOOL GXboxMenuAudioModeActive = 0;
static UBOOL GXboxFrontendMenuOpenPending = 0;
static UBOOL GXboxFrontendTournamentOpenPending = 0;
static UBOOL GXboxFrontendTournamentPostMatchPending = 0;
static FXboxTournamentPostMatchState GXboxTournamentPostMatch;
static FXboxPendingMatchRules GXboxPendingMatchRules = { 0, NULL, TEXT(""), 0, 0, 0, 0, 0, 0, 0 };
static ULevel* GXboxTournamentTransitionHandledLevel = NULL;
static ULevel* GXboxTournamentLastLoggedLevel = NULL;
static APlayerPawn* GXboxTournamentLastLoggedPlayer = NULL;

static const TCHAR* XboxPlayerStateName( APlayerPawn* Player )
{
    return (Player && Player->GetStateFrame() && Player->GetStateFrame()->StateNode)
        ? *Player->GetStateFrame()->StateNode->GetFName()
        : TEXT("None");
}

static UBOOL XboxClassIsNamed( UClass* Class, const TCHAR* Name )
{
    if( !Name )
        return 0;

    for( UClass* Test=Class; Test; Test=Test->GetSuperClass() )
        if( appStricmp( Test->GetName(), Name ) == 0 )
            return 1;
    return 0;
}

static UBOOL XboxIsFrontendLevel( ULevel* Level )
{
    if( !Level )
        return 0;

    if( Level->URL.Map.Len()
    && (appStricmp( *Level->URL.Map, TEXT("CityIntro") ) == 0
    ||  appStricmp( *Level->URL.Map, TEXT("CityIntro.unr") ) == 0
    ||  appStricmp( *Level->URL.Map, TEXT("UT-Logo-Map") ) == 0
    ||  appStricmp( *Level->URL.Map, TEXT("UT-Logo-Map.unr") ) == 0) )
        return 1;

    ALevelInfo* Info = Level->GetLevelInfo();
    UObject* Game = Info ? Info->Game : NULL;
    UClass* GameClass = Game ? Game->GetClass() : NULL;
    return GameClass && appStricmp( GameClass->GetName(), TEXT("UTIntro") ) == 0;
}

static UBOOL XboxIsTournamentLevel( ULevel* Level )
{
    return Level && Level->URL.GetOption( TEXT("Tournament="), NULL ) != NULL;
}

static DWORD XboxMenuAvailPhysKB()
{
    MEMORYSTATUS MemStatus;
    appMemzero( &MemStatus, sizeof(MemStatus) );
    MemStatus.dwLength = sizeof(MemStatus);
    GlobalMemoryStatus( &MemStatus );
    return MemStatus.dwAvailPhys / 1024;
}

static void XboxWeaponWheelReleaseCache()
{
    INT ReleasedIcons = 0;
    INT ReleasedMeshes = 0;
    for( INT i=0; i<ARRAY_COUNT(GXboxWeaponWheelSlots); i++ )
    {
        if( GXboxWeaponWheelSlots[i].Icon )
            ReleasedIcons++;
        if( GXboxWeaponWheelSlots[i].PickupMesh )
            ReleasedMeshes++;
        GXboxWeaponWheelSlots[i].Icon = NULL;
        GXboxWeaponWheelSlots[i].PickupMesh = NULL;
        GXboxWeaponWheelSlots[i].PickupScale = 1.0f;
        GXboxWeaponWheelSlots[i].PickupRotation = FRotator(0,0,0);
    }
    if( GXboxWeaponWheelPreviewActor )
        GXboxWeaponWheelPreviewActor->Destroy();
    GXboxWeaponWheelPreviewActor = NULL;
    GXboxWeaponWheelPreviewLevel = NULL;
    if( ReleasedIcons || ReleasedMeshes )
        GXboxLog.Write( "XWHEEL cache released icons=%d meshes=%d availKB=%u", ReleasedIcons, ReleasedMeshes, (unsigned)XboxMenuAvailPhysKB() );
}

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
    1,
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
static UBOOL GXboxPauseReturnConfirm = 0;
static INT GXboxPauseReturnConfirmFocus = 1;

struct FXboxDiscoveredOption
{
    FString Label;
    FString URLValue;
    FString MapPrefix;
    UBOOL   bMultiSkinGroup;

    FXboxDiscoveredOption()
    : bMultiSkinGroup(0)
    {
    }
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
    FString DefaultPackage;
    FString DefaultSkinName;
    FString SkinValue;
    FString FaceValue;
    char    PortraitName[64];
    INT     DefaultTeam;
    INT     FixedSkin;
    INT     FaceSkin;
    INT     TeamSkin1;
    INT     TeamSkin2;
    UBOOL bMultiSkinned;

    FXboxPlayerClassOption()
    : DefaultTeam(255)
    , FixedSkin(2)
    , FaceSkin(3)
    , TeamSkin1(0)
    , TeamSkin2(1)
    , bMultiSkinned(1)
    {
        PortraitName[0] = 0;
    }
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

struct FXboxTournamentLadderOption
{
    const TCHAR* Label;
    const TCHAR* LadderClass;
    const TCHAR* GameClass;
    INT FirstRatedMatch;
    INT LastMatchType;
};

static const FXboxTournamentLadderOption GXboxTournamentLadders[] =
{
    { TEXT("DEATHMATCH"),       TEXT("Botpack.LadderDM"),   TEXT("Botpack.DeathMatchPlus"), 1, 1 },
    { TEXT("DOMINATION"),       TEXT("Botpack.LadderDOM"),  TEXT("Botpack.Domination"),     1, 3 },
    { TEXT("CAPTURE THE FLAG"), TEXT("Botpack.LadderCTF"),  TEXT("Botpack.CTFGame"),        1, 2 },
    { TEXT("ASSAULT"),          TEXT("Botpack.LadderAS"),   TEXT("Botpack.Assault"),        1, 4 },
    { TEXT("FINAL CHALLENGE"),  TEXT("Botpack.LadderChal"), TEXT("Botpack.ChallengeDMP"),   1, 5 }
};

enum EXboxSettingsHubRow
{
    XSH_Controls,
    XSH_Audio,
    XSH_Video,
    XSH_Count
};

enum EXboxAudioSettingsRow
{
    XAR_MusicVolume,
    XAR_SoundVolume,
    XAR_AnnouncerVolume,
    XAR_Count
};

enum EXboxVideoSettingsRow
{
    XVR_Brightness,
    XVR_Contrast,
    XVR_Gamma,
    XVR_SafeAreaSize,
    XVR_SafeAreaX,
    XVR_SafeAreaY,
    XVR_Crosshair,
    XVR_HudColor,
    XVR_CrosshairColor,
    XVR_HudOpacity,
    XVR_MatureLanguage,
    XVR_Count
};

enum EXboxControlsRow
{
    XCR_LookSensitivity,
    XCR_MoveSensitivity,
    XCR_InvertY,
    XCR_DeadZone,
    XCR_Preset,
    XCR_StickLayout,
    XCR_WeaponHand,
    XCR_AutoSwitch,
    XCR_FirstButton
};

enum EXboxControlButton
{
    XCB_A,
    XCB_B,
    XCB_X,
    XCB_Y,
    XCB_LeftTrigger,
    XCB_RightTrigger,
    XCB_White,
    XCB_Black,
    XCB_Back,
    XCB_RightThumb,
    XCB_Count
};

static INT XboxControlsRowCount()
{
    return XCR_FirstButton + XCB_Count;
}

static INT XboxControlsVisibleRows()
{
    return Min<INT>( XboxControlsRowCount(), 12 );
}

static UBOOL XboxControlsCanScroll()
{
    return XboxControlsRowCount() > XboxControlsVisibleRows();
}

static INT XboxControlsScrollTop()
{
    INT TotalRows = XboxControlsRowCount();
    INT VisibleRows = XboxControlsVisibleRows();
    INT Focus = Clamp<INT>( GXboxMenu.ControlsFocus, 0, TotalRows - 1 );
    INT Top = 0;

    if( Focus >= VisibleRows )
        Top = Focus - VisibleRows + 1;

    return Clamp<INT>( Top, 0, Max<INT>( 0, TotalRows - VisibleRows ) );
}

struct FXboxControlButtonInfo
{
    const char* Image;
    INT AnalogIndex;
    WORD DigitalMask;
};

static const FXboxControlButtonInfo GXboxControlButtons[XCB_Count] =
{
    { "button_a.xui",      XINPUT_GAMEPAD_A,             0 },
    { "button_b.xui",      XINPUT_GAMEPAD_B,             0 },
    { "button_x.xui",      XINPUT_GAMEPAD_X,             0 },
    { "button_y.xui",      XINPUT_GAMEPAD_Y,             0 },
    { "button_lt.xui",     XINPUT_GAMEPAD_LEFT_TRIGGER,  0 },
    { "button_rt.xui",     XINPUT_GAMEPAD_RIGHT_TRIGGER, 0 },
    { "button_white.xui",  XINPUT_GAMEPAD_WHITE,         0 },
    { "button_black.xui",  XINPUT_GAMEPAD_BLACK,         0 },
    { "button_back.xui",   -1,                           XINPUT_GAMEPAD_BACK },
    { "button_rstick.xui", -1,                           XINPUT_GAMEPAD_RIGHT_THUMB }
};

struct FXboxControlActionInfo
{
    const TCHAR* Label;
    EInputKey Key;
    INT CycleDir;
    UBOOL bWheelHold;
    UBOOL bDodge;
    UBOOL bReadyFire;
    UBOOL bReadyAltFire;
};

static const FXboxControlActionInfo GXboxControlActions[] =
{
    { TEXT("NONE"),             IK_None,  0, 0, 0, 0, 0 },
    { TEXT("FIRE"),             IK_Joy1,  0, 0, 0, 1, 0 },
    { TEXT("ALT FIRE"),         IK_Joy3,  0, 0, 0, 0, 1 },
    { TEXT("JUMP"),             IK_Joy2,  0, 0, 0, 0, 0 },
    { TEXT("DUCK"),             IK_Joy4,  0, 0, 0, 0, 0 },
    { TEXT("USE"),              IK_Enter, 0, 0, 0, 0, 0 },
    { TEXT("DODGE"),            IK_None,  0, 0, 1, 0, 0 },
    { TEXT("PREV WEAPON/WHEEL"),IK_None, -1, 1, 0, 0, 0 },
    { TEXT("NEXT WEAPON/WHEEL"),IK_None,  1, 1, 0, 0, 0 },
    { TEXT("SCOREBOARD"),       IK_Tab,   0, 0, 0, 0, 0 },
    { TEXT("CENTER VIEW"),      IK_Joy6,  0, 0, 0, 0, 0 }
};

struct FXboxControlPreset
{
    const TCHAR* Label;
    INT LegacyButtonLayout;
    INT StickLayout;
    INT Actions[XCB_Count];
};

static const FXboxControlPreset GXboxControlPresets[] =
{
    { TEXT("DEFAULT"),          0, XSL_Default,        { XCA_Jump, XCA_Duck, XCA_Use, XCA_Dodge, XCA_AltFire, XCA_Fire, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("FACE FIRE"),        2, XSL_Default,        { XCA_Fire, XCA_Duck, XCA_Use, XCA_Dodge, XCA_AltFire, XCA_Jump, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("ALT SWAP"),         0, XSL_Default,        { XCA_Jump, XCA_Duck, XCA_Use, XCA_Dodge, XCA_Fire, XCA_AltFire, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("SOUTHPAW"),         0, XSL_Southpaw,       { XCA_Jump, XCA_Duck, XCA_Use, XCA_Dodge, XCA_AltFire, XCA_Fire, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("LEGACY"),           0, XSL_Legacy,         { XCA_Jump, XCA_Duck, XCA_Use, XCA_Dodge, XCA_AltFire, XCA_Fire, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("LEGACY SOUTHPAW"),  0, XSL_LegacySouthpaw, { XCA_Jump, XCA_Duck, XCA_Use, XCA_Dodge, XCA_AltFire, XCA_Fire, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("BUMPER JUMPER"),    0, XSL_Default,        { XCA_Use, XCA_Duck, XCA_AltFire, XCA_Dodge, XCA_Jump, XCA_Fire, XCA_PrevWeaponWheel, XCA_NextWeaponWheel, XCA_Scoreboard, XCA_CenterView } },
    { TEXT("TACTICAL"),         0, XSL_Default,        { XCA_Jump, XCA_Dodge, XCA_Use, XCA_AltFire, XCA_PrevWeaponWheel, XCA_Fire, XCA_Scoreboard, XCA_NextWeaponWheel, XCA_None, XCA_Duck } }
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

enum
{
    XBOX_PROFILE_COUNT = 16,
    XBOX_PROFILE_NAME_MAX = 15,
    XBOX_PLAYER_ROW_CHARACTER = 0,
    XBOX_PLAYER_ROW_TEAM,
    XBOX_PLAYER_ROW_COUNT
};

enum EXboxProfileNameMode
{
    XPNM_None,
    XPNM_StartupCreate,
    XPNM_MultiplayerCreate
};

struct FXboxProfileSummary
{
    UBOOL Created;
    TCHAR Name[XBOX_PROFILE_NAME_MAX+1];
};

static FXboxProfileSummary GXboxProfiles[XBOX_PROFILE_COUNT];
static INT GXboxActiveProfile = 0;
static INT GXboxProfileSelected = 0;
static UBOOL GXboxProfilesLoaded = 0;
static UBOOL GXboxSessionProfileLoaded = 0;
static INT GXboxProfileGateFocus = 0;
static TCHAR GXboxProfileName[XBOX_PROFILE_NAME_MAX+1] = TEXT("PLAYER 1");
static TCHAR GXboxProfileEditName[XBOX_PROFILE_NAME_MAX+1] = TEXT("");
static INT GXboxProfileKeyboardFocus = 0;
static EXboxProfileNameMode GXboxProfileNameMode = XPNM_None;
static INT GXboxProfileNamePort = -1;
static EXboxMenuScreen GXboxProfileReturnScreen = XMS_ProfileSelect;
static INT GXboxProfilePreviewPlayerClass = -1;

static const TCHAR* GXboxProfileKeyboardKeys[] =
{
    TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E"), TEXT("F"), TEXT("G"), TEXT("H"),
    TEXT("I"), TEXT("J"), TEXT("K"), TEXT("L"), TEXT("M"), TEXT("N"), TEXT("O"), TEXT("P"),
    TEXT("Q"), TEXT("R"), TEXT("S"), TEXT("T"), TEXT("U"), TEXT("V"), TEXT("W"), TEXT("X"),
    TEXT("Y"), TEXT("Z"), TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"),
    TEXT("6"), TEXT("7"), TEXT("8"), TEXT("9"), TEXT("SPACE"), TEXT("DELETE"), TEXT("CLEAR"), TEXT("DONE")
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
static TArray<FRegistryObjectInfo> GXboxMenuIntObjectCache;
static UBOOL GXboxMenuRegistryCacheRefreshed = 0;
static UBOOL GXboxMenuIntObjectCacheLoaded = 0;
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
struct FXboxPlayerPreviewAssets
{
    INT ClassIndex;
    INT SkinIndex;
    INT FaceIndex;
    INT TeamIndex;
    UMesh* Mesh;
    UTexture* Skin;
    UTexture* MultiSkins[8];
};
static FXboxPlayerPreviewAssets GXboxPlayerPreviewCurrent;
static FXboxPlayerPreviewAssets GXboxPlayerPreviewPrevious;
static UBOOL GXboxPlayerPreviewAssetsInitialized = 0;
struct FXboxPlayerPreviewRootRef
{
    UObject* Object;
    INT Count;
};
static FXboxPlayerPreviewRootRef GXboxPlayerPreviewRootRefs[64];
static INT GXboxPlayerPreviewChangesSinceGC = 0;

static const INT GXboxSystemLinkBasePort = 9777;
static const INT GXboxSystemLinkPortCount = 4;
static const INT GXboxSystemLinkMaxPeers = 8;
static const FLOAT GXboxSystemLinkHostClaimSeconds = 1.5f;
static const FLOAT GXboxSystemLinkPeerTimeoutSeconds = 8.0f;
static const FLOAT GXboxSystemLinkLaunchHostDelaySeconds = 1.0f;
static const FLOAT GXboxSystemLinkLaunchDeadlineSeconds = 5.0f;
static const FLOAT GXboxSystemLinkLaunchCommitDelaySeconds = 1.0f;
static const FLOAT GXboxSystemLinkLaunchClientDelaySeconds = 1.5f;
static const FLOAT GXboxSystemLinkSmokeMapSelectDelaySeconds = 10.0f;
static const FLOAT GXboxSystemLinkSmokeTravelHoldSeconds = 0.0f;
static const INT GXboxSystemLinkGamePort = 7777;
static const FLOAT GXboxSplitDummyRespawnSeconds = 3.0f;

enum EXboxSystemLinkRole
{
    XSLR_Seeking = 0,
    XSLR_Host    = 1,
    XSLR_Client  = 2
};

enum EXboxSystemLinkPhase
{
    XSLP_Discovery       = 0,
    XSLP_Ready           = 1,
    XSLP_ReadyConfirmed  = 2,
    XSLP_MapSelect       = 3,
    XSLP_Launching       = 4
};

static UBOOL GXboxSplitPending = 0;
static UBOOL GXboxSplitActive = 0;
static INT   GXboxSplitActiveMask = 1;
static INT   GXboxSplitActivePlayerCount = 1;
static INT   GXboxSplitRenderViewport = 0;
static INT   GXboxSplitRenderViewportCount = 1;
static FLOAT GXboxSplitDummyDeathTime[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
static UBOOL GXboxSplitBorrowedActor[4] = { 0, 0, 0, 0 };
static UBOOL GXboxSplitSmokeTravelStarted = 0;
static UBOOL GXboxSplitSmokeActive = 0;
static UBOOL GXboxSplitSmokeFinished = 0;
static DOUBLE GXboxSplitSmokeStartTime = 0.0;
static FVector GXboxSplitSmokeStartLocation(0,0,0);
static UBOOL GXboxSplitUseReadySlots = 0;
static INT   GXboxSystemLinkChildJoinSentMask = 0;
static INT   GXboxSystemLinkChildBoundMask = 0;

struct FXboxSplitReadySlot
{
    UBOOL Joined;
    UBOOL Locked;
    INT   Profile;
    INT   Character;
    INT   Team;
    INT   Focus;
};

static FXboxSplitReadySlot GXboxSplitReadySlots[4];
struct FXboxRuntimeProfileControls
{
    UBOOL Valid;
    INT Profile;
    FLOAT LookSensitivity;
    FLOAT MoveSensitivity;
    FLOAT DeadZone;
    UBOOL InvertY;
    INT StickLayout;
    INT Actions[XCB_Count];
};
static FXboxRuntimeProfileControls GXboxSplitProfileControls[4];
static HANDLE GXboxSplitReadyControllerHandles[4] = { NULL, NULL, NULL, NULL };
static XINPUT_STATE GXboxSplitReadyControllerState[4];
static XINPUT_STATE GXboxSplitReadyPrevControllerState[4];
static UBOOL GXboxSplitReadyInitialized = 0;

static UBOOL XboxSetObjectPropertyText( UObject* Object, const TCHAR* PropertyName, const TCHAR* Value );
static UBOOL XboxSetClassDefaultPropertyText( const TCHAR* ClassName, const TCHAR* PropertyName, const TCHAR* Value );
static UBOOL XboxSetClassDefaultPropertyInt( const TCHAR* ClassName, const TCHAR* PropertyName, INT Value );
static void XboxMenuLoadDiscoveredLists();
static UBOOL XboxProfileActiveCreated();
static UBOOL XboxMenuGameUsesLives( const TCHAR* GameClassName );
static UBOOL XboxMenuGameUsesTeamScore( const TCHAR* GameClassName );
static UBOOL XboxMenuGameUsesObjectiveRules( const TCHAR* GameClassName );
static void XboxMenuApplyMatchRuleDefaults( const TCHAR* GameClassName, INT ScoreLimit, INT MinPlayers );
static void XboxMenuBuildMatchRuleOptions( const TCHAR* GameClassName, INT ScoreLimit, INT TimeLimit, INT MinPlayers, INT Skill, TCHAR* Out );
static void XboxMenuQueuePendingMatchRules( const TCHAR* GameClassName, INT ScoreLimit, INT TimeLimit, INT MinPlayers, INT Skill, INT DesiredBots );
static void XboxMenuApplyPendingMatchRules( UXboxViewport* Viewport );
static void XboxMenuActivate( UXboxViewport* Viewport );
static void XboxMenuAdjustInstantAction( INT Delta );
static void XboxProfileOpen( UXboxViewport* Viewport, UBOOL bPersistGlobal=1 );
static INT XboxMenuWeaponHandIndex( APlayerPawn* Player );
static const TCHAR* XboxControlPresetLabel( UXboxClient* Client );
static const TCHAR* XboxInstantRulesProofRequestedPrefix();
static INT XboxSplitReadyJoinedCount();
static UBOOL XboxSplitReadyCanBegin();
static void XboxSplitReadyEnsure();
static void XboxSplitReadyReset( UXboxViewport* Viewport, UBOOL bPersistProfile=1 );
static UBOOL XboxSplitEnsureProfileForJoin( UXboxViewport* Viewport, INT Port, UBOOL bCreateIfMissing=0 );
static const FXboxPlayerClassOption& XboxSplitReadyPlayerClass( INT Port );
static void XboxSplitBuildPlayerURLForSlot( INT Port, TCHAR* Out, INT OutCount, UBOOL bForceDummy );
static void XboxProfileApplyPlayerOptionsForPort( APlayerPawn* Player, INT Port );
static void XboxMenuClose( UXboxViewport* Viewport );
static UBOOL XboxSplitControlsProofApply( UXboxViewport* Viewport, XINPUT_GAMEPAD& Pad );
static void XboxSplitControlsProofObserve( UXboxViewport* Viewport, const XINPUT_GAMEPAD& Pad );
static void XboxMenuReturnToFrontend( UXboxViewport* Viewport );
static void XboxTournamentClampSelection( UXboxViewport* Viewport );
static INT XboxTournamentMatchCount( INT LadderIndex );
static INT XboxTournamentSavedPosition( INT LadderIndex, INT DefaultPosition );
static INT XboxTournamentAvailableMatch( UXboxViewport* Viewport, INT LadderIndex );
static UBOOL XboxTournamentStringAt( INT LadderIndex, const TCHAR* PropertyName, INT MatchIndex, FString& OutValue );
static FString XboxTournamentFullMap( INT LadderIndex, INT MatchIndex );
static UBOOL XboxTournamentEnsureInventory( UXboxViewport* Viewport );
static void XboxTournamentProgressProofPrepareWin( UObject* Game, APlayerPawn* Player, FLOAT HighestBotScore, INT FragLimit );
static const TCHAR* XboxMenuScreenName( EXboxMenuScreen Screen );
static INT XboxMenuGameTypeCount();
static INT XboxInstantMapList( INT GameType );
static const FXboxDiscoveredOption& XboxMenuGameType( INT Index );
static const FXboxDiscoveredOption& XboxMenuMap( INT GameType, INT Index );
static void XboxSystemLinkSmokeLogGameplayStatus( UClient* InClient, ULevel* CurrentLevel, UBOOL bForce );

struct FXboxSystemLinkPeer
{
    DWORD Id;
    DWORD Address;
    DWORD SecureAddress;
    INT Port;
    DWORD HostId;
    INT Role;
    INT Phase;
    INT ReadyMask;
    INT LockedMask;
    INT Confirmed;
    INT GameType;
    INT MapIndex;
    INT FragLimitIndex;
    INT TimeLimitIndex;
    INT SkillIndex;
    DWORD LaunchId;
    DWORD LaunchAckId;
    UBOOL HasSecureInfo;
    UBOOL HasSecureAddress;
    UBOOL VerifiedSecurePeer;
    XNADDR XnAddr;
    XNKID SessionKeyId;
    XNKEY SessionKey;
    FLOAT FirstSeen;
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
    DWORD HostId;
    INT Role;
    INT Phase;
    INT ReadyConfirmed;
    INT LastReadyMask;
    INT LastLockedMask;
    INT GameType;
    INT MapIndex;
    INT FragLimitIndex;
    INT TimeLimitIndex;
    INT SkillIndex;
    DWORD LaunchId;
    DWORD LaunchAckId;
    DWORD LastSeenLaunchId;
    UBOOL PendingTravel;
    FLOAT PendingTravelTime;
    FLOAT PendingTravelDeadline;
    DWORD PendingHostAddress;
    DWORD SecureHostAddress;
    UBOOL HasSecureHostAddress;
    UBOOL HasLocalXnAddr;
    DWORD LocalXnAddrStatus;
    XNADDR LocalXnAddr;
    UBOOL SessionRegistered;
    UBOOL SessionKeyOwnedBySystemLink;
    UBOOL SessionIsHost;
    DWORD SessionHostId;
    XNKID SessionKeyId;
    XNKEY SessionKey;
    INT LastLoggedRole;
    DWORD LastLoggedHostId;
    FLOAT EnterTime;
    FLOAT LastSendTime;
    FLOAT LastLogTime;
    FLOAT LastSecureConnectLogTime;
    INT LastError;
    TArray<FXboxSystemLinkPeer> Peers;
};

static FXboxSystemLinkProbe GXboxSystemLink;
static UBOOL GXboxSystemLinkStateInitialized = 0;
static UBOOL GXboxSystemLinkSmokeHostPreTravelHoldDone = 0;
static INT GXboxSystemLinkProbeSendLogBudget = 0;
static INT GXboxSystemLinkProbePeerLogBudget = 0;

static void XboxSystemLinkEnsureState()
{
    if( GXboxSystemLinkStateInitialized )
        return;
    GXboxSystemLink.Socket = INVALID_SOCKET;
    GXboxSystemLink.Role = XSLR_Seeking;
    GXboxSystemLink.Phase = XSLP_Discovery;
    GXboxSystemLink.LastLoggedRole = -1;
    GXboxSystemLinkStateInitialized = 1;
}

static void XboxSystemLinkFormatAddress( DWORD Address, TCHAR* Out, INT OutCount )
{
    BYTE* B = (BYTE*)&Address;
    appSprintf( Out, TEXT("%u.%u.%u.%u"), B[0], B[1], B[2], B[3] );
    Out[OutCount-1] = 0;
}

static UBOOL XboxSystemLinkIsUsableIPv4( DWORD Address )
{
    if( Address == 0 || Address == INADDR_NONE || Address == INADDR_ANY || Address == INADDR_BROADCAST )
        return 0;

    // xemu's pcap backend reports raw broadcast frames through Xbox Winsock
    // with 0.0.0.1 as their source.  It is useful for receiving discovery
    // state but cannot be a unicast destination or an XNet virtual address.
    if( Address == inet_addr("0.0.0.1") )
        return 0;

    BYTE* B = (BYTE*)&Address;
    // XNetXnAddrToInAddr returns a local-only virtual IN_ADDR with the
    // 0.x.y.z shape. That is valid for Xbox Winsock travel even though it is
    // not a routable LAN address.
    return B[0] != 127 && B[0] < 224;
}

static UBOOL XboxSystemLinkIsXNetVirtualAddress( DWORD Address )
{
    if( !XboxSystemLinkIsUsableIPv4(Address) )
        return 0;

    BYTE* B = (BYTE*)&Address;
    return B[0] == 0;
}

static DWORD XboxSystemLinkSelectClientTravelAddress( const FXboxSystemLinkPeer* HostPeer, DWORD SecureAddress, UBOOL* OutUsingSecure )
{
    if( OutUsingSecure )
        *OutUsingSecure = 0;

    // XDK System Link samples register the host key, translate the host XNADDR,
    // and connect gameplay sockets to that translated address. The broadcast
    // sender IP is only suitable as a fallback for diagnostics/non-secure paths.
    if( XboxSystemLinkIsXNetVirtualAddress( SecureAddress ) )
    {
        if( OutUsingSecure )
            *OutUsingSecure = 1;
        return SecureAddress;
    }

    return HostPeer ? HostPeer->Address : SecureAddress;
}

static UBOOL XboxSystemLinkSecureAddressConnected( DWORD Address, const char* Reason, FLOAT Now )
{
    if( !XboxSystemLinkIsXNetVirtualAddress(Address) )
        return 0;

    IN_ADDR SecureAddr;
    SecureAddr.s_addr = Address;
    DWORD Status = XNetGetConnectStatus( SecureAddr );
    if( Status == XNET_CONNECT_STATUS_CONNECTED )
        return 1;

    INT ConnectResult = 0;
    if( Status == XNET_CONNECT_STATUS_IDLE || Status == XNET_CONNECT_STATUS_LOST )
    {
        ConnectResult = XNetConnect( SecureAddr );
        if( ConnectResult != 0 )
            GXboxSystemLink.LastError = ConnectResult;
    }

    if( Now - GXboxSystemLink.LastSecureConnectLogTime >= 1.0f )
    {
        GXboxSystemLink.LastSecureConnectLogTime = Now;
        GXboxLog.Write( "XSL secure connect wait reason=%s addr=0x%08X status=%lu connect=%d",
            Reason ? Reason : "unknown",
            Address,
            Status,
            ConnectResult );
    }
    return 0;
}

static char XboxSystemLinkHexDigit( INT Value )
{
    Value &= 15;
    return (char)(Value < 10 ? ('0' + Value) : ('A' + Value - 10));
}

static INT XboxSystemLinkHexValue( char Ch )
{
    if( Ch >= '0' && Ch <= '9' )
        return Ch - '0';
    if( Ch >= 'A' && Ch <= 'F' )
        return Ch - 'A' + 10;
    if( Ch >= 'a' && Ch <= 'f' )
        return Ch - 'a' + 10;
    return -1;
}

static void XboxSystemLinkHexEncode( const BYTE* Data, INT Count, char* Out, INT OutCount )
{
    if( !Out || OutCount <= 0 )
        return;

    INT Pos = 0;
    for( INT i=0; i<Count && Pos+2<OutCount; i++ )
    {
        Out[Pos++] = XboxSystemLinkHexDigit( Data[i] >> 4 );
        Out[Pos++] = XboxSystemLinkHexDigit( Data[i] );
    }
    Out[Pos] = 0;
}

static void XboxSystemLinkHexEncodeZeroes( INT Count, char* Out, INT OutCount )
{
    if( !Out || OutCount <= 0 )
        return;

    INT Pos = 0;
    for( INT i=0; i<Count && Pos+2<OutCount; i++ )
    {
        Out[Pos++] = '0';
        Out[Pos++] = '0';
    }
    Out[Pos] = 0;
}

static UBOOL XboxSystemLinkHexDecode( const char* In, BYTE* Out, INT Count )
{
    if( !In || !Out )
        return 0;

    for( INT i=0; i<Count; i++ )
    {
        INT Hi = XboxSystemLinkHexValue( In[i*2] );
        INT Lo = XboxSystemLinkHexValue( In[i*2+1] );
        if( Hi < 0 || Lo < 0 )
            return 0;
        Out[i] = (BYTE)((Hi << 4) | Lo);
    }
    return In[Count*2] == 0;
}

static UBOOL XboxSystemLinkSameKeyId( const XNKID* A, const XNKID* B )
{
    return A && B && appMemcmp( A, B, sizeof(XNKID) ) == 0;
}

static void XboxSystemLinkUnregisterSession( const char* Reason )
{
    XboxSystemLinkEnsureState();
    if( !GXboxSystemLink.SessionRegistered )
        return;

    INT Result = 0;
    if( GXboxSystemLink.SessionKeyOwnedBySystemLink )
        Result = XNetUnregisterKey( &GXboxSystemLink.SessionKeyId );
    GXboxLog.Write( "XSL session unregister reason=%s host=%d hostId=0x%08X owned=%d result=%d",
        Reason ? Reason : "unknown",
        GXboxSystemLink.SessionIsHost ? 1 : 0,
        GXboxSystemLink.SessionHostId,
        GXboxSystemLink.SessionKeyOwnedBySystemLink ? 1 : 0,
        Result );

    GXboxSystemLink.SessionRegistered = 0;
    GXboxSystemLink.SessionKeyOwnedBySystemLink = 0;
    GXboxSystemLink.SessionIsHost = 0;
    GXboxSystemLink.SessionHostId = 0;
    GXboxSystemLink.HasSecureHostAddress = 0;
    GXboxSystemLink.SecureHostAddress = 0;
    appMemzero( &GXboxSystemLink.SessionKeyId, sizeof(GXboxSystemLink.SessionKeyId) );
    appMemzero( &GXboxSystemLink.SessionKey, sizeof(GXboxSystemLink.SessionKey) );
}

static UBOOL XboxSystemLinkUpdateLocalXnAddr()
{
    XboxSystemLinkEnsureState();
    if( !GXboxSystemLink.SocketsReady )
        return 0;

    DWORD Status = XNetGetTitleXnAddr( &GXboxSystemLink.LocalXnAddr );
    GXboxSystemLink.LocalXnAddrStatus = Status;
    GXboxSystemLink.HasLocalXnAddr =
        Status != XNET_GET_XNADDR_PENDING
    &&  (Status & XNET_GET_XNADDR_NONE) == 0
    &&  (Status & XNET_GET_XNADDR_TROUBLESHOOT) == 0;
    return GXboxSystemLink.HasLocalXnAddr;
}

static UBOOL XboxSystemLinkEnsureHostSession()
{
    XboxSystemLinkEnsureState();
    if( GXboxSystemLink.SessionRegistered && GXboxSystemLink.SessionIsHost )
        return 1;

    if( GXboxSystemLink.SessionRegistered )
        XboxSystemLinkUnregisterSession( "host role change" );

    INT KeyCreated = XNetCreateKey( &GXboxSystemLink.SessionKeyId, &GXboxSystemLink.SessionKey );
    INT KeyRegistered = (KeyCreated == 0)
        ? XNetRegisterKey( &GXboxSystemLink.SessionKeyId, &GXboxSystemLink.SessionKey )
        : KeyCreated;

    if( KeyCreated != 0 || KeyRegistered != 0 )
    {
        GXboxSystemLink.LastError = KeyRegistered ? KeyRegistered : KeyCreated;
        GXboxLog.Write( "XSL host session failed create=%d register=%d", KeyCreated, KeyRegistered );
        return 0;
    }

    GXboxSystemLink.SessionRegistered = 1;
    GXboxSystemLink.SessionKeyOwnedBySystemLink = 1;
    GXboxSystemLink.SessionIsHost = 1;
    GXboxSystemLink.SessionHostId = GXboxSystemLink.LocalId;
    GXboxLog.Write( "XSL host session registered id=0x%08X", GXboxSystemLink.LocalId );
    return 1;
}

static UBOOL XboxSystemLinkResolveHostSecureAddress( const FXboxSystemLinkPeer* HostPeer, DWORD* OutAddress )
{
    XboxSystemLinkEnsureState();
    if( OutAddress )
        *OutAddress = 0;
    if( !HostPeer || !HostPeer->HasSecureInfo )
        return 0;

    if( GXboxSystemLink.SessionRegistered
    && (!XboxSystemLinkSameKeyId( &GXboxSystemLink.SessionKeyId, &HostPeer->SessionKeyId )
    ||  GXboxSystemLink.SessionIsHost
    ||  GXboxSystemLink.SessionHostId != HostPeer->Id) )
    {
        XboxSystemLinkUnregisterSession( "client host change" );
    }

    if( !GXboxSystemLink.SessionRegistered )
    {
        INT Result = XNetRegisterKey( &HostPeer->SessionKeyId, &HostPeer->SessionKey );
        if( Result != 0 && Result != WSAEALREADY )
        {
            GXboxSystemLink.LastError = Result;
            GXboxLog.Write( "XSL client session register failed host=0x%08X result=%d", HostPeer->Id, Result );
            return 0;
        }

        GXboxSystemLink.SessionRegistered = 1;
        GXboxSystemLink.SessionKeyOwnedBySystemLink = (Result == 0) ? 1 : 0;
        GXboxSystemLink.SessionIsHost = 0;
        GXboxSystemLink.SessionHostId = HostPeer->Id;
        appMemcpy( &GXboxSystemLink.SessionKeyId, &HostPeer->SessionKeyId, sizeof(GXboxSystemLink.SessionKeyId) );
        appMemcpy( &GXboxSystemLink.SessionKey, &HostPeer->SessionKey, sizeof(GXboxSystemLink.SessionKey) );
        GXboxLog.Write( "XSL client session registered host=0x%08X result=%d", HostPeer->Id, Result );
    }

    IN_ADDR SecureAddr;
    appMemzero( &SecureAddr, sizeof(SecureAddr) );
    INT Result = XNetXnAddrToInAddr( &HostPeer->XnAddr, &HostPeer->SessionKeyId, &SecureAddr );
    if( Result != 0 )
    {
        GXboxSystemLink.LastError = Result;
        GXboxLog.Write( "XSL host address translation failed host=0x%08X result=%d", HostPeer->Id, Result );
        return 0;
    }

    UBOOL bNewSecureAddress = !GXboxSystemLink.HasSecureHostAddress || GXboxSystemLink.SecureHostAddress != SecureAddr.s_addr;
    DWORD ConnectStatus = XNetGetConnectStatus( SecureAddr );
    if( bNewSecureAddress || ConnectStatus == XNET_CONNECT_STATUS_IDLE || ConnectStatus == XNET_CONNECT_STATUS_LOST )
    {
        INT ConnectResult = XNetConnect( SecureAddr );
        if( ConnectResult != 0 )
            GXboxSystemLink.LastError = ConnectResult;
        GXboxLog.Write( "XSL host address translated host=0x%08X secureAddr=0x%08X status=%lu connect=%d",
            HostPeer->Id, SecureAddr.s_addr, ConnectStatus, ConnectResult );
    }

    GXboxSystemLink.SecureHostAddress = SecureAddr.s_addr;
    GXboxSystemLink.HasSecureHostAddress = 1;
    if( OutAddress )
        *OutAddress = SecureAddr.s_addr;
    return 1;
}

static void XboxSystemLinkVerifySecurePeer( FXboxSystemLinkPeer& Peer )
{
    if( GXboxSystemLink.Role != XSLR_Host || Peer.VerifiedSecurePeer )
        return;
    if( !XboxSystemLinkIsXNetVirtualAddress(Peer.Address) )
        return;

    IN_ADDR PeerAddr;
    PeerAddr.s_addr = Peer.Address;
    XNADDR XnAddr;
    appMemzero( &XnAddr, sizeof(XnAddr) );
    INT Result = XNetInAddrToXnAddr( PeerAddr, &XnAddr, NULL );
    if( Result == 0 )
    {
        Peer.VerifiedSecurePeer = 1;
        Peer.HasSecureAddress = 1;
        Peer.SecureAddress = Peer.Address;
        GXboxLog.Write( "XSL secure peer verified id=0x%08X addr=0x%08X", Peer.Id, Peer.Address );
    }
}

static const TCHAR* XboxSystemLinkRoleText( INT Role )
{
    switch( Role )
    {
        case XSLR_Host:   return TEXT("HOST");
        case XSLR_Client: return TEXT("JOINED");
        default:          return TEXT("SEARCHING");
    }
}

static const TCHAR* XboxSystemLinkPhaseText( INT Phase )
{
    switch( Phase )
    {
        case XSLP_Ready:          return TEXT("READY SELECT");
        case XSLP_ReadyConfirmed: return TEXT("CONFIRMED");
        case XSLP_MapSelect:      return TEXT("MAP SELECT");
        case XSLP_Launching:      return TEXT("LAUNCHING");
        default:                  return TEXT("DISCOVERY");
    }
}

static UBOOL XboxSystemLinkMenuActive()
{
    return GXboxMenu.Screen == XMS_SystemLink || GXboxMenu.Screen == XMS_SystemLinkMapSelect;
}

static UBOOL XboxSystemLinkLocalReadyCanConfirm()
{
    return XboxSplitReadyCanBegin();
}

static INT XboxSystemLinkReadyMask()
{
    XboxSplitReadyEnsure();
    INT Mask = 0;
    for( INT i=0; i<4; i++ )
        if( GXboxSplitReadySlots[i].Joined )
            Mask |= (1 << i);
    return Mask;
}

static INT XboxSystemLinkLockedMask()
{
    XboxSplitReadyEnsure();
    INT Mask = 0;
    for( INT i=0; i<4; i++ )
        if( GXboxSplitReadySlots[i].Joined && GXboxSplitReadySlots[i].Locked )
            Mask |= (1 << i);
    return Mask;
}

static UBOOL XboxSystemLinkPeerInGroup( const FXboxSystemLinkPeer& Peer )
{
    if( !GXboxSystemLink.HostId )
        return 0;
    return Peer.Id == GXboxSystemLink.HostId || Peer.HostId == GXboxSystemLink.HostId;
}

static INT XboxSystemLinkGroupMachineCount()
{
    if( !GXboxSystemLink.HostId )
        return 0;

    INT Count = 1;
    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
        if( XboxSystemLinkPeerInGroup(GXboxSystemLink.Peers(i)) )
            Count++;
    return Count;
}

static INT XboxSystemLinkConfirmedMachineCount()
{
    if( !GXboxSystemLink.HostId )
        return 0;

    INT Count = GXboxSystemLink.ReadyConfirmed ? 1 : 0;
    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( XboxSystemLinkPeerInGroup(Peer) && Peer.Confirmed && Peer.ReadyMask && ((Peer.ReadyMask & Peer.LockedMask) == Peer.ReadyMask) )
            Count++;
    }
    return Count;
}

static UBOOL XboxSystemLinkAllMachinesConfirmed()
{
    INT Machines = XboxSystemLinkGroupMachineCount();
    if( Machines < 2 )
        return 0;
    if( !GXboxSystemLink.ReadyConfirmed || !XboxSystemLinkLocalReadyCanConfirm() )
        return 0;
    return XboxSystemLinkConfirmedMachineCount() == Machines;
}

static void XboxSystemLinkMarkLocalReadyChanged()
{
    if( GXboxMenu.Screen != XMS_SystemLink && GXboxMenu.Screen != XMS_SystemLinkMapSelect )
        return;
    GXboxSystemLink.ReadyConfirmed = 0;
    GXboxSystemLink.Phase = GXboxSystemLink.HostId ? XSLP_Ready : XSLP_Discovery;
    GXboxLog.Write( "XSL local ready changed readyMask=0x%X lockedMask=0x%X",
        XboxSystemLinkReadyMask(), XboxSystemLinkLockedMask() );
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
                if( VP->Actor && VP->Actor->GetLevel() && !GXboxSplitBorrowedActor[i] )
                {
                    GXboxLog.Write( "XSPLIT destroying dummy actor index=%d actor=0x%08X reason=%s",
                        i, (DWORD)VP->Actor, Reason ? Reason : "" );
                    VP->Actor->GetLevel()->DestroyActor( VP->Actor, 1 );
                    VP->Actor = NULL;
                }
                else if( GXboxSplitBorrowedActor[i] )
                {
                    GXboxLog.Write( "XSPLIT releasing borrowed actor viewport index=%d actor=0x%08X reason=%s",
                        i, (DWORD)VP->Actor, Reason ? Reason : "" );
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
    GXboxSplitUseReadySlots = 0;
    GXboxSplitActiveMask = 1;
    GXboxSplitActivePlayerCount = 1;
    GXboxSplitRenderViewport = 0;
    GXboxSplitRenderViewportCount = 1;
    for( INT i=0; i<4; i++ )
    {
        GXboxSplitDummyDeathTime[i] = -1.0f;
        GXboxSplitBorrowedActor[i] = 0;
    }
    GXboxSystemLinkChildJoinSentMask = 0;
    GXboxSystemLinkChildBoundMask = 0;

    GXboxLog.Write( "XSPLIT reset reason=%s remainingViewports=%d",
        Reason ? Reason : "",
        Client ? Client->Viewports.Num() : -1 );
}

static UBOOL XboxSmokeMarkerExists( const char* MarkerName, INT& CachedResult )
{
    if( CachedResult > 0 )
        return 1;
    // Proof markers are staged before title startup. Cache absence as well as
    // presence: otherwise every disabled proof probes two filesystem paths on
    // every poll, multiplied by the number of local players.
    if( CachedResult == -2 )
        return 0;

    char DPath[128];
    appSprintf( DPath, "D:\\%s", MarkerName );
    if( GetFileAttributesA( DPath ) != 0xFFFFFFFF )
    {
        CachedResult = 1;
        GXboxLog.Write( "XSMOKE marker %s found path=%s", MarkerName, DPath );
        return 1;
    }

    if( GetFileAttributesA( MarkerName ) != 0xFFFFFFFF )
    {
        CachedResult = 1;
        GXboxLog.Write( "XSMOKE marker %s found relativePath=%s", MarkerName, MarkerName );
        return 1;
    }

    if( CachedResult == -1 )
        GXboxLog.Write( "XSMOKE marker %s missing checked=%s,%s", MarkerName, DPath, MarkerName );
    CachedResult = -2;
    return 0;
}

static UBOOL XboxSplitControlsProofEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSplitControlsProofSmoke.ini", Cached );
}

static UBOOL XboxSplitBenchmarkEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSplitBenchmark.ini", Cached );
}

static UBOOL XboxSplitCombatBenchmarkEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSplitCombatBenchmark.ini", Cached );
}

static UBOOL XboxSplitControlsOnlineProofEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSplitControlsOnlineProofSmoke.ini", Cached );
}

static UBOOL XboxSplitSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSplitSmoke.ini", Cached )
        || XboxSplitControlsProofEnabled()
        || XboxSplitControlsOnlineProofEnabled();
}

static INT XboxSplitLayoutProofMask()
{
    static const char* MarkerNames[] =
    {
        "XboxSplitLayout03.ini",
        "XboxSplitLayout05.ini",
        "XboxSplitLayout09.ini",
        "XboxSplitLayout07.ini",
        "XboxSplitLayout0B.ini",
        "XboxSplitLayout0D.ini"
    };
    static const INT MarkerMasks[] = { 0x03, 0x05, 0x09, 0x07, 0x0B, 0x0D };
    static INT Cached[ARRAY_COUNT(MarkerNames)] = { -1, -1, -1, -1, -1, -1 };

    for( INT i=0; i<ARRAY_COUNT(MarkerNames); i++ )
        if( XboxSmokeMarkerExists(MarkerNames[i], Cached[i]) )
            return MarkerMasks[i];
    return 0;
}

static UBOOL XboxSoakSmokeEnabled();

static UBOOL XboxSplitSmokeInputProofEnabled()
{
    if( XboxSplitLayoutProofMask() )
        return 0;

    if( XboxSplitSmokeEnabled() )
        return 1;

    if( XboxSoakSmokeEnabled() )
        return 1;

    // The paired System Link stress run does not use the standalone split-screen
    // smoke marker, but it still needs the same per-viewport input proof.
    static INT SystemLinkCached = -1;
    static INT FourPlayerCached = -1;
    return XboxSmokeMarkerExists( "XboxSystemLinkSmoke.ini", SystemLinkCached )
        && XboxSmokeMarkerExists( "XboxSystemLink4PStress.ini", FourPlayerCached );
}

static UBOOL XboxMenuSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxMenuSmoke.ini", Cached );
}

static UBOOL XboxMenuSmokeJailbreakEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxMenuSmokeJailbreak.ini", Cached );
}

static UBOOL XboxTournamentSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxTournamentSmoke.ini", Cached );
}

static UBOOL XboxTournamentProgressWinSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxTournamentProgressWin.ini", Cached );
}

static INT XboxTournamentProofRequestedLadder()
{
    static INT DomCached = -1;
    static INT CtfCached = -1;
    static INT AssaultCached = -1;
    static INT ChallengeCached = -1;
    if( XboxSmokeMarkerExists( "XboxTournamentProofDOM.ini", DomCached ) )
        return 1;
    if( XboxSmokeMarkerExists( "XboxTournamentProofCTF.ini", CtfCached ) )
        return 2;
    if( XboxSmokeMarkerExists( "XboxTournamentProofAS.ini", AssaultCached ) )
        return 3;
    if( XboxSmokeMarkerExists( "XboxTournamentProofCHAL.ini", ChallengeCached ) )
        return 4;
    return 0;
}

static UBOOL XboxControlsProofSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxControlsProofSmoke.ini", Cached );
}

static UBOOL XboxSettingsProofSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSettingsProofSmoke.ini", Cached );
}

static UBOOL XboxAudioSettingsProofSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxAudioSettingsProofSmoke.ini", Cached );
}

static UBOOL XboxVideoSettingsProofSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxVideoSettingsProofSmoke.ini", Cached );
}

static UBOOL XboxMainMenuProofSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxMainMenuProofSmoke.ini", Cached );
}

enum EXboxFullMenuProofRequest
{
    XFMP_None = 0,
    XFMP_Main,
    XFMP_Pause,
    XFMP_PauseConfirm,
    XFMP_InstantAction,
    XFMP_Mutators,
    XFMP_Tournament,
    XFMP_TournamentResult,
    XFMP_SystemLinkDiscovery,
    XFMP_SystemLinkReady,
    XFMP_SystemLinkHostMap,
    XFMP_SystemLinkClientMap,
    XFMP_SplitReady,
    XFMP_SplitMap,
    XFMP_ProfileSelect,
    XFMP_ProfileSwitch,
    XFMP_PlayerSetup,
    XFMP_ProfileName,
    XFMP_ControlsTop,
    XFMP_ControlsButtons,
    XFMP_Settings,
    XFMP_Audio,
    XFMP_Video,
    XFMP_ComingSoon
};

static INT XboxFullMenuProofRequested()
{
    static INT Cached = -2;
    if( Cached != -2 )
        return Cached;

    static const char* MarkerNames[] =
    {
        "",
        "XboxProofMain.ini",
        "XboxProofPause.ini",
        "XboxProofPauseConfirm.ini",
        "XboxProofInstantAction.ini",
        "XboxProofMutators.ini",
        "XboxProofTournament.ini",
        "XboxProofTournamentResult.ini",
        "XboxProofSystemLinkDiscovery.ini",
        "XboxProofSystemLinkReady.ini",
        "XboxProofSystemLinkHostMap.ini",
        "XboxProofSystemLinkClientMap.ini",
        "XboxProofSplitReady.ini",
        "XboxProofSplitMap.ini",
        "XboxProofProfileSelect.ini",
        "XboxProofProfileSwitch.ini",
        "XboxProofPlayerSetup.ini",
        "XboxProofProfileName.ini",
        "XboxProofControlsTop.ini",
        "XboxProofControlsButtons.ini",
        "XboxProofSettings.ini",
        "XboxProofAudio.ini",
        "XboxProofVideo.ini",
        "XboxProofComingSoon.ini"
    };

    for( INT i=1; i<ARRAY_COUNT(MarkerNames); i++ )
    {
        char DPath[128];
        appSprintf( DPath, "D:\\%s", MarkerNames[i] );
        if( GetFileAttributesA(DPath) != 0xFFFFFFFF || GetFileAttributesA(MarkerNames[i]) != 0xFFFFFFFF )
        {
            Cached = i;
            GXboxLog.Write( "XMENU FULL PROOF request=%d marker=%s", i, MarkerNames[i] );
            return Cached;
        }
    }

    Cached = XFMP_None;
    return Cached;
}

static UBOOL XboxIsSystemLinkProofRequest( INT Request )
{
    return Request == XFMP_SystemLinkDiscovery
        || Request == XFMP_SystemLinkReady
        || Request == XFMP_SystemLinkHostMap
        || Request == XFMP_SystemLinkClientMap;
}

static UBOOL XboxFullMenuProofExtraMarkerExists( const char* MarkerName )
{
    if( !MarkerName || !MarkerName[0] )
        return 0;

    char DPath[128];
    appSprintf( DPath, "D:\\%s", MarkerName );
    return GetFileAttributesA(DPath) != 0xFFFFFFFF
        || GetFileAttributesA(MarkerName) != 0xFFFFFFFF;
}

static INT XboxFullMenuProofFindGameType( const TCHAR* ClassName )
{
    const INT Count = XboxMenuGameTypeCount();
    for( INT i=0; i<Count; i++ )
        if( appStricmp(*XboxMenuGameType(i).URLValue, ClassName) == 0 )
            return i;
    return INDEX_NONE;
}

static UBOOL XboxSafeAreaProofSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSafeAreaProofSmoke.ini", Cached );
}

static UBOOL XboxInstantRulesProofSmokeEnabled()
{
    return XboxInstantRulesProofRequestedPrefix() != NULL;
}

static UBOOL XboxSoakSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSoakSmoke.ini", Cached );
}

static UBOOL XboxIssueMapSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxIssueMapSmoke.ini", Cached );
}

static UBOOL XboxFlickerTraversalProofEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxFlickerTraversal.ini", Cached );
}

static UBOOL XboxSystemLinkSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSystemLinkSmoke.ini", Cached );
}

static UBOOL XboxSystemLinkLifecycleSmokeEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSystemLinkLifecycle.ini", Cached );
}

static UBOOL XboxSystemLinkLongSoakEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSystemLinkLongSoak.ini", Cached );
}

static UBOOL XboxSystemLinkFourPlayerStressEnabled()
{
    static INT Cached = -1;
    return XboxSmokeMarkerExists( "XboxSystemLink4PStress.ini", Cached );
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

    FString TravelURL = XboxSplitControlsOnlineProofEnabled()
        ? TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=0?MaxPlayers=16?Listen?LAN?Difficulty=1?Name=SmokeP1?Class=Botpack.TMale2?team=0?skin=SoldierSkins.blkt?Face=SoldierSkins.Othello?Voice=BotPack.VoiceMaleTwo")
        : TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=0?MaxPlayers=4?Difficulty=1?Name=SmokeP1?Class=Botpack.TMale2?team=0?skin=SoldierSkins.blkt?Face=SoldierSkins.Othello?Voice=BotPack.VoiceMaleTwo");

    INT LayoutProofMask = XboxSplitLayoutProofMask();
    if( LayoutProofMask || XboxSplitControlsProofEnabled() || XboxSplitControlsOnlineProofEnabled() )
    {
        UXboxViewport* PrimaryViewport = Cast<UXboxViewport>( Client->Viewports(0) );
        XboxSplitReadyReset( PrimaryViewport );
        for( INT Port=0; Port<4; Port++ )
        {
            UBOOL bRequested = LayoutProofMask ? ((LayoutProofMask & (1 << Port)) != 0) : 1;
            if( bRequested && XboxSplitEnsureProfileForJoin(PrimaryViewport, Port, 1) )
            {
                GXboxSplitReadySlots[Port].Joined = 1;
                GXboxSplitReadySlots[Port].Locked = 1;
                GXboxSplitReadySlots[Port].Focus = 0;
            }
        }
        GXboxSplitUseReadySlots = 1;
        TCHAR PlayerURL[512];
        XboxSplitBuildPlayerURLForSlot( 0, PlayerURL, ARRAY_COUNT(PlayerURL), 0 );
        TravelURL = FString::Printf
        (
            XboxSplitControlsOnlineProofEnabled()
                ? TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=0?MaxPlayers=16?Listen?LAN?Difficulty=1%s")
                : TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=0?MaxPlayers=4?Difficulty=1%s"),
            PlayerURL
        );
        GXboxLog.Write( "XSPLIT proof assigned requestedMask=0x%X readyMask=0x%X lockedMask=0x%X",
            LayoutProofMask, XboxSystemLinkReadyMask(), XboxSystemLinkLockedMask() );

    }

    GXboxLog.Write( "XSPLIT SELFTEST queued travel: %s", TCHAR_TO_ANSI(*TravelURL) );
    Client->Engine->SetClientTravel( Client->Viewports(0), const_cast<TCHAR*>(*TravelURL), 0, TRAVEL_Absolute );
}

static INT XboxSplitCountActiveBits( INT Mask )
{
    INT Count = 0;
    for( INT i=0; i<4; i++ )
        if( Mask & (1 << i) )
            Count++;
    return Count;
}

static INT XboxSplitFirstActiveSlot()
{
    for( INT i=0; i<4; i++ )
        if( GXboxSplitActiveMask & (1 << i) )
            return i;
    return 0;
}

static INT XboxSplitCurrentActiveMask()
{
    INT LayoutProofMask = XboxSplitLayoutProofMask();
    if( LayoutProofMask )
        return LayoutProofMask;

    if( XboxSplitSmokeInputProofEnabled() )
        return 0x0F;

    INT Mask = 0;
    if( GXboxSplitUseReadySlots )
    {
        XboxSplitReadyEnsure();
        for( INT i=0; i<4; i++ )
            if( GXboxSplitReadySlots[i].Joined )
                Mask |= (1 << i);
    }
    else
    {
        DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
        Mask = 1;
        for( INT i=1; i<4; i++ )
            if( DeviceMask & (1 << i) )
                Mask |= (1 << i);
    }

    if( !Mask )
        Mask = 1;
    return Mask & 0x0F;
}

static INT XboxSplitActiveOrderForSlot( INT Slot )
{
    INT Order = 0;
    for( INT i=0; i<Slot && i<4; i++ )
        if( GXboxSplitActiveMask & (1 << i) )
            Order++;
    return Order;
}

static void XboxSplitSetDisabledViewRegion( UXboxViewport* VP )
{
    if( !VP )
        return;

    VP->ViewX = XBOX_SCREEN_WIDTH / 2;
    VP->ViewY = XBOX_SCREEN_HEIGHT / 2;
    VP->SizeX = VP->ViewWidth = XBOX_SCREEN_WIDTH / 2;
    VP->SizeY = VP->ViewHeight = XBOX_SCREEN_HEIGHT / 2;
}

static void XboxSplitApplyActiveViewRegion( UXboxViewport* VP, INT Slot )
{
    if( !VP )
        return;

    INT Count = Clamp<INT>( GXboxSplitActivePlayerCount, 1, 4 );
    INT Order = Clamp<INT>( XboxSplitActiveOrderForSlot( Slot ), 0, Count - 1 );
    const INT HalfW = XBOX_SCREEN_WIDTH / 2;
    const INT HalfH = XBOX_SCREEN_HEIGHT / 2;

    if( Count <= 1 )
    {
        VP->ViewX = 0;
        VP->ViewY = 0;
        VP->SizeX = VP->ViewWidth = XBOX_SCREEN_WIDTH;
        VP->SizeY = VP->ViewHeight = XBOX_SCREEN_HEIGHT;
    }
    else if( Count == 2 )
    {
        VP->ViewX = 0;
        VP->ViewY = Order ? HalfH : 0;
        VP->SizeX = VP->ViewWidth = XBOX_SCREEN_WIDTH;
        VP->SizeY = VP->ViewHeight = HalfH;
    }
    else
    {
        VP->ViewX = (Order & 1) ? HalfW : 0;
        VP->ViewY = (Order & 2) ? HalfH : 0;
        VP->SizeX = VP->ViewWidth = HalfW;
        VP->SizeY = VP->ViewHeight = HalfH;
    }
}

extern "C" void XboxSplitBeginRenderFrame( INT ViewportCount )
{
    GXboxSplitRenderViewport = 0;
    GXboxSplitRenderViewportCount = GXboxSplitActive
        ? Max<INT>( GXboxSplitActivePlayerCount, 1 )
        : Max<INT>( ViewportCount, 1 );
}

extern "C" void XboxSplitSetRenderViewport( INT ViewportIndex )
{
    GXboxSplitRenderViewport = ViewportIndex;
}

extern "C" UBOOL XboxSplitShouldRenderViewport( UViewport* Viewport, INT ViewportIndex )
{
    if( !GXboxSplitActive )
        return 1;

    UXboxViewport* XboxViewport = Cast<UXboxViewport>( Viewport );
    if( !XboxViewport || XboxViewport->bXboxSplitDummy )
        return 0;

    INT PhysicalPort = Clamp<INT>( XboxViewport->ControllerPort, 0, 3 );
    return (GXboxSplitActiveMask & (1 << PhysicalPort)) ? 1 : 0;
}

extern "C" UBOOL XboxViewportShouldUpdateAudio( UViewport* Viewport )
{
    if( !GXboxSplitActive )
        return 1;

    UXboxViewport* XboxViewport = Cast<UXboxViewport>( Viewport );
    if( !XboxViewport || XboxViewport->bXboxSplitDummy )
        return 0;

    return XboxViewport->ControllerPort == XboxSplitFirstActiveSlot();
}

extern "C" void XboxSplitClearUnusedRenderRegions( UClient* Client )
{
    if( !GXboxSplitActive || GXboxSplitActivePlayerCount != 3 || !Client || Client->Viewports.Num() <= 0 )
        return;

    UXboxViewport* Primary = Cast<UXboxViewport>( Client->Viewports(0) );
    if( !Primary || !Primary->RenDev )
        return;

    XboxRenderClearRegion( Primary->RenDev, XBOX_SCREEN_WIDTH / 2, XBOX_SCREEN_HEIGHT / 2, XBOX_SCREEN_WIDTH / 2, XBOX_SCREEN_HEIGHT / 2 );
}

extern "C" UBOOL XboxSplitShouldClearRenderLock()
{
    return 1;
}

static void XboxViewportApplySafeArea( UXboxClient* Client, INT& X, INT& Y, INT& W, INT& H )
{
    if( !Client || W <= 1 || H <= 1 )
        return;

    INT SizePercent = Clamp<INT>( Client->SafeAreaSize, 85, 100 );
    INT InsetX = (W * (100 - SizePercent) + 100) / 200;
    INT InsetY = (H * (100 - SizePercent) + 100) / 200;
    INT OffsetX = Clamp<INT>( Client->SafeAreaX, -InsetX, InsetX );
    INT OffsetY = Clamp<INT>( Client->SafeAreaY, -InsetY, InsetY );

    X += InsetX + OffsetX;
    Y += InsetY + OffsetY;
    W -= InsetX * 2;
    H -= InsetY * 2;
}

extern "C" void XboxViewportApplyViewRegion( UViewport* Viewport, FSceneNode* Frame )
{
    UXboxViewport* XboxViewport = Cast<UXboxViewport>( Viewport );
    if( !XboxViewport || !Frame )
        return;

    INT X = XboxViewport->ViewX;
    INT Y = XboxViewport->ViewY;
    INT W = XboxViewport->ViewWidth;
    INT H = XboxViewport->ViewHeight;

    if( !GXboxSplitActive && !XboxViewport->bXboxSplitDummy )
    {
        UXboxClient* Client = Cast<UXboxClient>( XboxViewport->GetOuter() );
        if( Client )
            XboxViewportApplySafeArea( Client, X, Y, W, H );
    }

    Frame->XB = X;
    Frame->YB = Y;
    Frame->X  = Max<INT>( W, 1 );
    Frame->Y  = Max<INT>( H, 1 );
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

static UBOOL XboxViewportEnsureInputInitialized( UXboxViewport* Viewport, const char* Reason )
{
    if( !Viewport )
        return 0;

    if( !Viewport->Input )
    {
        GXboxLog.Write( "XSPLIT input init failed reason=%s viewport=0x%08X input=NULL",
            Reason ? Reason : "unknown", (DWORD)Viewport );
        return 0;
    }
    if( !Viewport->Actor )
    {
        GXboxLog.Write( "XSPLIT input init deferred reason=%s viewport=0x%08X actor=NULL",
            Reason ? Reason : "unknown", (DWORD)Viewport );
        return 0;
    }

    if( Viewport->Input->Viewport != Viewport )
    {
        Viewport->Input->Init( Viewport );
        GXboxLog.Write( "XSPLIT input initialized reason=%s viewport=0x%08X input=0x%08X",
            Reason ? Reason : "unknown", (DWORD)Viewport, (DWORD)Viewport->Input );
    }

    return 1;
}

static void XboxSplitSmokeFeedInput( UXboxViewport* Viewport )
{
    if( XboxSplitControlsProofEnabled() || XboxSplitControlsOnlineProofEnabled()
    ||  !GXboxSplitActive || !XboxSplitSmokeInputProofEnabled() || !Viewport || Viewport->bXboxSplitDummy || !Viewport->Actor )
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
    GXboxSplitActiveMask = XboxSplitCurrentActiveMask();
    GXboxSplitActivePlayerCount = Clamp<INT>( XboxSplitCountActiveBits( GXboxSplitActiveMask ), 1, 4 );
    GXboxSplitRenderViewportCount = GXboxSplitActivePlayerCount;

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
        VP->ColorBytes = Primary->ColorBytes ? Primary->ColorBytes : 4;
        XboxViewportEnsureInputInitialized( VP, "SplitConfigure" );

        UBOOL bActiveSlot = (GXboxSplitActiveMask & (1 << i)) ? 1 : 0;
        VP->bXboxSplitDummy = !bActiveSlot;
        // GameEngine::Init creates a console only for the original viewport.
        // ChallengeHUD reads PlayerOwner.Player.Console.bTyping every frame,
        // so each additional active player needs its own lightweight console.
        // UViewport::Serialize/Destroy already retain and release this object.
        if( bActiveSlot && !VP->Console )
            XboxEnsureConsoleClass( VP, TEXT("Engine.Console"), "SplitConfigure" );
        if( bActiveSlot )
            XboxSplitApplyActiveViewRegion( VP, i );
        else
            XboxSplitSetDisabledViewRegion( VP );

        INT RenderOrder = bActiveSlot ? XboxSplitActiveOrderForSlot( VP->ControllerPort ) : -1;
        INT ProfileIndex = GXboxSplitUseReadySlots ? GXboxSplitReadySlots[i].Profile : -1;
        GXboxLog.Write( "XSPLIT viewport=%d physical=P%d renderOrder=%d profile=%d devicePresent=%d joined=%d dummy=%d region=%d,%d %dx%d",
            i, VP->ControllerPort + 1, RenderOrder + 1, ProfileIndex + 1, (DeviceMask & (1 << i)) ? 1 : 0,
            GXboxSplitUseReadySlots ? (GXboxSplitReadySlots[i].Joined ? 1 : 0) : -1,
            VP->bXboxSplitDummy ? 1 : 0,
            VP->ViewX, VP->ViewY, VP->ViewWidth, VP->ViewHeight );
    }

    for( INT i=0; i<4; i++ )
        GXboxSplitDummyDeathTime[i] = -1.0f;
    GXboxLog.Write( "XSPLIT configured viewports=%d activeMask=0x%X activePlayers=%d",
        Client->Viewports.Num(), GXboxSplitActiveMask, GXboxSplitActivePlayerCount );
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

    UBOOL bNetworkClient = Level->GetLevelInfo()->NetMode == NM_Client;
    if( !Level->GetLevelInfo()->Game && !bNetworkClient )
        return 0;

    if( Level->URL.Map.Len() <= 0 )
        return 0;

    const TCHAR* MapName = *Level->URL.Map;
    if( appStricmp( MapName, TEXT("CityIntro") ) == 0
    ||  appStricmp( MapName, TEXT("CityIntro.unr") ) == 0
    ||  appStricmp( MapName, TEXT("UT-Logo-Map") ) == 0
    ||  appStricmp( MapName, TEXT("UT-Logo-Map.unr") ) == 0 )
    {
        return 0;
    }

    UClass* GameClass = Level->GetLevelInfo()->Game ? Level->GetLevelInfo()->Game->GetClass() : NULL;
    if( GameClass )
    {
        const TCHAR* GameName = GameClass->GetName();
        if( appStricmp( GameName, TEXT("GameInfo") ) == 0
        ||  appStricmp( GameName, TEXT("UTIntro") ) == 0
        ||  appStricmp( GameName, TEXT("LadderNewGame") ) == 0 )
        {
            return 0;
        }
    }

    return 1;
}

static UBOOL XboxSystemLinkActorIsViewportActor( UClient* Client, APlayerPawn* Actor )
{
    if( !Client || !Actor )
        return 0;

    for( INT i=0; i<Client->Viewports.Num(); i++ )
        if( Client->Viewports(i) && Client->Viewports(i)->Actor == Actor )
            return 1;
    return 0;
}

static APlayerPawn* XboxSystemLinkFindUnboundAutonomousChild( ULevel* Level, UClient* Client )
{
    if( !Level )
        return NULL;

    UViewport* PrimaryViewport = (Client && Client->Viewports.Num() > 0) ? Client->Viewports(0) : NULL;
    APlayerPawn* PrimaryActor = PrimaryViewport ? PrimaryViewport->Actor : NULL;
    for( INT i=0; i<Level->Actors.Num(); i++ )
    {
        APlayerPawn* Player = Cast<APlayerPawn>( Level->Actors(i) );
        if( !Player || Player == PrimaryActor || Player->bDeleteMe )
            continue;
        if( Player->Role != ROLE_AutonomousProxy )
            continue;
        if( Player->Player != NULL )
            continue;
        if( XboxSystemLinkActorIsViewportActor( Client, Player ) )
            continue;
        return Player;
    }
    return NULL;
}

static INT XboxSystemLinkClientChildRequiredMask()
{
    INT Mask = 0;
    for( INT Slot=1; Slot<4; Slot++ )
        if( GXboxSplitReadySlots[Slot].Joined )
            Mask |= (1 << Slot);
    return Mask;
}

static INT XboxSystemLinkCountUnboundAutonomousChildren( ULevel* Level, UClient* Client )
{
    if( !Level )
        return 0;

    UViewport* PrimaryViewport = (Client && Client->Viewports.Num() > 0) ? Client->Viewports(0) : NULL;
    APlayerPawn* PrimaryActor = PrimaryViewport ? PrimaryViewport->Actor : NULL;
    INT Count = 0;
    for( INT i=0; i<Level->Actors.Num(); i++ )
    {
        APlayerPawn* Player = Cast<APlayerPawn>( Level->Actors(i) );
        if( !Player || Player == PrimaryActor || Player->bDeleteMe )
            continue;
        if( Player->Role != ROLE_AutonomousProxy )
            continue;
        if( Player->Player != NULL )
            continue;
        if( XboxSystemLinkActorIsViewportActor( Client, Player ) )
            continue;
        Count++;
    }
    return Count;
}

static UBOOL XboxSystemLinkSendClientChildJoins( ULevel* Level )
{
    if( !Level || !Level->NetDriver || !Level->NetDriver->ServerConnection )
        return 0;

    UNetConnection* Connection = Level->NetDriver->ServerConnection;
    if( Connection->State != USOCK_Open )
    {
        GXboxLog.Write( "XSL child join deferred state=%d sentMask=0x%X",
            (INT)Connection->State, GXboxSystemLinkChildJoinSentMask );
        return 0;
    }

    for( INT Slot=1; Slot<4; Slot++ )
    {
        if( !GXboxSplitReadySlots[Slot].Joined )
            continue;
        if( GXboxSystemLinkChildJoinSentMask & (1 << Slot) )
            continue;

        TCHAR SlotURL[512];
        XboxSplitBuildPlayerURLForSlot( Slot, SlotURL, ARRAY_COUNT(SlotURL), 0 );
        Connection->Logf( TEXT("XSLJOIN SLOT=%i URL=%s"), Slot, SlotURL );
        Connection->FlushNet();
        GXboxSystemLinkChildJoinSentMask |= (1 << Slot);
        GXboxLog.Write( "XSL child join sent slot=%d url=%s sentMask=0x%X",
            Slot, TCHAR_TO_ANSI(SlotURL), GXboxSystemLinkChildJoinSentMask );
    }

    return 1;
}

static INT XboxSystemLinkBindClientChildActors( UXboxClient* Client, ULevel* Level )
{
    if( !Client || !Level || !Level->GetLevelInfo() || Level->GetLevelInfo()->NetMode != NM_Client )
        return 0;

    INT BoundThisCall = 0;
    for( INT Slot=1; Slot<Client->Viewports.Num() && Slot<4; Slot++ )
    {
        if( !GXboxSplitReadySlots[Slot].Joined )
            continue;
        if( GXboxSystemLinkChildBoundMask & (1 << Slot) )
            continue;

        UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(Slot) );
        if( !VP )
            continue;

        APlayerPawn* Child = XboxSystemLinkFindUnboundAutonomousChild( Level, Client );
        if( !Child )
            continue;

        Child->SetPlayer( VP );
        VP->bXboxSplitDummy = 0;
        GXboxSplitBorrowedActor[Slot] = 0;
        XboxViewportEnsureInputInitialized( VP, "SystemLinkChildBind" );
        XboxSplitPreparePlayer( Child, 0 );
        XboxProfileApplyPlayerOptionsForPort( Child, Slot );
        GXboxSystemLinkChildBoundMask |= (1 << Slot);
        BoundThisCall++;
        GXboxLog.Write( "XSL child bound slot=%d viewport=0x%08X actor=0x%08X class=%s pri=%s boundMask=0x%X",
            Slot,
            (DWORD)VP,
            (DWORD)Child,
            Child->GetClass() ? TCHAR_TO_ANSI(Child->GetClass()->GetName()) : "",
            Child->PlayerReplicationInfo ? TCHAR_TO_ANSI(*Child->PlayerReplicationInfo->PlayerName) : "",
            GXboxSystemLinkChildBoundMask );
    }
    return BoundThisCall;
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

    GXboxLog.Write( "XSPLIT activating after map load url=%s useReady=%d readyMask=0x%X lockedMask=0x%X stress4p=%d",
        TCHAR_TO_ANSI(*Level->URL.String()),
        GXboxSplitUseReadySlots ? 1 : 0,
        XboxSystemLinkReadyMask(),
        XboxSystemLinkLockedMask(),
        XboxSystemLinkFourPlayerStressEnabled() ? 1 : 0 );

    XboxSplitSuppressBots( Level );

    if( Level->GetLevelInfo() && Level->GetLevelInfo()->NetMode == NM_Client )
    {
        APlayerPawn* PrimaryActor = Primary ? Primary->Actor : NULL;
        if( !XboxSystemLinkSendClientChildJoins( Level ) )
            return;

        INT RequiredMask = XboxSystemLinkClientChildRequiredMask();
        INT RequiredChildren = 0;
        for( INT Slot=1; Slot<4; Slot++ )
            if( RequiredMask & (1 << Slot) )
                RequiredChildren++;

        INT AvailableChildren = XboxSystemLinkCountUnboundAutonomousChildren( Level, Client );
        if( AvailableChildren < RequiredChildren )
        {
            if( PrimaryActor )
                XboxSplitPreparePlayer( PrimaryActor, 0 );
            GXboxLog.Write( "XSPLIT net-client waiting for child actors available=%d required=%d sentMask=0x%X boundMask=0x%X",
                AvailableChildren,
                RequiredChildren,
                GXboxSystemLinkChildJoinSentMask,
                GXboxSystemLinkChildBoundMask );
            XboxSystemLinkSmokeLogGameplayStatus( Client, Level, 0 );
            return;
        }

        XboxSplitConfigureViewports( Client );
        GXboxSplitPending = 0;
        GXboxSplitActive = 1;
        if( PrimaryActor )
        {
            XboxSplitPreparePlayer( PrimaryActor, 0 );
            XboxProfileApplyPlayerOptionsForPort( PrimaryActor, 0 );
        }
        XboxSystemLinkBindClientChildActors( Client, Level );
        GXboxLog.Write( "XSPLIT active viewports=%d netClientChildren sentMask=0x%X boundMask=0x%X",
            Client->Viewports.Num(),
            GXboxSystemLinkChildJoinSentMask,
            GXboxSystemLinkChildBoundMask );
        XboxSystemLinkSmokeLogGameplayStatus( Client, Level, 1 );
        return;
    }

    XboxSplitConfigureViewports( Client );

    for( INT i=1; i<Client->Viewports.Num() && i<4; i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(i) );
        if( !VP )
            continue;
        if( VP->bXboxSplitDummy )
        {
            GXboxLog.Write( "XSPLIT skip inactive viewport=%d activeMask=0x%X", i, GXboxSplitActiveMask );
            continue;
        }

        FString Error;
        TCHAR SlotURL[512];
        if( GXboxSplitUseReadySlots )
            XboxSplitBuildPlayerURLForSlot( i, SlotURL, ARRAY_COUNT(SlotURL), !GXboxSplitReadySlots[i].Joined );
        else
            appSprintf( SlotURL, TEXT("?Name=Dummy%i?Team=%i"), i + 1, i & 3 );

        FString DummyURLText = FString::Printf( TEXT("%s%s"), *Level->URL.String(), SlotURL );
        FURL DummyURL( NULL, *DummyURLText, TRAVEL_Absolute );
        GXboxLog.Write( "XSPLIT spawning dummy viewport=%d url=%s", i, TCHAR_TO_ANSI(*DummyURL.String()) );
        if( !Level->SpawnPlayActor( VP, ROLE_SimulatedProxy, DummyURL, Error ) )
            GXboxLog.Write( "XSPLIT dummy spawn failed viewport=%d error=%s", i, TCHAR_TO_ANSI(*Error) );
        else if( VP->Actor )
        {
            XboxViewportEnsureInputInitialized( VP, "SplitSpawn" );
            XboxSplitPreparePlayer( VP->Actor, VP->bXboxSplitDummy );
            if( !VP->bXboxSplitDummy )
                XboxProfileApplyPlayerOptionsForPort( VP->Actor, i );
            GXboxLog.Write( "XSPLIT dummy spawned viewport=%d actor=0x%08X class=%s", i, (DWORD)VP->Actor, TCHAR_TO_ANSI(VP->Actor->GetClass()->GetName()) );
        }
    }

    GXboxSplitPending = 0;
    GXboxSplitActive = 1;
    XboxSplitSuppressBots( Level );
    if( Primary && Primary->Actor )
    {
        UXboxViewport* PrimaryXVP = Cast<UXboxViewport>( Primary );
        UBOOL bPrimaryDummy = GXboxSplitUseReadySlots && !GXboxSplitReadySlots[0].Joined;
        if( PrimaryXVP )
            PrimaryXVP->bXboxSplitDummy = bPrimaryDummy;
        XboxSplitPreparePlayer( Primary->Actor, bPrimaryDummy );
        if( !bPrimaryDummy )
            XboxProfileApplyPlayerOptionsForPort( Primary->Actor, 0 );
    }
    for( INT i=1; i<Client->Viewports.Num() && i<4; i++ )
        if( Client->Viewports(i) && Client->Viewports(i)->Actor )
        {
            UXboxViewport* VP = Cast<UXboxViewport>( Client->Viewports(i) );
            if( VP )
                XboxViewportEnsureInputInitialized( VP, "SplitActivate" );
            XboxSplitPreparePlayer( Client->Viewports(i)->Actor, VP ? VP->bXboxSplitDummy : 1 );
            if( VP && !VP->bXboxSplitDummy )
                XboxProfileApplyPlayerOptionsForPort( Client->Viewports(i)->Actor, i );
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
        INT PhysicalPort = XVP ? XVP->ControllerPort : i;
        INT RenderOrder = (GXboxSplitActiveMask & (1 << PhysicalPort)) ? XboxSplitActiveOrderForSlot( PhysicalPort ) : -1;
        GXboxLog.Write( "XSPLIT player ready viewport=%d physical=P%d renderOrder=%d actor=0x%08X state=%s physics=%d hud=0x%08X showMenu=%d pauser=%s",
            i, PhysicalPort + 1, RenderOrder + 1, (DWORD)Player, TCHAR_TO_ANSI(StateName), (INT)Player->Physics, (DWORD)Player->myHUD,
            Player->bShowMenu ? 1 : 0,
            Player->Level ? TCHAR_TO_ANSI(*Player->Level->Pauser) : "" );
    }

    GXboxLog.Write( "XSPLIT active viewports=%d", Client->Viewports.Num() );
    XboxSystemLinkSmokeLogGameplayStatus( Client, Level, 1 );
}

static void XboxSplitSmokeCheck( UClient* InClient )
{
    if( XboxSplitControlsProofEnabled() || XboxSplitControlsOnlineProofEnabled()
    ||  !GXboxSplitActive || !XboxSplitSmokeInputProofEnabled() || GXboxSplitSmokeFinished || !InClient || InClient->Viewports.Num() <= 0 )
        return;

    DOUBLE Now = appSeconds();
    DWORD ActiveMask = 0;
    DWORD DoneMask = 0;
    static DWORD BegunMask = 0;
    static DWORD PassedMask = 0;
    static DWORD FailedMask = 0;
    static APlayerPawn* SlotActor[4] = { NULL, NULL, NULL, NULL };
    static FVector SlotStartLocation[4];
    static DOUBLE SlotStartTime[4] = { 0.0, 0.0, 0.0, 0.0 };

    for( INT i=0; i<InClient->Viewports.Num() && i<4; i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( InClient->Viewports(i) );
        APlayerPawn* Player = VP ? VP->Actor : NULL;
        if( !Player || VP->bXboxSplitDummy )
            continue;

        DWORD SlotBit = (1 << i);
        ActiveMask |= SlotBit;
        if( SlotActor[i] != Player )
        {
            SlotActor[i] = Player;
            SlotStartLocation[i] = Player->Location;
            SlotStartTime[i] = Now;
            BegunMask &= ~SlotBit;
            PassedMask &= ~SlotBit;
            FailedMask &= ~SlotBit;
        }

        const TCHAR* StateName = (Player->GetStateFrame() && Player->GetStateFrame()->StateNode)
            ? *Player->GetStateFrame()->StateNode->GetFName()
            : TEXT("None");

        XboxSplitSmokeFeedInput( VP );

        if( !(BegunMask & SlotBit) )
        {
            BegunMask |= SlotBit;
            GXboxSplitSmokeActive = 1;
            GXboxLog.Write( "XSPLIT SELFTEST begin slot=%d actor=0x%08X state=%s loc=%.1f,%.1f,%.1f",
                i,
                (DWORD)Player,
                TCHAR_TO_ANSI(StateName),
                Player->Location.X, Player->Location.Y, Player->Location.Z );
            continue;
        }

        if( PassedMask & SlotBit )
        {
            DoneMask |= SlotBit;
            continue;
        }
        if( FailedMask & SlotBit )
        {
            DoneMask |= SlotBit;
            continue;
        }

        FLOAT DistSq = (Player->Location - SlotStartLocation[i]).SizeSquared();
        DOUBLE Elapsed = Now - SlotStartTime[i];
        if( Elapsed >= 0.75 && DistSq > 25.0f )
        {
            PassedMask |= SlotBit;
            DoneMask |= SlotBit;
            GXboxLog.Write( "XSPLIT SELFTEST PASS slot=%d movement elapsed=%.2f distSq=%.1f loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f acc=%.1f,%.1f,%.1f",
                i,
                Elapsed,
                DistSq,
                Player->Location.X, Player->Location.Y, Player->Location.Z,
                Player->Velocity.X, Player->Velocity.Y, Player->Velocity.Z,
                Player->Acceleration.X, Player->Acceleration.Y, Player->Acceleration.Z );
        }
        else if( Elapsed >= 4.0 )
        {
            FailedMask |= SlotBit;
            DoneMask |= SlotBit;
            GXboxLog.Write( "XSPLIT SELFTEST FAIL slot=%d movement elapsed=%.2f distSq=%.1f loc=%.1f,%.1f,%.1f vel=%.1f,%.1f,%.1f acc=%.1f,%.1f,%.1f axes=%.1f/%.1f/%.1f/%.1f/%.1f menu=%d pauser=%s",
                i,
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

    if( ActiveMask && ( (PassedMask | FailedMask) & ActiveMask ) == ActiveMask )
    {
        GXboxSplitSmokeFinished = 1;
        GXboxLog.Write( "XSPLIT SELFTEST done active=0x%X pass=0x%X fail=0x%X",
            ActiveMask, PassedMask & ActiveMask, FailedMask & ActiveMask );
    }
}

static void XboxSystemLinkSmokeLogGameplayStatus( UClient* InClient, ULevel* CurrentLevel, UBOOL bForce )
{
    if( !XboxSystemLinkSmokeEnabled() || !InClient || !CurrentLevel || !CurrentLevel->GetLevelInfo() )
        return;

    static DOUBLE LastStatusTime = 0.0;
    DOUBLE Now = appSeconds();
    if( !bForce && Now - LastStatusTime < 2.0 )
        return;
    LastStatusTime = Now;

    INT PlayerPawnCount = 0;
    INT PlayerPawnWithPlayerCount = 0;
    INT PlayerPawnWithPRICount = 0;
    INT NonSpectatorCount = 0;
    INT ViewportActorCount = 0;

    for( INT i=0; i<CurrentLevel->Actors.Num(); i++ )
    {
        APlayerPawn* Player = Cast<APlayerPawn>( CurrentLevel->Actors(i) );
        if( !Player || Player->bDeleteMe )
            continue;

        PlayerPawnCount++;
        if( Player->Player )
            PlayerPawnWithPlayerCount++;
        if( Player->PlayerReplicationInfo )
        {
            PlayerPawnWithPRICount++;
            if( !Player->PlayerReplicationInfo->bIsSpectator )
                NonSpectatorCount++;
        }
        if( XboxSystemLinkActorIsViewportActor( InClient, Player ) )
            ViewportActorCount++;
    }

    TCHAR SlotText[768];
    SlotText[0] = 0;
    for( INT i=0; i<InClient->Viewports.Num() && i<4; i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( InClient->Viewports(i) );
        APlayerPawn* Player = VP ? VP->Actor : NULL;
        const TCHAR* StateName = (Player && Player->GetStateFrame() && Player->GetStateFrame()->StateNode)
            ? Player->GetStateFrame()->StateNode->GetName()
            : TEXT("None");
        const TCHAR* ClassName = (Player && Player->GetClass()) ? Player->GetClass()->GetName() : TEXT("None");
        const TCHAR* PlayerName = (Player && Player->PlayerReplicationInfo) ? *Player->PlayerReplicationInfo->PlayerName : TEXT("None");
        TCHAR Part[192];
        appSprintf
        (
            Part,
            TEXT(" s%i:%s/%s pri=%s spec=%i dummy=%i hp=%i"),
            i,
            ClassName,
            StateName,
            PlayerName,
            (Player && Player->PlayerReplicationInfo && Player->PlayerReplicationInfo->bIsSpectator) ? 1 : 0,
            (VP && VP->bXboxSplitDummy) ? 1 : 0,
            Player ? Player->Health : -999
        );
        appStrncat( SlotText, Part, ARRAY_COUNT(SlotText)-appStrlen(SlotText)-1 );
    }

    AGameInfo* Game = CurrentLevel->GetLevelInfo()->Game;
    GXboxLog.Write( "XSL GAMEPLAY status map=%s net=%d viewports=%d ready=0x%X locked=0x%X sent=0x%X bound=0x%X gamePlayers=%d pawns=%d pawnsWithPlayer=%d pawnsWithPRI=%d nonSpec=%d vpActors=%d%s",
        TCHAR_TO_ANSI(*CurrentLevel->URL.Map),
        (INT)CurrentLevel->GetLevelInfo()->NetMode,
        InClient->Viewports.Num(),
        XboxSystemLinkReadyMask(),
        XboxSystemLinkLockedMask(),
        GXboxSystemLinkChildJoinSentMask,
        GXboxSystemLinkChildBoundMask,
        Game ? Game->NumPlayers : -1,
        PlayerPawnCount,
        PlayerPawnWithPlayerCount,
        PlayerPawnWithPRICount,
        NonSpectatorCount,
        ViewportActorCount,
        TCHAR_TO_ANSI(SlotText) );

    UNetConnection* HealthConnection = NULL;
    if( CurrentLevel->NetDriver )
    {
        if( CurrentLevel->NetDriver->ServerConnection )
            HealthConnection = CurrentLevel->NetDriver->ServerConnection;
        else
        {
            // Server travel can briefly leave an old and a replacement client
            // connection in the new driver's list.  Report the connection that
            // most recently received traffic instead of the stale first entry.
            for( INT ConnectionIndex=0; ConnectionIndex<CurrentLevel->NetDriver->ClientConnections.Num(); ConnectionIndex++ )
            {
                UNetConnection* Candidate = CurrentLevel->NetDriver->ClientConnections(ConnectionIndex);
                if( Candidate && (!HealthConnection || Candidate->LastReceiveTime > HealthConnection->LastReceiveTime) )
                    HealthConnection = Candidate;
            }
        }
    }
    if( HealthConnection )
    {
        GXboxLog.Write( "XSL NETHEALTH state=%d alert=%d inPPS=%.2f outPPS=%.2f inLoss=%.2f outLoss=%.2f bestLag=%.3f rxAge=%.3f txAge=%.3f speed=%d queued=%d",
            (INT)HealthConnection->State,
            (ViewportActorCount > 0 && InClient->Viewports(0) && InClient->Viewports(0)->Actor && InClient->Viewports(0)->Actor->bBadConnectionAlert) ? 1 : 0,
            HealthConnection->InPackets,
            HealthConnection->OutPackets,
            HealthConnection->InLoss,
            HealthConnection->OutLoss,
            HealthConnection->BestLag,
            CurrentLevel->NetDriver->Time - HealthConnection->LastReceiveTime,
            CurrentLevel->NetDriver->Time - HealthConnection->LastSendTime,
            HealthConnection->CurrentNetSpeed,
            HealthConnection->QueuedBytes );
    }

    if( XboxSystemLinkLongSoakEnabled()
    &&  (CurrentLevel->GetLevelInfo()->NetMode == NM_ListenServer || CurrentLevel->GetLevelInfo()->NetMode == NM_Client) )
    {
        static DOUBLE SoakStartTime = 0.0;
        static DOUBLE LastCheckpointTime = 0.0;
        static INT TravelStage = 0;
        static INT ObservedMapLegs = 0;
        static INT AlertSamples = 0;
        static INT ClosedSamples = 0;
        static FLOAT PeakInLoss = 0.0f;
        static FLOAT PeakOutLoss = 0.0f;
        static FLOAT PeakReceiveAge = 0.0f;
        static TCHAR LastMap[128] = TEXT("");
        static UBOOL PassLogged = 0;

        if( SoakStartTime == 0.0 )
        {
            SoakStartTime = Now;
            LastCheckpointTime = Now;
            GXboxLog.Write( "XSL LONGSOAK START map=%s net=%d target=900s travelAt=120/300 bots=6 minPlayers=8",
                TCHAR_TO_ANSI(*CurrentLevel->URL.Map),
                (INT)CurrentLevel->GetLevelInfo()->NetMode );
        }

        if( appStricmp( LastMap, *CurrentLevel->URL.Map ) != 0 )
        {
            appStrncpy( LastMap, *CurrentLevel->URL.Map, ARRAY_COUNT(LastMap) );
            LastMap[ARRAY_COUNT(LastMap)-1] = 0;
            ObservedMapLegs++;
            GXboxLog.Write( "XSL LONGSOAK MAP leg=%d elapsed=%.1f map=%s net=%d players=%d",
                ObservedMapLegs,
                Now - SoakStartTime,
                TCHAR_TO_ANSI(LastMap),
                (INT)CurrentLevel->GetLevelInfo()->NetMode,
                Game ? Game->NumPlayers : -1 );
        }

        if( HealthConnection )
        {
            UBOOL bAlert = ViewportActorCount > 0
                && InClient->Viewports(0)
                && InClient->Viewports(0)->Actor
                && InClient->Viewports(0)->Actor->bBadConnectionAlert;
            if( bAlert )
                AlertSamples++;
            if( HealthConnection->State != USOCK_Open )
                ClosedSamples++;
            PeakInLoss = Max<FLOAT>( PeakInLoss, HealthConnection->InLoss );
            PeakOutLoss = Max<FLOAT>( PeakOutLoss, HealthConnection->OutLoss );
            PeakReceiveAge = Max<FLOAT>( PeakReceiveAge, CurrentLevel->NetDriver->Time - HealthConnection->LastReceiveTime );
        }

        DOUBLE SoakElapsed = Now - SoakStartTime;
        if( Now - LastCheckpointTime >= 60.0 )
        {
            LastCheckpointTime = Now;
            GXboxLog.Write( "XSL LONGSOAK CHECK elapsed=%.1f mapLegs=%d map=%s net=%d players=%d state=%d alerts=%d closed=%d peakLoss=%.2f/%.2f peakRxAge=%.3f",
                SoakElapsed,
                ObservedMapLegs,
                TCHAR_TO_ANSI(LastMap),
                (INT)CurrentLevel->GetLevelInfo()->NetMode,
                Game ? Game->NumPlayers : -1,
                HealthConnection ? (INT)HealthConnection->State : -1,
                AlertSamples,
                ClosedSamples,
                PeakInLoss,
                PeakOutLoss,
                PeakReceiveAge );
            GXboxLog.Flush();
        }

        if( CurrentLevel->GetLevelInfo()->NetMode == NM_ListenServer )
        {
            const TCHAR* TravelURL = NULL;
            if( TravelStage == 0 && SoakElapsed >= 120.0 )
                TravelURL = TEXT("DM-Deck16][.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=8?Difficulty=3?MaxPlayers=16?LAN");
            else if( TravelStage == 1 && SoakElapsed >= 300.0 )
                TravelURL = TEXT("DM-Oblivion.unr?Game=Botpack.DeathMatchPlus?FragLimit=0?TimeLimit=0?MinPlayers=8?Difficulty=3?MaxPlayers=16?LAN");

            if( TravelURL )
            {
                TravelStage++;
                GXboxLog.Write( "XSL LONGSOAK SERVERTRAVEL stage=%d elapsed=%.1f url=%s",
                    TravelStage, SoakElapsed, TCHAR_TO_ANSI(TravelURL) );
                GXboxLog.Flush();
                CurrentLevel->GetLevelInfo()->eventServerTravel( FString(TravelURL), 0 );
            }
        }

        if( !PassLogged && SoakElapsed >= 900.0 )
        {
            UBOOL bOpen = HealthConnection && HealthConnection->State == USOCK_Open;
            UBOOL bEnoughMaps = ObservedMapLegs >= 3;
            PassLogged = 1;
            GXboxLog.Write( "XSL LONGSOAK %s elapsed=%.1f mapLegs=%d map=%s net=%d players=%d state=%d alerts=%d closed=%d peakLoss=%.2f/%.2f peakRxAge=%.3f",
                (bOpen && bEnoughMaps) ? "PASS" : "FAIL",
                SoakElapsed,
                ObservedMapLegs,
                TCHAR_TO_ANSI(LastMap),
                (INT)CurrentLevel->GetLevelInfo()->NetMode,
                Game ? Game->NumPlayers : -1,
                HealthConnection ? (INT)HealthConnection->State : -1,
                AlertSamples,
                ClosedSamples,
                PeakInLoss,
                PeakOutLoss,
                PeakReceiveAge );
            GXboxLog.Flush();
        }
    }

    static UBOOL bSystemLinkSmokeDoneLogged = 0;
    if( !bSystemLinkSmokeDoneLogged
    &&  XboxSystemLinkFourPlayerStressEnabled()
    &&  GXboxSplitSmokeFinished
    &&  CurrentLevel->GetLevelInfo()->NetMode != NM_Standalone
    &&  InClient->Viewports.Num() >= 4
    &&  (XboxSystemLinkReadyMask() & 0xF) == 0xF
    &&  (XboxSystemLinkLockedMask() & 0xF) == 0xF )
    {
        bSystemLinkSmokeDoneLogged = 1;
        GXboxLog.Write( "XSL SMOKE DONE role=%s map=%s net=%d viewports=%d ready=0x%X locked=0x%X bound=0x%X gamePlayers=%d pawns=%d pawnsWithPlayer=%d nonSpec=%d vpActors=%d availKB=%u",
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
            TCHAR_TO_ANSI(*CurrentLevel->URL.Map),
            (INT)CurrentLevel->GetLevelInfo()->NetMode,
            InClient->Viewports.Num(),
            XboxSystemLinkReadyMask(),
            XboxSystemLinkLockedMask(),
            GXboxSystemLinkChildBoundMask,
            Game ? Game->NumPlayers : -1,
            PlayerPawnCount,
            PlayerPawnWithPlayerCount,
            NonSpectatorCount,
            ViewportActorCount,
            (unsigned)XboxMenuAvailPhysKB() );
        GXboxLog.Flush();
    }

    static APlayerPawn* LastSlotActor[4] = { NULL, NULL, NULL, NULL };
    static FVector LastSlotLocation[4];
    for( INT i=0; i<InClient->Viewports.Num() && i<4; i++ )
    {
        UXboxViewport* VP = Cast<UXboxViewport>( InClient->Viewports(i) );
        APlayerPawn* Player = VP ? VP->Actor : NULL;
        if( !Player )
            continue;

        FLOAT MoveDeltaSq = 0.0f;
        if( LastSlotActor[i] == Player )
            MoveDeltaSq = (Player->Location - LastSlotLocation[i]).SizeSquared();
        LastSlotActor[i] = Player;
        LastSlotLocation[i] = Player->Location;

        const TCHAR* ClassName = Player->GetClass() ? Player->GetClass()->GetName() : TEXT("None");
        const TCHAR* PlayerName = Player->PlayerReplicationInfo ? *Player->PlayerReplicationInfo->PlayerName : TEXT("None");
        GXboxLog.Write( "XSL SLOT status net=%d slot=%d vp=0x%08X actor=0x%08X class=%s pri=%s dummy=%d player=%d playerActor=%d role=%d/%d loc=%.1f,%.1f,%.1f moveSq=%.1f vel=%.1f,%.1f,%.1f acc=%.1f,%.1f,%.1f axes=%.1f/%.1f/%.1f fire=%d alt=%d ready=%d",
            (INT)CurrentLevel->GetLevelInfo()->NetMode,
            i,
            (DWORD)VP,
            (DWORD)Player,
            TCHAR_TO_ANSI(ClassName),
            TCHAR_TO_ANSI(PlayerName),
            (VP && VP->bXboxSplitDummy) ? 1 : 0,
            Player->Player ? 1 : 0,
            (Player->Player && Player->Player->Actor == Player) ? 1 : 0,
            (INT)Player->Role,
            (INT)Player->RemoteRole,
            Player->Location.X, Player->Location.Y, Player->Location.Z,
            MoveDeltaSq,
            Player->Velocity.X, Player->Velocity.Y, Player->Velocity.Z,
            Player->Acceleration.X, Player->Acceleration.Y, Player->Acceleration.Z,
            Player->aForward, Player->aBaseY, Player->aStrafe,
            Player->bFire ? 1 : 0,
            Player->bAltFire ? 1 : 0,
            Player->bReadyToPlay ? 1 : 0 );
    }
}

extern "C" void XboxSplitTickDummies( UClient* InClient )
{
    if( !GXboxSplitActive || !InClient || InClient->Viewports.Num() < 2 )
    {
        XboxSplitSmokeCheck( InClient );
        UViewport* PrimaryViewport = (InClient && InClient->Viewports.Num() > 0) ? InClient->Viewports(0) : NULL;
        ULevel* CurrentLevel = (PrimaryViewport && PrimaryViewport->Actor) ? PrimaryViewport->Actor->GetLevel() : NULL;
        XboxSystemLinkSmokeLogGameplayStatus( InClient, CurrentLevel, 0 );
        return;
    }

    UXboxClient* XboxClient = Cast<UXboxClient>( InClient );
    UViewport* PrimaryViewport = InClient->Viewports.Num() > 0 ? InClient->Viewports(0) : NULL;
    ULevel* CurrentLevel = (PrimaryViewport && PrimaryViewport->Actor) ? PrimaryViewport->Actor->GetLevel() : NULL;
    XboxSystemLinkBindClientChildActors( XboxClient, CurrentLevel );
    XboxSystemLinkSmokeLogGameplayStatus( InClient, CurrentLevel, 0 );
    XboxSplitSmokeCheck( InClient );

    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    for( INT i=1; i<InClient->Viewports.Num() && i<4; i++ )
    {
        UViewport* VP = InClient->Viewports(i);
        UXboxViewport* XVP = Cast<UXboxViewport>( VP );
        if( !XVP )
            continue;

        if( GXboxSplitBorrowedActor[i] )
        {
            XVP->bXboxSplitDummy = 1;
            continue;
        }

        UBOOL bShouldBeDummy = XboxSplitSmokeInputProofEnabled()
            ? 0
            : ( GXboxSplitUseReadySlots
                ? !GXboxSplitReadySlots[i].Joined
                : !(DeviceMask & (1 << i)) );
        if( CurrentLevel && CurrentLevel->GetLevelInfo() && CurrentLevel->GetLevelInfo()->NetMode == NM_Client && GXboxSplitReadySlots[i].Joined )
            bShouldBeDummy = ((GXboxSystemLinkChildBoundMask & (1 << i)) == 0);
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

    FString SocketErrorText;
    GXboxSystemLink.SocketsReady = InitSockets( SocketErrorText );
    DWORD LinkStatus = GXboxSystemLink.SocketsReady ? XNetGetEthernetLinkStatus() : 0;
    GXboxSystemLink.LastError = GXboxSystemLink.SocketsReady ? 0 : 1;
    GXboxLog.Write( "XSL probe net init shared ready=%d link=0x%08X active=%d error=%s",
        GXboxSystemLink.SocketsReady,
        LinkStatus,
        (LinkStatus & XNET_ETHERNET_LINK_ACTIVE) ? 1 : 0,
        TCHAR_TO_ANSI(*SocketErrorText) );
    return GXboxSystemLink.SocketsReady;
}

static void XboxSystemLinkStopLobby( UBOOL bKeepNetwork )
{
    XboxSystemLinkEnsureState();
    if( GXboxSystemLink.Socket != INVALID_SOCKET )
    {
        closesocket( GXboxSystemLink.Socket );
        GXboxSystemLink.Socket = INVALID_SOCKET;
    }
    GXboxSystemLink.Started = 0;
    GXboxSystemLink.LocalPort = 0;
    GXboxSystemLink.Role = XSLR_Seeking;
    GXboxSystemLink.HostId = 0;
    GXboxSystemLink.Phase = XSLP_Discovery;
    GXboxSystemLink.ReadyConfirmed = 0;
    GXboxSystemLink.PendingTravel = 0;
    GXboxSystemLink.PendingTravelDeadline = 0.0f;
    GXboxSystemLink.LaunchAckId = 0;
    GXboxSystemLink.Peers.Empty();

    if( !bKeepNetwork )
    {
        XboxSystemLinkUnregisterSession( "lobby stop" );
#if TARGET_XBOX
        XboxIpDrvClearSecureTravelHost();
#endif
        GXboxLog.Write( "XSL session cleared; network stack kept alive ready=%d xnaddr=0x%08X",
            GXboxSystemLink.SocketsReady ? 1 : 0,
            GXboxSystemLink.LocalXnAddrStatus );
    }

    GXboxLog.Write( "XSL lobby stopped keepNetwork=%d", bKeepNetwork ? 1 : 0 );
}

static void XboxSystemLinkStop()
{
    XboxSystemLinkStopLobby( 0 );
}

static void XboxSystemLinkStopForTravel()
{
    XboxSystemLinkStopLobby( 1 );
}

extern "C" void XboxSystemLinkAbortTravelCleanup( const char* Reason )
{
    if( !GXboxSystemLinkStateInitialized )
        return;

    UBOOL bHadState =
        GXboxSystemLink.Started
    ||  GXboxSystemLink.Socket != INVALID_SOCKET
    ||  GXboxSystemLink.SessionRegistered
    ||  GXboxSystemLink.PendingTravel
    ||  GXboxSystemLink.Phase == XSLP_Launching
    ||  GXboxSystemLink.LaunchId
    ||  GXboxSystemLink.LaunchAckId
    ||  GXboxSystemLink.HasSecureHostAddress;
    if( !bHadState )
        return;

    GXboxLog.Write( "XSL abort travel cleanup reason=%s role=%s phase=%s launch=0x%08X ack=0x%08X pending=%d socket=0x%08X session=%d",
        Reason ? Reason : "unknown",
        TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
        TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
        GXboxSystemLink.LaunchId,
        GXboxSystemLink.LaunchAckId,
        GXboxSystemLink.PendingTravel ? 1 : 0,
        (DWORD)GXboxSystemLink.Socket,
        GXboxSystemLink.SessionRegistered ? 1 : 0 );

    XboxSystemLinkStopLobby( 0 );
    GXboxSystemLink.LaunchId = 0;
    GXboxSystemLink.LaunchAckId = 0;
    GXboxSystemLink.LastSeenLaunchId = 0;
    GXboxSystemLink.PendingTravelTime = 0.0f;
    GXboxSystemLink.PendingTravelDeadline = 0.0f;
    GXboxSystemLink.PendingHostAddress = 0;
    GXboxSystemLinkSmokeHostPreTravelHoldDone = 0;
}

static UBOOL XboxSystemLinkStart( UXboxViewport* Viewport )
{
    XboxSystemLinkEnsureState();
    if( GXboxSystemLink.Started )
        return 1;

    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    UGameEngine* GameEngine = Client ? Cast<UGameEngine>(Client->Engine) : NULL;
    if( GameEngine && GameEngine->GPendingLevel )
    {
        GXboxLog.Write( "XSL cancelling stale pending travel before lobby start" );
        GameEngine->CancelPending();
    }

    if( !XboxSystemLinkInitSockets() )
        return 0;

    if( GXboxSystemLink.SessionRegistered )
        XboxSystemLinkUnregisterSession( "fresh lobby" );

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
    {
        DWORD RandomId = 0;
        if( XNetRandom( (BYTE*)&RandomId, sizeof(RandomId) ) == 0 && RandomId )
            GXboxSystemLink.LocalId = RandomId;
        else
            GXboxSystemLink.LocalId = GetTickCount() ^ (DWORD)&GXboxSystemLink;
    }
    GXboxSystemLink.Started = 1;
    GXboxSystemLink.Role = XSLR_Seeking;
    GXboxSystemLink.HostId = 0;
    GXboxSystemLink.Phase = XSLP_Discovery;
    GXboxSystemLink.ReadyConfirmed = 0;
    GXboxSystemLink.LastReadyMask = 0;
    GXboxSystemLink.LastLockedMask = 0;
    GXboxSystemLink.GameType = GXboxMenu.InstantGameType;
    GXboxSystemLink.MapIndex = GXboxMenu.InstantMap[Clamp<INT>(GXboxMenu.InstantGameType, 0, 63)];
    GXboxSystemLink.FragLimitIndex = GXboxMenu.InstantFragLimit;
    GXboxSystemLink.TimeLimitIndex = GXboxMenu.InstantTimeLimit;
    GXboxSystemLink.SkillIndex = GXboxMenu.InstantSkill;
    GXboxSystemLink.LaunchId = 0;
    GXboxSystemLink.LastSeenLaunchId = 0;
    GXboxSystemLink.PendingTravel = 0;
    GXboxSystemLink.PendingTravelTime = 0.0f;
    GXboxSystemLink.PendingTravelDeadline = 0.0f;
    GXboxSystemLink.PendingHostAddress = 0;
    GXboxSystemLinkSmokeHostPreTravelHoldDone = 0;
    GXboxSystemLink.SecureHostAddress = 0;
    GXboxSystemLink.HasSecureHostAddress = 0;
    GXboxSystemLink.LaunchAckId = 0;
    GXboxSystemLink.LastLoggedRole = -1;
    GXboxSystemLink.LastLoggedHostId = 0;
    GXboxSystemLink.EnterTime = appSeconds();
    GXboxSystemLink.LastSendTime = 0.0f;
    GXboxSystemLink.LastLogTime = 0.0f;
    GXboxSystemLink.LastSecureConnectLogTime = 0.0f;
    GXboxSystemLink.SendCounter = 0;
    GXboxSystemLink.Peers.Empty();
    GXboxSystemLinkProbeSendLogBudget = 96;
    GXboxSystemLinkProbePeerLogBudget = 96;
    XboxSystemLinkUpdateLocalXnAddr();
    GXboxLog.Write( "XSL lobby started id=0x%08X port=%d xnaddr=0x%08X",
        GXboxSystemLink.LocalId, GXboxSystemLink.LocalPort, GXboxSystemLink.LocalXnAddrStatus );
    return 1;
}

static void XboxSystemLinkUpdateLocalAdvertisement()
{
    GXboxSystemLink.LastReadyMask = XboxSystemLinkReadyMask();
    GXboxSystemLink.LastLockedMask = XboxSystemLinkLockedMask();

    if( GXboxSystemLink.Role == XSLR_Host )
    {
        XboxSystemLinkEnsureHostSession();
        XboxSystemLinkUpdateLocalXnAddr();
    }

    if( GXboxMenu.Screen == XMS_SystemLinkMapSelect && GXboxSystemLink.Role == XSLR_Host )
    {
        GXboxSystemLink.GameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, 63 );
        GXboxSystemLink.MapIndex = GXboxMenu.InstantMap[GXboxSystemLink.GameType];
        GXboxSystemLink.FragLimitIndex = GXboxMenu.InstantFragLimit;
        GXboxSystemLink.TimeLimitIndex = GXboxMenu.InstantTimeLimit;
        GXboxSystemLink.SkillIndex = GXboxMenu.InstantSkill;
    }

    if( GXboxSystemLink.Phase == XSLP_Launching )
        return;

    if( !GXboxSystemLink.HostId )
        GXboxSystemLink.Phase = XSLP_Discovery;
    else if( GXboxMenu.Screen == XMS_SystemLinkMapSelect && GXboxSystemLink.Role == XSLR_Host )
        GXboxSystemLink.Phase = XSLP_MapSelect;
    else if( GXboxSystemLink.ReadyConfirmed )
        GXboxSystemLink.Phase = XSLP_ReadyConfirmed;
    else
        GXboxSystemLink.Phase = XSLP_Ready;
}

static void XboxSystemLinkSendProbeTo( const char* Reason, DWORD Address, INT Port, const char* Packet, INT PacketLen, UBOOL bTraceSuccess )
{
    if( !Address || !Port || GXboxSystemLink.Socket == INVALID_SOCKET )
        return;

    sockaddr_in To;
    appMemzero( &To, sizeof(To) );
    To.sin_family = AF_INET;
    To.sin_addr.s_addr = Address;
    To.sin_port = htons( (u_short)Port );

    INT Sent = sendto( GXboxSystemLink.Socket, Packet, PacketLen, 0, (sockaddr*)&To, sizeof(To) );
    if( Sent == SOCKET_ERROR )
    {
        GXboxSystemLink.LastError = WSAGetLastError();
        if( GXboxSystemLinkProbeSendLogBudget > 0 )
        {
            TCHAR AddrText[32];
            XboxSystemLinkFormatAddress( Address, AddrText, ARRAY_COUNT(AddrText) );
            GXboxLog.Write( "XSL probe send failed reason=%s addr=%s port=%d err=%d",
                Reason ? Reason : "unknown",
                TCHAR_TO_ANSI(AddrText),
                Port,
                GXboxSystemLink.LastError );
            GXboxSystemLinkProbeSendLogBudget--;
        }
    }
    else if( bTraceSuccess && GXboxSystemLinkProbeSendLogBudget > 0 )
    {
        TCHAR AddrText[32];
        XboxSystemLinkFormatAddress( Address, AddrText, ARRAY_COUNT(AddrText) );
        GXboxLog.Write( "XSL probe send reason=%s addr=%s port=%d bytes=%d role=%s phase=%s host=0x%08X",
            Reason ? Reason : "unknown",
            TCHAR_TO_ANSI(AddrText),
            Port,
            Sent,
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
            TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
            GXboxSystemLink.HostId );
        GXboxSystemLinkProbeSendLogBudget--;
    }
}

static void XboxSystemLinkSendProbe()
{
    if( !GXboxSystemLink.Started || GXboxSystemLink.Socket == INVALID_SOCKET )
        return;

    XboxSystemLinkUpdateLocalAdvertisement();

    char XnAddrHex[sizeof(XNADDR)*2 + 1];
    char KeyIdHex[sizeof(XNKID)*2 + 1];
    char KeyHex[sizeof(XNKEY)*2 + 1];
    appMemzero( XnAddrHex, sizeof(XnAddrHex) );
    appMemzero( KeyIdHex, sizeof(KeyIdHex) );
    appMemzero( KeyHex, sizeof(KeyHex) );
    XboxSystemLinkHexEncodeZeroes( sizeof(XNADDR), XnAddrHex, sizeof(XnAddrHex) );
    XboxSystemLinkHexEncodeZeroes( sizeof(XNKID), KeyIdHex, sizeof(KeyIdHex) );
    XboxSystemLinkHexEncodeZeroes( sizeof(XNKEY), KeyHex, sizeof(KeyHex) );

    UBOOL bAdvertiseSecure =
        GXboxSystemLink.Role == XSLR_Host
    &&  GXboxSystemLink.SessionRegistered
    &&  GXboxSystemLink.SessionIsHost
    &&  GXboxSystemLink.HasLocalXnAddr;

    if( bAdvertiseSecure )
    {
        XboxSystemLinkHexEncode( (const BYTE*)&GXboxSystemLink.LocalXnAddr, sizeof(GXboxSystemLink.LocalXnAddr), XnAddrHex, sizeof(XnAddrHex) );
        XboxSystemLinkHexEncode( (const BYTE*)&GXboxSystemLink.SessionKeyId, sizeof(GXboxSystemLink.SessionKeyId), KeyIdHex, sizeof(KeyIdHex) );
        XboxSystemLinkHexEncode( (const BYTE*)&GXboxSystemLink.SessionKey, sizeof(GXboxSystemLink.SessionKey), KeyHex, sizeof(KeyHex) );
    }

    char Packet[512];
    sprintf
    (
        Packet,
        "UTXSL4|%08X|%d|%lu|%d|%08X|%d|%d|%d|%d|%d|%d|%d|%d|%d|%08X|%08X|%d|%s|%s|%s",
        GXboxSystemLink.LocalId,
        GXboxSystemLink.LocalPort,
        GXboxSystemLink.SendCounter++,
        GXboxSystemLink.Role,
        GXboxSystemLink.HostId,
        GXboxSystemLink.Phase,
        GXboxSystemLink.LastReadyMask,
        GXboxSystemLink.LastLockedMask,
        GXboxSystemLink.ReadyConfirmed ? 1 : 0,
        GXboxSystemLink.GameType,
        GXboxSystemLink.MapIndex,
        GXboxSystemLink.FragLimitIndex,
        GXboxSystemLink.TimeLimitIndex,
        GXboxSystemLink.SkillIndex,
        GXboxSystemLink.LaunchId,
        GXboxSystemLink.LaunchAckId,
        bAdvertiseSecure ? 1 : 0,
        XnAddrHex,
        KeyIdHex,
        KeyHex
    );
    INT PacketLen = 0;
    while( Packet[PacketLen] )
        PacketLen++;

    for( INT i=0; i<GXboxSystemLinkPortCount; i++ )
        XboxSystemLinkSendProbeTo( "broadcast", INADDR_BROADCAST, GXboxSystemLinkBasePort + i, Packet, PacketLen, 0 );

    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( XboxSystemLinkIsUsableIPv4(Peer.Address) && Peer.Port )
            XboxSystemLinkSendProbeTo( "peer", Peer.Address, Peer.Port, Packet, PacketLen, 1 );
    }

    if( GXboxSystemLink.Role == XSLR_Client && GXboxSystemLink.HasSecureHostAddress && GXboxSystemLink.HostId )
    {
        INT HostPort = 0;
        for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
        {
            if( GXboxSystemLink.Peers(i).Id == GXboxSystemLink.HostId )
            {
                HostPort = GXboxSystemLink.Peers(i).Port;
                break;
            }
        }

        if( HostPort )
            XboxSystemLinkSendProbeTo( "secure-host", GXboxSystemLink.SecureHostAddress, HostPort, Packet, PacketLen, 1 );
    }
}

static void XboxSystemLinkApplyPeerSecurity( FXboxSystemLinkPeer& Peer, UBOOL HasSecureInfo, const XNADDR* XnAddr, const XNKID* SessionKeyId, const XNKEY* SessionKey )
{
    Peer.HasSecureInfo = HasSecureInfo ? 1 : 0;
    if( Peer.HasSecureInfo && XnAddr && SessionKeyId && SessionKey )
    {
        appMemcpy( &Peer.XnAddr, XnAddr, sizeof(Peer.XnAddr) );
        appMemcpy( &Peer.SessionKeyId, SessionKeyId, sizeof(Peer.SessionKeyId) );
        appMemcpy( &Peer.SessionKey, SessionKey, sizeof(Peer.SessionKey) );
    }
}

static void XboxSystemLinkRecordPeer( DWORD Id, DWORD Address, INT Port, INT Role, DWORD HostId, INT Phase, INT ReadyMask, INT LockedMask, INT Confirmed, INT GameType, INT MapIndex, INT FragLimitIndex, INT TimeLimitIndex, INT SkillIndex, DWORD LaunchId, DWORD LaunchAckId, UBOOL HasSecureInfo, const XNADDR* XnAddr, const XNKID* SessionKeyId, const XNKEY* SessionKey, FLOAT Now )
{
    if( Id == GXboxSystemLink.LocalId )
        return;

    Role = Clamp<INT>( Role, XSLR_Seeking, XSLR_Client );
    Phase = Clamp<INT>( Phase, XSLP_Discovery, XSLP_Launching );
    if( Port < GXboxSystemLinkBasePort || Port >= GXboxSystemLinkBasePort + GXboxSystemLinkPortCount )
        Port = 0;
    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( Peer.Id == Id )
        {
            // A raw pcap broadcast and its secure XNet delivery can both reach
            // this socket. Once XNet verifies a peer, never let the unusable
            // raw-source sentinel replace that connection-owned address.
            if( !XboxSystemLinkIsUsableIPv4(Address)
            &&  Peer.VerifiedSecurePeer
            &&  XboxSystemLinkIsXNetVirtualAddress(Peer.SecureAddress) )
            {
                Address = Peer.SecureAddress;
            }
            UBOOL bStateChanged =
                Peer.Address != Address
            ||  Peer.Port != Port
            ||  Peer.Role != Role
            ||  Peer.HostId != HostId
            ||  Peer.Phase != Phase
            ||  Peer.ReadyMask != ReadyMask
            ||  Peer.LockedMask != LockedMask
            ||  Peer.Confirmed != (Confirmed ? 1 : 0)
            ||  Peer.GameType != GameType
            ||  Peer.MapIndex != MapIndex
            ||  Peer.FragLimitIndex != FragLimitIndex
            ||  Peer.TimeLimitIndex != TimeLimitIndex
            ||  Peer.SkillIndex != SkillIndex
            ||  Peer.LaunchId != LaunchId
            ||  Peer.LaunchAckId != LaunchAckId
            ||  Peer.HasSecureInfo != (HasSecureInfo ? 1 : 0);
            if( Peer.Address != Address )
            {
                UBOOL bIncomingSecureAddress = XboxSystemLinkIsXNetVirtualAddress( Address );
                if( !Peer.VerifiedSecurePeer || bIncomingSecureAddress )
                {
                    Peer.VerifiedSecurePeer = 0;
                    Peer.HasSecureAddress = 0;
                    Peer.SecureAddress = 0;
                }
            }
            Peer.Address = Address;
            Peer.Port = Port;
            Peer.Role = Role;
            Peer.HostId = HostId;
            Peer.Phase = Phase;
            Peer.ReadyMask = ReadyMask;
            Peer.LockedMask = LockedMask;
            Peer.Confirmed = Confirmed ? 1 : 0;
            Peer.GameType = GameType;
            Peer.MapIndex = MapIndex;
            Peer.FragLimitIndex = FragLimitIndex;
            Peer.TimeLimitIndex = TimeLimitIndex;
            Peer.SkillIndex = SkillIndex;
            Peer.LaunchId = LaunchId;
            Peer.LaunchAckId = LaunchAckId;
            XboxSystemLinkApplyPeerSecurity( Peer, HasSecureInfo, XnAddr, SessionKeyId, SessionKey );
            Peer.LastSeen = Now;
            Peer.Packets++;
            XboxSystemLinkVerifySecurePeer( Peer );
            if( bStateChanged && GXboxSystemLinkProbePeerLogBudget > 0 )
            {
                TCHAR AddrText[32];
                XboxSystemLinkFormatAddress( Address, AddrText, ARRAY_COUNT(AddrText) );
                GXboxLog.Write( "XSL peer update id=0x%08X addr=%s port=%d role=%s host=0x%08X phase=%s ready=0x%X locked=0x%X confirmed=%d game=%d map=%d fragIdx=%d timeIdx=%d skill=%d launch=0x%08X ack=0x%08X secure=%d packets=%d",
                    Id,
                    TCHAR_TO_ANSI(AddrText),
                    Port,
                    TCHAR_TO_ANSI(XboxSystemLinkRoleText(Role)),
                    HostId,
                    TCHAR_TO_ANSI(XboxSystemLinkPhaseText(Phase)),
                    ReadyMask,
                    LockedMask,
                    Confirmed ? 1 : 0,
                    GameType,
                    MapIndex,
                    FragLimitIndex,
                    TimeLimitIndex,
                    SkillIndex,
                    LaunchId,
                    LaunchAckId,
                    HasSecureInfo ? 1 : 0,
                    Peer.Packets );
                GXboxSystemLinkProbePeerLogBudget--;
            }
            return;
        }
    }

    if( GXboxSystemLink.Peers.Num() >= GXboxSystemLinkMaxPeers )
        GXboxSystemLink.Peers.Remove( 0 );

    FXboxSystemLinkPeer& Peer = *new(GXboxSystemLink.Peers)FXboxSystemLinkPeer;
    appMemzero( &Peer, sizeof(Peer) );
    Peer.Id = Id;
    Peer.Address = Address;
    Peer.SecureAddress = 0;
    Peer.Port = Port;
    Peer.Role = Role;
    Peer.HostId = HostId;
    Peer.Phase = Phase;
    Peer.ReadyMask = ReadyMask;
    Peer.LockedMask = LockedMask;
    Peer.Confirmed = Confirmed ? 1 : 0;
    Peer.GameType = GameType;
    Peer.MapIndex = MapIndex;
    Peer.FragLimitIndex = FragLimitIndex;
    Peer.TimeLimitIndex = TimeLimitIndex;
    Peer.SkillIndex = SkillIndex;
    Peer.LaunchId = LaunchId;
    Peer.LaunchAckId = LaunchAckId;
    XboxSystemLinkApplyPeerSecurity( Peer, HasSecureInfo, XnAddr, SessionKeyId, SessionKey );
    Peer.FirstSeen = Now;
    Peer.LastSeen = Now;
    Peer.Packets = 1;
    XboxSystemLinkVerifySecurePeer( Peer );

    TCHAR AddrText[32];
    XboxSystemLinkFormatAddress( Address, AddrText, ARRAY_COUNT(AddrText) );
    GXboxLog.Write( "XSL peer discovered id=0x%08X addr=%s port=%d role=%s host=0x%08X phase=%s ready=0x%X locked=0x%X confirmed=%d game=%d map=%d fragIdx=%d timeIdx=%d skill=%d secure=%d",
        Id, TCHAR_TO_ANSI(AddrText), Port, TCHAR_TO_ANSI(XboxSystemLinkRoleText(Role)), HostId,
        TCHAR_TO_ANSI(XboxSystemLinkPhaseText(Phase)), ReadyMask, LockedMask, Confirmed ? 1 : 0,
        GameType, MapIndex, FragLimitIndex, TimeLimitIndex, SkillIndex, HasSecureInfo ? 1 : 0 );
}

static void XboxSystemLinkExpirePeers( FLOAT Now )
{
    for( INT i=GXboxSystemLink.Peers.Num()-1; i>=0; i-- )
    {
        FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( Now - Peer.LastSeen > GXboxSystemLinkPeerTimeoutSeconds )
        {
            GXboxLog.Write( "XSL peer expired id=0x%08X role=%s host=0x%08X packets=%d",
                Peer.Id, TCHAR_TO_ANSI(XboxSystemLinkRoleText(Peer.Role)), Peer.HostId, Peer.Packets );
            GXboxSystemLink.Peers.Remove( i );
        }
    }
}

static void XboxSystemLinkUpdateElection( FLOAT Now )
{
    DWORD BestHost = 0;
    FLOAT BestHostSeen = 0.0f;

    if( GXboxSystemLink.Role == XSLR_Host )
    {
        BestHost = GXboxSystemLink.LocalId;
        BestHostSeen = GXboxSystemLink.EnterTime + GXboxSystemLinkHostClaimSeconds;
    }

    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( Peer.Role == XSLR_Host
        && (!BestHost || Peer.FirstSeen < BestHostSeen || (Peer.FirstSeen == BestHostSeen && Peer.Id < BestHost)) )
        {
            BestHost = Peer.Id;
            BestHostSeen = Peer.FirstSeen;
        }
    }

    if( !BestHost )
    {
        if( GXboxSystemLink.Peers.Num() == 0 )
        {
            if( Now - GXboxSystemLink.EnterTime >= GXboxSystemLinkHostClaimSeconds )
                BestHost = GXboxSystemLink.LocalId;
        }
        else
        {
            BestHost = GXboxSystemLink.LocalId;
            for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
                if( GXboxSystemLink.Peers(i).Id < BestHost )
                    BestHost = GXboxSystemLink.Peers(i).Id;
        }
    }

    INT NewRole = XSLR_Seeking;
    if( BestHost )
        NewRole = (BestHost == GXboxSystemLink.LocalId) ? XSLR_Host : XSLR_Client;

    if( NewRole != GXboxSystemLink.Role || BestHost != GXboxSystemLink.HostId )
    {
        INT OldRole = GXboxSystemLink.Role;
        GXboxSystemLink.Role = NewRole;
        GXboxSystemLink.HostId = BestHost;
        if( OldRole == XSLR_Host && NewRole != XSLR_Host && GXboxSystemLink.SessionRegistered && GXboxSystemLink.SessionIsHost )
            XboxSystemLinkUnregisterSession( "lost host election" );
        if( NewRole != XSLR_Client && GXboxSystemLink.SessionRegistered && !GXboxSystemLink.SessionIsHost )
            XboxSystemLinkUnregisterSession( "left client role" );
        if( NewRole == XSLR_Host )
            XboxSystemLinkEnsureHostSession();
    }

    if( GXboxSystemLink.Role != GXboxSystemLink.LastLoggedRole || GXboxSystemLink.HostId != GXboxSystemLink.LastLoggedHostId )
    {
        GXboxSystemLink.LastLoggedRole = GXboxSystemLink.Role;
        GXboxSystemLink.LastLoggedHostId = GXboxSystemLink.HostId;
        GXboxLog.Write( "XSL lobby role local=0x%08X role=%s host=0x%08X peers=%d",
            GXboxSystemLink.LocalId,
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
            GXboxSystemLink.HostId,
            GXboxSystemLink.Peers.Num() );
    }
}

static const FXboxSystemLinkPeer* XboxSystemLinkFindPeerById( DWORD Id )
{
    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
        if( GXboxSystemLink.Peers(i).Id == Id )
            return &GXboxSystemLink.Peers(i);
    return NULL;
}

static UBOOL XboxSystemLinkAllLaunchAcksReceived()
{
    if( GXboxSystemLink.Role != XSLR_Host || !GXboxSystemLink.LaunchId )
        return 0;
    if( !XboxSystemLinkAllMachinesConfirmed() )
        return 0;

    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( !XboxSystemLinkPeerInGroup(Peer) || Peer.Id == GXboxSystemLink.LocalId )
            continue;
        if( Peer.Confirmed && Peer.ReadyMask && ((Peer.ReadyMask & Peer.LockedMask) == Peer.ReadyMask) )
        {
            if( !Peer.VerifiedSecurePeer )
                return 0;
            if( Peer.LaunchAckId != GXboxSystemLink.LaunchId )
                return 0;
        }
    }
    return 1;
}

static UBOOL XboxSystemLinkBuildSelectedMapURL( TCHAR* Out, INT OutCount, UBOOL bListen, DWORD HostAddress, const FXboxSystemLinkPeer* HostPeer )
{
    INT GameTypeCount = XboxMenuGameTypeCount();
    if( GameTypeCount <= 0 )
        return 0;

    INT GameType = Clamp<INT>( GXboxSystemLink.GameType, 0, GameTypeCount-1 );
    INT MapCount = XboxInstantMapList( GameType );
    if( MapCount <= 0 )
        return 0;

    INT MapIndex = Clamp<INT>( GXboxSystemLink.MapIndex, 0, MapCount-1 );
    const FXboxDiscoveredOption& Map = XboxMenuMap( GameType, MapIndex );
    const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
    INT FragLimit = GXboxFragLimits[Clamp<INT>(GXboxSystemLink.FragLimitIndex, 0, ARRAY_COUNT(GXboxFragLimits)-1)];
    INT TimeLimit = GXboxTimeLimits[Clamp<INT>(GXboxSystemLink.TimeLimitIndex, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
    INT Skill = Clamp<INT>( GXboxSystemLink.SkillIndex, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );
    INT MinPlayers = 0;
    INT DesiredBots = 0;
    if( XboxSystemLinkSmokeEnabled() )
    {
        FragLimit = 0;
        TimeLimit = 0;
    }
    if( XboxSystemLinkLongSoakEnabled() )
    {
        MinPlayers = 8;
        DesiredBots = 6;
        Skill = 3;
    }

    TCHAR PlayerURL[512];
    XboxSplitBuildPlayerURLForSlot( 0, PlayerURL, ARRAY_COUNT(PlayerURL), !GXboxSplitReadySlots[0].Joined );

    if( bListen )
    {
        TCHAR RuleURL[256];
        XboxMenuApplyMatchRuleDefaults( *Game.URLValue, FragLimit, MinPlayers );
        XboxMenuBuildMatchRuleOptions( *Game.URLValue, FragLimit, TimeLimit, MinPlayers, Skill, RuleURL );
        XboxMenuQueuePendingMatchRules( *Game.URLValue, FragLimit, TimeLimit, MinPlayers, Skill, DesiredBots );
        appSprintf
        (
            Out,
            TEXT("%s?Game=%s?%s?MaxPlayers=16?Listen?LAN%s"),
            *Map.URLValue,
            *Game.URLValue,
            RuleURL,
            PlayerURL
        );
        GXboxLog.Write( "XSL host rules teamScore=%d objective=%d score=%d time=%d minPlayers=%d bots=%d: %s",
            XboxMenuGameUsesTeamScore( *Game.URLValue ) ? 1 : 0,
            XboxMenuGameUsesObjectiveRules( *Game.URLValue ) ? 1 : 0,
            FragLimit,
            TimeLimit,
            MinPlayers,
            DesiredBots,
            TCHAR_TO_ANSI(Out) );
    }
    else
    {
        TCHAR HostAddr[32];
        TCHAR SecureOptions[192];
        SecureOptions[0] = 0;
        if( HostPeer && HostPeer->HasSecureInfo )
        {
            char XnAddrHex[sizeof(XNADDR)*2 + 1];
            char KeyIdHex[sizeof(XNKID)*2 + 1];
            char KeyHex[sizeof(XNKEY)*2 + 1];
            XboxSystemLinkHexEncode( (const BYTE*)&HostPeer->XnAddr, sizeof(HostPeer->XnAddr), XnAddrHex, sizeof(XnAddrHex) );
            XboxSystemLinkHexEncode( (const BYTE*)&HostPeer->SessionKeyId, sizeof(HostPeer->SessionKeyId), KeyIdHex, sizeof(KeyIdHex) );
            XboxSystemLinkHexEncode( (const BYTE*)&HostPeer->SessionKey, sizeof(HostPeer->SessionKey), KeyHex, sizeof(KeyHex) );
            appSprintf
            (
                SecureOptions,
                TEXT("?SessionID=%s?ExchangeKey=%s?HostAddr=%s"),
                ANSI_TO_TCHAR(KeyIdHex),
                ANSI_TO_TCHAR(KeyHex),
                ANSI_TO_TCHAR(XnAddrHex)
            );
        }
        XboxSystemLinkFormatAddress( HostAddress, HostAddr, ARRAY_COUNT(HostAddr) );
        appSprintf
        (
            Out,
            TEXT("%s:%i?LAN%s%s"),
            HostAddr,
            GXboxSystemLinkGamePort,
            SecureOptions,
            PlayerURL
        );
    }
    Out[OutCount-1] = 0;
    return 1;
}

static void XboxSystemLinkStartTravel( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    UBOOL bHost = GXboxSystemLink.Role == XSLR_Host;
    DWORD HostAddress = GXboxSystemLink.PendingHostAddress;
    const FXboxSystemLinkPeer* SecureTravelHostPeer = NULL;
    if( !bHost && !HostAddress )
    {
        SecureTravelHostPeer = XboxSystemLinkFindPeerById( GXboxSystemLink.HostId );
        if( !SecureTravelHostPeer || !XboxSystemLinkResolveHostSecureAddress( SecureTravelHostPeer, &HostAddress ) )
        {
            GXboxLog.Write( "XSL launch failed: secure host address unavailable host=0x%08X secure=%d",
                GXboxSystemLink.HostId, SecureTravelHostPeer && SecureTravelHostPeer->HasSecureInfo ? 1 : 0 );
            XboxSystemLinkAbortTravelCleanup( "secure host unavailable" );
            return;
        }
    }
    else if( !bHost )
    {
        SecureTravelHostPeer = XboxSystemLinkFindPeerById( GXboxSystemLink.HostId );
    }

    TCHAR URL[1024];
    if( !XboxSystemLinkBuildSelectedMapURL( URL, ARRAY_COUNT(URL), bHost, HostAddress, SecureTravelHostPeer ) )
    {
        GXboxLog.Write( "XSL launch failed: cannot build travel url role=%s host=0x%08X addr=0x%08X",
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)), GXboxSystemLink.HostId, HostAddress );
        XboxSystemLinkAbortTravelCleanup( "build travel url failed" );
        return;
    }

    INT LocalReadyPlayers = XboxSplitReadyJoinedCount();
    XboxSplitResetRuntime( Client, bHost ? "SystemLinkHostLaunch" : "SystemLinkClientLaunch" );
    GXboxSplitUseReadySlots = 1;
    GXboxSplitPending = LocalReadyPlayers > 1;

    GXboxLog.Write( "XSL launch travel role=%s launch=0x%08X url=%s localReady=%d localPlayers=%d splitPending=%d readyMask=0x%X lockedMask=0x%X",
        TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
        GXboxSystemLink.LaunchId,
        TCHAR_TO_ANSI(URL),
        XboxSystemLinkLocalReadyCanConfirm() ? 1 : 0,
        LocalReadyPlayers,
        GXboxSplitPending ? 1 : 0,
        XboxSystemLinkReadyMask(),
        XboxSystemLinkLockedMask() );
    GXboxLog.Flush();

    XboxMenuClose( Viewport );
#if TARGET_XBOX
    if( bHost )
        XboxIpDrvClearSecureTravelHost();
    else if( SecureTravelHostPeer && SecureTravelHostPeer->HasSecureInfo && XboxSystemLinkIsXNetVirtualAddress(HostAddress) )
        XboxIpDrvSetSecureTravelHost( &SecureTravelHostPeer->XnAddr, &SecureTravelHostPeer->SessionKeyId, &SecureTravelHostPeer->SessionKey, HostAddress );
    else
        XboxIpDrvClearSecureTravelHost();
#endif
    XboxSystemLinkStopForTravel();
    Client->Engine->SetClientTravel( Viewport, URL, 0, TRAVEL_Absolute );
    GXboxLog.Write( "XSL launch SetClientTravel returned role=%s launch=0x%08X",
        TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
        GXboxSystemLink.LaunchId );
    GXboxLog.Flush();
    if( bHost && XboxSystemLinkSmokeEnabled() && GXboxSystemLinkSmokeTravelHoldSeconds > 0.0f )
    {
        GXboxLog.Write( "XSL SMOKE host post-SetClientTravel hold %.1fs launch=0x%08X",
            GXboxSystemLinkSmokeTravelHoldSeconds,
            GXboxSystemLink.LaunchId );
        GXboxLog.Flush();
        Sleep( (DWORD)(GXboxSystemLinkSmokeTravelHoldSeconds * 1000.0f) );
        GXboxLog.Write( "XSL SMOKE host post-SetClientTravel hold complete launch=0x%08X",
            GXboxSystemLink.LaunchId );
        GXboxLog.Flush();
    }
}

static UBOOL XboxSystemLinkScheduleHostLaunch()
{
    if( GXboxSystemLink.Role != XSLR_Host )
        return 0;
    if( !XboxSystemLinkAllMachinesConfirmed() )
    {
        GXboxLog.Write( "XSL host launch blocked confirmed=%d/%d localReady=%d",
            XboxSystemLinkConfirmedMachineCount(), XboxSystemLinkGroupMachineCount(), XboxSystemLinkLocalReadyCanConfirm() ? 1 : 0 );
        return 0;
    }

    if( !XboxSystemLinkLocalReadyCanConfirm() )
    {
        GXboxLog.Write( "XSL host launch blocked: local player not joined and locked" );
        return 0;
    }

    if( !XboxSystemLinkEnsureHostSession() || !XboxSystemLinkUpdateLocalXnAddr() )
    {
        GXboxLog.Write( "XSL host launch blocked: secure session not ready xnaddr=0x%08X",
            GXboxSystemLink.LocalXnAddrStatus );
        return 0;
    }

    XboxSystemLinkUpdateLocalAdvertisement();
    GXboxSystemLink.Phase = XSLP_Launching;
    GXboxSystemLink.LaunchId++;
    if( !GXboxSystemLink.LaunchId )
        GXboxSystemLink.LaunchId = 1;
    GXboxSystemLink.PendingTravel = 1;
    GXboxSystemLink.PendingTravelTime = appSeconds() + GXboxSystemLinkLaunchHostDelaySeconds;
    GXboxSystemLink.PendingTravelDeadline = appSeconds() + GXboxSystemLinkLaunchDeadlineSeconds;
    GXboxSystemLink.PendingHostAddress = 0;
    GXboxSystemLink.LaunchAckId = 0;
    GXboxLog.Write( "XSL host launch scheduled launch=0x%08X mapGame=%d map=%d fragIdx=%d timeIdx=%d skill=%d machines=%d",
        GXboxSystemLink.LaunchId,
        GXboxSystemLink.GameType,
        GXboxSystemLink.MapIndex,
        GXboxSystemLink.FragLimitIndex,
        GXboxSystemLink.TimeLimitIndex,
        GXboxSystemLink.SkillIndex,
        XboxSystemLinkGroupMachineCount() );
    XboxSystemLinkSendProbe();
    return 1;
}

static void XboxSystemLinkCheckRemoteLaunch( FLOAT Now )
{
    if( GXboxSystemLink.Role != XSLR_Client || !GXboxSystemLink.HostId )
        return;

    const FXboxSystemLinkPeer* HostPeer = XboxSystemLinkFindPeerById( GXboxSystemLink.HostId );
    if( !HostPeer )
        return;

    if( HostPeer->Phase == XSLP_MapSelect )
    {
        GXboxSystemLink.GameType = HostPeer->GameType;
        GXboxSystemLink.MapIndex = HostPeer->MapIndex;
        GXboxSystemLink.FragLimitIndex = HostPeer->FragLimitIndex;
        GXboxSystemLink.TimeLimitIndex = HostPeer->TimeLimitIndex;
        GXboxSystemLink.SkillIndex = HostPeer->SkillIndex;
    }

    if( HostPeer->Phase != XSLP_Launching || !HostPeer->LaunchId || HostPeer->LaunchId == GXboxSystemLink.LastSeenLaunchId )
        return;

    DWORD SecureHostAddress = 0;
    if( !XboxSystemLinkResolveHostSecureAddress( HostPeer, &SecureHostAddress ) )
    {
        GXboxLog.Write( "XSL client launch waiting: secure host session unavailable host=0x%08X secure=%d",
            HostPeer->Id, HostPeer->HasSecureInfo ? 1 : 0 );
        return;
    }

    UBOOL bUsingSecureTravelAddress = 0;
    DWORD TravelHostAddress = XboxSystemLinkSelectClientTravelAddress( HostPeer, SecureHostAddress, &bUsingSecureTravelAddress );
    if( bUsingSecureTravelAddress )
    {
        XboxSystemLinkSecureAddressConnected( TravelHostAddress, "client launch", Now );
    }

    if( GXboxSystemLink.LaunchId != HostPeer->LaunchId )
    {
        GXboxSystemLink.GameType = HostPeer->GameType;
        GXboxSystemLink.MapIndex = HostPeer->MapIndex;
        GXboxSystemLink.FragLimitIndex = HostPeer->FragLimitIndex;
        GXboxSystemLink.TimeLimitIndex = HostPeer->TimeLimitIndex;
        GXboxSystemLink.SkillIndex = HostPeer->SkillIndex;
        GXboxSystemLink.LaunchId = HostPeer->LaunchId;
        GXboxSystemLink.PendingHostAddress = TravelHostAddress;
        GXboxSystemLink.PendingTravel = 0;
        GXboxSystemLink.PendingTravelTime = 0.0f;
        GXboxSystemLink.PendingTravelDeadline = 0.0f;
        GXboxSystemLink.LaunchAckId = HostPeer->LaunchId;
        GXboxSystemLink.Phase = XSLP_Launching;
        GXboxLog.Write( "XSL client launch acked launch=0x%08X host=0x%08X travelAddr=0x%08X secureAddr=0x%08X directAddr=0x%08X usingSecure=%d",
            GXboxSystemLink.LaunchId,
            GXboxSystemLink.HostId,
            GXboxSystemLink.PendingHostAddress,
            SecureHostAddress,
            HostPeer->Address,
            bUsingSecureTravelAddress ? 1 : 0 );
        XboxSystemLinkSendProbe();
    }

    if( HostPeer->LaunchAckId != HostPeer->LaunchId || GXboxSystemLink.PendingTravel )
        return;

    GXboxSystemLink.GameType = HostPeer->GameType;
    GXboxSystemLink.MapIndex = HostPeer->MapIndex;
    GXboxSystemLink.FragLimitIndex = HostPeer->FragLimitIndex;
    GXboxSystemLink.TimeLimitIndex = HostPeer->TimeLimitIndex;
    GXboxSystemLink.SkillIndex = HostPeer->SkillIndex;
    GXboxSystemLink.PendingHostAddress = TravelHostAddress;
    GXboxSystemLink.PendingTravel = 1;
    GXboxSystemLink.PendingTravelTime = Now + GXboxSystemLinkLaunchClientDelaySeconds;
    GXboxSystemLink.LastSeenLaunchId = HostPeer->LaunchId;
    GXboxLog.Write( "XSL client launch commit received launch=0x%08X host=0x%08X travelAddr=0x%08X secureAddr=0x%08X directAddr=0x%08X usingSecure=%d delay=%.1f",
        GXboxSystemLink.LaunchId,
        GXboxSystemLink.HostId,
        GXboxSystemLink.PendingHostAddress,
        SecureHostAddress,
        HostPeer->Address,
        bUsingSecureTravelAddress ? 1 : 0,
        GXboxSystemLinkLaunchClientDelaySeconds );
    XboxSystemLinkSendProbe();
}

static void XboxSystemLinkUpdateProgress( UXboxViewport* Viewport, FLOAT Now )
{
    XboxSystemLinkUpdateLocalAdvertisement();

    if( GXboxSystemLink.Role == XSLR_Host
    &&  GXboxMenu.Screen == XMS_SystemLink
    &&  XboxSystemLinkAllMachinesConfirmed() )
    {
        GXboxMenu.Screen = XMS_SystemLinkMapSelect;
        GXboxMenu.SplitFocus = 0;
        GXboxSystemLink.Phase = XSLP_MapSelect;
        GXboxLog.Write( "XSL all machines confirmed -> host map select machines=%d", XboxSystemLinkGroupMachineCount() );
    }
    else if( GXboxSystemLink.Role == XSLR_Host
    &&  GXboxMenu.Screen == XMS_SystemLinkMapSelect
    &&  GXboxSystemLink.Phase != XSLP_Launching
    &&  !XboxSystemLinkAllMachinesConfirmed() )
    {
        GXboxMenu.Screen = XMS_SystemLink;
        GXboxSystemLink.Phase = XSLP_Ready;
        GXboxLog.Write( "XSL host map select cancelled: machines confirmed=%d/%d",
            XboxSystemLinkConfirmedMachineCount(), XboxSystemLinkGroupMachineCount() );
    }

    XboxSystemLinkCheckRemoteLaunch( Now );

    if( GXboxSystemLink.PendingTravel
    &&  GXboxSystemLink.Role == XSLR_Host
    &&  GXboxSystemLink.Phase == XSLP_Launching
    &&  Now >= GXboxSystemLink.PendingTravelTime )
    {
        UBOOL bAcksReady = XboxSystemLinkAllLaunchAcksReceived();
        if( GXboxSystemLink.LaunchAckId == GXboxSystemLink.LaunchId )
        {
            if( XboxSystemLinkSmokeEnabled() && GXboxSystemLinkSmokeTravelHoldSeconds > 0.0f && !GXboxSystemLinkSmokeHostPreTravelHoldDone )
            {
                GXboxSystemLinkSmokeHostPreTravelHoldDone = 1;
                GXboxSystemLink.PendingTravelTime = Now + GXboxSystemLinkSmokeTravelHoldSeconds;
                GXboxLog.Write( "XSL SMOKE host pre-travel hold %.1fs launch=0x%08X",
                    GXboxSystemLinkSmokeTravelHoldSeconds,
                    GXboxSystemLink.LaunchId );
                XboxSystemLinkSendProbe();
                return;
            }
            GXboxLog.Write( "XSL host launch travel after commit launch=0x%08X",
                GXboxSystemLink.LaunchId );
            XboxSystemLinkStartTravel( Viewport );
        }
        else if( bAcksReady )
        {
            GXboxSystemLink.LaunchAckId = GXboxSystemLink.LaunchId;
            GXboxSystemLink.PendingTravelTime = Now + GXboxSystemLinkLaunchCommitDelaySeconds;
            GXboxSystemLink.PendingTravelDeadline = Now + GXboxSystemLinkLaunchDeadlineSeconds;
            GXboxLog.Write( "XSL host launch commit announced launch=0x%08X delay=%.1f",
                GXboxSystemLink.LaunchId,
                GXboxSystemLinkLaunchCommitDelaySeconds );
            XboxSystemLinkSendProbe();
        }
        else if( Now >= GXboxSystemLink.PendingTravelDeadline )
        {
            GXboxLog.Write( "XSL host launch waiting for secure acks launch=0x%08X machines=%d confirmed=%d",
                GXboxSystemLink.LaunchId,
                XboxSystemLinkGroupMachineCount(),
                XboxSystemLinkConfirmedMachineCount() );
            for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
            {
                const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
                if( !XboxSystemLinkPeerInGroup(Peer) || Peer.Id == GXboxSystemLink.LocalId )
                    continue;
                if( Peer.Confirmed && Peer.ReadyMask && ((Peer.ReadyMask & Peer.LockedMask) == Peer.ReadyMask) )
                {
                    GXboxLog.Write( "XSL launch wait peer id=0x%08X phase=%s ack=0x%08X need=0x%08X verified=%d ready=0x%X locked=0x%X addr=0x%08X port=%d secure=%d",
                        Peer.Id,
                        TCHAR_TO_ANSI(XboxSystemLinkPhaseText(Peer.Phase)),
                        Peer.LaunchAckId,
                        GXboxSystemLink.LaunchId,
                        Peer.VerifiedSecurePeer ? 1 : 0,
                        Peer.ReadyMask,
                        Peer.LockedMask,
                        Peer.Address,
                        Peer.Port,
                        Peer.HasSecureInfo ? 1 : 0 );
                }
            }
            GXboxSystemLink.PendingTravelDeadline = Now + GXboxSystemLinkLaunchDeadlineSeconds;
            XboxSystemLinkSendProbe();
        }
        return;
    }

    if( GXboxSystemLink.PendingTravel && Now >= GXboxSystemLink.PendingTravelTime )
        XboxSystemLinkStartTravel( Viewport );
}

static void XboxSystemLinkTick( UXboxViewport* Viewport )
{
    XboxSystemLinkEnsureState();
    if( GXboxMenu.Screen != XMS_SystemLink && GXboxMenu.Screen != XMS_SystemLinkMapSelect )
        return;

    // Proof screens must remain deterministic and never touch live network state.
    if( XboxIsSystemLinkProofRequest(XboxFullMenuProofRequested()) )
        return;

    if( !GXboxSystemLink.Started )
        XboxSystemLinkStart( Viewport );
    if( !GXboxSystemLink.Started )
        return;

    FLOAT Now = appSeconds();
    XboxSystemLinkExpirePeers( Now );
    XboxSystemLinkUpdateElection( Now );

    if( Now - GXboxSystemLink.LastSendTime >= 0.5f )
    {
        XboxSystemLinkSendProbe();
        GXboxSystemLink.LastSendTime = Now;
    }

    for( ;; )
    {
        char Buffer[512];
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
        INT Role = XSLR_Seeking;
        DWORD HostId = 0;
        INT Phase = XSLP_Discovery;
        INT ReadyMask = 0;
        INT LockedMask = 0;
        INT Confirmed = 0;
        INT GameType = 0;
        INT MapIndex = 0;
        INT FragLimitIndex = 0;
        INT TimeLimitIndex = 0;
        INT SkillIndex = 0;
        DWORD LaunchId = 0;
        DWORD LaunchAckId = 0;
        INT SecureInfo = 0;
        char XnAddrHex[sizeof(XNADDR)*2 + 1];
        char KeyIdHex[sizeof(XNKID)*2 + 1];
        char KeyHex[sizeof(XNKEY)*2 + 1];
        appMemzero( XnAddrHex, sizeof(XnAddrHex) );
        appMemzero( KeyIdHex, sizeof(KeyIdHex) );
        appMemzero( KeyHex, sizeof(KeyHex) );
        XNADDR PacketXnAddr;
        XNKID PacketKeyId;
        XNKEY PacketKey;
        appMemzero( &PacketXnAddr, sizeof(PacketXnAddr) );
        appMemzero( &PacketKeyId, sizeof(PacketKeyId) );
        appMemzero( &PacketKey, sizeof(PacketKey) );

        if( sscanf( Buffer, "UTXSL4|%08X|%d|%lu|%d|%08X|%d|%d|%d|%d|%d|%d|%d|%d|%d|%08X|%08X|%d|%72s|%16s|%32s",
            &Id, &Port, &Counter, &Role, &HostId, &Phase, &ReadyMask, &LockedMask, &Confirmed,
            &GameType, &MapIndex, &FragLimitIndex, &TimeLimitIndex, &SkillIndex, &LaunchId,
            &LaunchAckId, &SecureInfo, XnAddrHex, KeyIdHex, KeyHex ) == 20 )
        {
            UBOOL bDecodedSecure = SecureInfo
                && XboxSystemLinkHexDecode( XnAddrHex, (BYTE*)&PacketXnAddr, sizeof(PacketXnAddr) )
                && XboxSystemLinkHexDecode( KeyIdHex, (BYTE*)&PacketKeyId, sizeof(PacketKeyId) )
                && XboxSystemLinkHexDecode( KeyHex, (BYTE*)&PacketKey, sizeof(PacketKey) );
            XboxSystemLinkRecordPeer( Id, From.sin_addr.s_addr, Port, Role, HostId, Phase, ReadyMask, LockedMask, Confirmed, GameType, MapIndex, FragLimitIndex, TimeLimitIndex, SkillIndex, LaunchId, LaunchAckId, bDecodedSecure, &PacketXnAddr, &PacketKeyId, &PacketKey, Now );
        }
        else if( sscanf( Buffer, "UTXSL3|%08X|%d|%lu|%d|%08X|%d|%d|%d|%d|%d|%d|%d|%d|%d|%08X",
            &Id, &Port, &Counter, &Role, &HostId, &Phase, &ReadyMask, &LockedMask, &Confirmed,
            &GameType, &MapIndex, &FragLimitIndex, &TimeLimitIndex, &SkillIndex, &LaunchId ) == 15 )
            XboxSystemLinkRecordPeer( Id, From.sin_addr.s_addr, Port, Role, HostId, Phase, ReadyMask, LockedMask, Confirmed, GameType, MapIndex, FragLimitIndex, TimeLimitIndex, SkillIndex, LaunchId, 0, 0, NULL, NULL, NULL, Now );
        else if( sscanf( Buffer, "UTXSL2|%08X|%d|%lu|%d|%08X", &Id, &Port, &Counter, &Role, &HostId ) == 5 )
            XboxSystemLinkRecordPeer( Id, From.sin_addr.s_addr, Port, Role, HostId, XSLP_Discovery, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, Now );
        else if( sscanf( Buffer, "UTXSL1|%08X|%d|%lu", &Id, &Port, &Counter ) == 3 )
            XboxSystemLinkRecordPeer( Id, From.sin_addr.s_addr, Port, XSLR_Seeking, 0, XSLP_Discovery, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, Now );
    }

    XboxSystemLinkUpdateElection( Now );
    XboxSystemLinkUpdateProgress( Viewport, Now );

    if( Now - GXboxSystemLink.LastLogTime >= 5.0f )
    {
        GXboxSystemLink.LastLogTime = Now;
        GXboxLog.Write( "XSL lobby status id=0x%08X role=%s phase=%s host=0x%08X port=%d peers=%d machines=%d confirmed=%d sent=%lu lastErr=%d",
            GXboxSystemLink.LocalId,
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
            TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
            GXboxSystemLink.HostId,
            GXboxSystemLink.LocalPort,
            GXboxSystemLink.Peers.Num(),
            XboxSystemLinkGroupMachineCount(),
            XboxSystemLinkConfirmedMachineCount(),
            GXboxSystemLink.SendCounter,
            GXboxSystemLink.LastError );
    }
}

static void XboxMenuEnsureRegistryCache()
{
#if TARGET_XBOX
    GXboxMenuRegistryCacheRefreshed = 1;
    return;
#endif
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

static UBOOL XboxMenuRootPreviewObject( UObject* Object )
{
    if( !Object )
        return 1;

    for( INT i=0; i<ARRAY_COUNT(GXboxPlayerPreviewRootRefs); i++ )
    {
        if( GXboxPlayerPreviewRootRefs[i].Object == Object )
        {
            GXboxPlayerPreviewRootRefs[i].Count++;
            return 1;
        }
    }
    for( INT j=0; j<ARRAY_COUNT(GXboxPlayerPreviewRootRefs); j++ )
    {
        if( !GXboxPlayerPreviewRootRefs[j].Object )
        {
            GXboxPlayerPreviewRootRefs[j].Object = Object;
            GXboxPlayerPreviewRootRefs[j].Count = 1;
            Object->AddToRoot();
            return 1;
        }
    }
    GXboxLog.Write( "XMENU preview root table full for %s", TCHAR_TO_ANSI(Object->GetFullName()) );
    return 0;
}

static void XboxMenuUnrootPreviewObject( UObject* Object )
{
    if( Object )
    {
        for( INT i=0; i<ARRAY_COUNT(GXboxPlayerPreviewRootRefs); i++ )
        {
            if( GXboxPlayerPreviewRootRefs[i].Object == Object )
            {
                GXboxPlayerPreviewRootRefs[i].Count--;
                if( GXboxPlayerPreviewRootRefs[i].Count <= 0 )
                {
                    Object->RemoveFromRoot();
                    GXboxPlayerPreviewRootRefs[i].Object = NULL;
                    GXboxPlayerPreviewRootRefs[i].Count = 0;
                }
                return;
            }
        }
    }
}

static void XboxMenuInitPlayerPreviewAssets()
{
    if( GXboxPlayerPreviewAssetsInitialized )
        return;

    appMemzero( &GXboxPlayerPreviewCurrent, sizeof(GXboxPlayerPreviewCurrent) );
    appMemzero( &GXboxPlayerPreviewPrevious, sizeof(GXboxPlayerPreviewPrevious) );
    GXboxPlayerPreviewCurrent.ClassIndex = -1;
    GXboxPlayerPreviewCurrent.SkinIndex  = -1;
    GXboxPlayerPreviewCurrent.FaceIndex  = -1;
    GXboxPlayerPreviewCurrent.TeamIndex  = -1;
    GXboxPlayerPreviewPrevious.ClassIndex = -1;
    GXboxPlayerPreviewPrevious.SkinIndex  = -1;
    GXboxPlayerPreviewPrevious.FaceIndex  = -1;
    GXboxPlayerPreviewPrevious.TeamIndex  = -1;
    GXboxPlayerPreviewAssetsInitialized = 1;
}

static UBOOL XboxMenuRootPlayerPreviewAssets( FXboxPlayerPreviewAssets& Assets )
{
    if( !XboxMenuRootPreviewObject( Assets.Mesh ) )
        return 0;
    if( !XboxMenuRootPreviewObject( Assets.Skin ) )
        return 0;
    for( INT i=0; i<ARRAY_COUNT(Assets.MultiSkins); i++ )
        if( !XboxMenuRootPreviewObject( Assets.MultiSkins[i] ) )
            return 0;
    return 1;
}

static void XboxMenuUnrootPlayerPreviewAssets( FXboxPlayerPreviewAssets& Assets )
{
    XboxMenuUnrootPreviewObject( Assets.Mesh );
    XboxMenuUnrootPreviewObject( Assets.Skin );
    for( INT i=0; i<ARRAY_COUNT(Assets.MultiSkins); i++ )
        XboxMenuUnrootPreviewObject( Assets.MultiSkins[i] );
    appMemzero( &Assets, sizeof(Assets) );
    Assets.ClassIndex = -1;
    Assets.SkinIndex  = -1;
    Assets.FaceIndex  = -1;
    Assets.TeamIndex  = -1;
}

static UBOOL XboxMenuPreviewAssetsMatch( const FXboxPlayerPreviewAssets& Assets, INT ClassIndex, INT SkinIndex, INT FaceIndex, INT TeamIndex )
{
    return Assets.ClassIndex == ClassIndex
        && Assets.SkinIndex  == SkinIndex
        && Assets.FaceIndex  == FaceIndex
        && Assets.TeamIndex  == TeamIndex;
}

static UBOOL XboxMenuTrackPlayerPreviewAssets( AActor* Actor, INT ClassIndex, INT SkinIndex, INT FaceIndex, INT TeamIndex )
{
    if( !Actor )
        return 0;

    XboxMenuInitPlayerPreviewAssets();
    if( XboxMenuPreviewAssetsMatch( GXboxPlayerPreviewCurrent, ClassIndex, SkinIndex, FaceIndex, TeamIndex ) )
        return 0;

    XboxMenuUnrootPlayerPreviewAssets( GXboxPlayerPreviewPrevious );
    GXboxPlayerPreviewPrevious = GXboxPlayerPreviewCurrent;
    appMemzero( &GXboxPlayerPreviewCurrent, sizeof(GXboxPlayerPreviewCurrent) );
    GXboxPlayerPreviewCurrent.ClassIndex  = ClassIndex;
    GXboxPlayerPreviewCurrent.SkinIndex   = SkinIndex;
    GXboxPlayerPreviewCurrent.FaceIndex   = FaceIndex;
    GXboxPlayerPreviewCurrent.TeamIndex   = TeamIndex;
    GXboxPlayerPreviewCurrent.Mesh        = Actor->Mesh;
    GXboxPlayerPreviewCurrent.Skin        = Actor->Skin;
    for( INT i=0; i<ARRAY_COUNT(GXboxPlayerPreviewCurrent.MultiSkins); i++ )
        GXboxPlayerPreviewCurrent.MultiSkins[i] = Actor->MultiSkins[i];

    if( !XboxMenuRootPlayerPreviewAssets( GXboxPlayerPreviewCurrent ) )
    {
        FXboxPlayerPreviewAssets Desired = GXboxPlayerPreviewCurrent;
        DWORD BeforeKB = XboxMenuAvailPhysKB();
        GXboxLog.Write( "XMENU preview root recovery begin availKB=%u", (unsigned)BeforeKB );
        XboxMenuUnrootPlayerPreviewAssets( GXboxPlayerPreviewCurrent );
        XboxMenuUnrootPlayerPreviewAssets( GXboxPlayerPreviewPrevious );
        UObject::CollectGarbage( RF_Native );
        GXboxPlayerPreviewCurrent = Desired;
        if( !XboxMenuRootPlayerPreviewAssets( GXboxPlayerPreviewCurrent ) )
        {
            XboxMenuUnrootPlayerPreviewAssets( GXboxPlayerPreviewCurrent );
            GXboxLog.Write( "XMENU preview root recovery failed availKB=%u", (unsigned)XboxMenuAvailPhysKB() );
            return 0;
        }
        GXboxLog.Write( "XMENU preview root recovery end availKB=%u", (unsigned)XboxMenuAvailPhysKB() );
    }
    GXboxLog.Write( "XMENU preview assets guarded current=%d/%d/%d/%d previous=%d/%d/%d/%d",
        GXboxPlayerPreviewCurrent.ClassIndex, GXboxPlayerPreviewCurrent.SkinIndex,
        GXboxPlayerPreviewCurrent.FaceIndex, GXboxPlayerPreviewCurrent.TeamIndex,
        GXboxPlayerPreviewPrevious.ClassIndex, GXboxPlayerPreviewPrevious.SkinIndex,
        GXboxPlayerPreviewPrevious.FaceIndex, GXboxPlayerPreviewPrevious.TeamIndex );
    return 1;
}

static void XboxMenuMaybeCollectPlayerPreviewGarbage( UXboxViewport* Viewport, const char* Reason )
{
    GXboxPlayerPreviewChangesSinceGC++;
    DWORD BeforeKB = XboxMenuAvailPhysKB();
    if( GXboxPlayerPreviewChangesSinceGC < 4 && BeforeKB >= 12288 )
        return;

    GXboxLog.Write( "XMENU preview gc begin reason=%s changes=%d availKB=%u", Reason ? Reason : "preview", GXboxPlayerPreviewChangesSinceGC, (unsigned)BeforeKB );
    if( Viewport && Viewport->RenDev )
        Viewport->RenDev->Flush( 0 );
    UObject::CollectGarbage( RF_Native );
    GXboxPlayerPreviewChangesSinceGC = 0;
    GXboxLog.Write( "XMENU preview gc end availKB=%u", (unsigned)XboxMenuAvailPhysKB() );
}

static void XboxMenuReleasePlayerPreviewAssets()
{
    XboxMenuInitPlayerPreviewAssets();
    XboxMenuUnrootPlayerPreviewAssets( GXboxPlayerPreviewCurrent );
    XboxMenuUnrootPlayerPreviewAssets( GXboxPlayerPreviewPrevious );
    GXboxPlayerPreviewChangesSinceGC = 0;
}

static void XboxMenuClearPlayerPreviewActorRefs( AActor* Actor )
{
    if( !Actor )
        return;

    Actor->Mesh = NULL;
    Actor->Skin = NULL;
    for( INT i=0; i<ARRAY_COUNT(Actor->MultiSkins); i++ )
        Actor->MultiSkins[i] = NULL;
}

static void XboxMenuPreparePlayerPreviewClassSwitch( UXboxViewport* Viewport, AActor* Actor )
{
    XboxMenuClearPlayerPreviewActorRefs( Actor );
    XboxMenuReleasePlayerPreviewAssets();
    XboxMenuResetPlayerPreviewCache();

    if( Actor && Viewport && Viewport->Actor )
    {
        GXboxPlayerPreviewActor = Actor;
        GXboxPlayerPreviewLevel = Viewport->Actor->XLevel;
    }

    if( Viewport && Viewport->RenDev )
        Viewport->RenDev->Flush( 0 );
}

static void XboxMenuDestroyPlayerPreview()
{
    XboxMenuReleasePlayerPreviewAssets();
    if( GXboxPlayerPreviewActor )
        GXboxPlayerPreviewActor->Destroy();
    XboxMenuResetPlayerPreviewCache();
}

static void XboxMenuReleaseMapPreviewTexture();
static const char* XboxMenuCurrentPlayerPortraitName();
static const char* XboxMenuCurrentPlayerPortraitNameRaw();
static void XboxMenuReleaseCurrentPlayerPortrait();
static void XboxMenuReleaseFrontendTransientAssets( const char* Reason, UBOOL bReleaseRenderTextures );

extern "C" void XboxMenuPreClientTravelCleanup()
{
    XboxMenuReleaseFrontendTransientAssets( "pre-travel", 1 );
}

static UTexture* GXboxMenuPreviewTexture = NULL;
static TCHAR     GXboxMenuPreviewMap[64] = TEXT("");

static void XboxMenuReleaseMapPreviewTexture()
{
    if( GXboxMenuPreviewTexture )
    {
        GXboxLog.Write( "XMENU map preview released %s availKB=%u", TCHAR_TO_ANSI(GXboxMenuPreviewMap), (unsigned)XboxMenuAvailPhysKB() );
        GXboxMenuPreviewTexture->RemoveFromRoot();
    }
    GXboxMenuPreviewTexture = NULL;
    GXboxMenuPreviewMap[0] = 0;
}

static void XboxMenuLogResourceBuckets( const char* Reason )
{
    INT MenuTextureCount = 0;
    INT MenuTextureKB = 0;
    INT MenuTextureFailures = 0;
    XboxRenderGetMenuTextureStats( &MenuTextureCount, &MenuTextureKB, &MenuTextureFailures );

    INT PreviewRootCount = 0;
    INT PreviewRootRefs = 0;
    for( INT i=0; i<ARRAY_COUNT(GXboxPlayerPreviewRootRefs); i++ )
    {
        if( GXboxPlayerPreviewRootRefs[i].Object )
        {
            PreviewRootCount++;
            PreviewRootRefs += GXboxPlayerPreviewRootRefs[i].Count;
        }
    }

    INT WheelIcons = 0;
    INT WheelMeshes = 0;
    for( INT j=0; j<ARRAY_COUNT(GXboxWeaponWheelSlots); j++ )
    {
        if( GXboxWeaponWheelSlots[j].Icon )
            WheelIcons++;
        if( GXboxWeaponWheelSlots[j].PickupMesh )
            WheelMeshes++;
    }

    GXboxLog.Write(
        "XBUCKET %s availKB=%u heapLiveKB=%u heapPeakKB=%u heapTotalKB=%u menuTex=%d/%dKB fail=%d mapPreview=%d portrait=%s previewActor=%d roots=%d refs=%d wheelIcons=%d wheelMeshes=%d lists=gt%d maps%d chars%d int%d",
        Reason ? Reason : "unknown",
        (unsigned)XboxMenuAvailPhysKB(),
        (unsigned)(GXboxMallocLiveBytes / 1024),
        (unsigned)(GXboxMallocPeakBytes / 1024),
        (unsigned)(GXboxMallocTotalBytes / 1024),
        MenuTextureCount,
        MenuTextureKB,
        MenuTextureFailures,
        GXboxMenuPreviewTexture ? 1 : 0,
        XboxMenuCurrentPlayerPortraitNameRaw(),
        GXboxPlayerPreviewActor ? 1 : 0,
        PreviewRootCount,
        PreviewRootRefs,
        WheelIcons,
        WheelMeshes,
        GXboxDiscoveredGameTypes.Num(),
        GXboxDiscoveredMaps.Num(),
        GXboxPlayerClasses.Num(),
        GXboxMenuIntObjectCache.Num()
    );
}

static void XboxMenuReleaseFrontendTransientAssets( const char* Reason, UBOOL bReleaseRenderTextures )
{
    DWORD BeforeKB = XboxMenuAvailPhysKB();
    XboxMenuLogResourceBuckets( Reason ? Reason : "cleanup pre" );
    XboxMenuReleaseCurrentPlayerPortrait();
    XboxMenuDestroyPlayerPreview();
    XboxMenuReleaseMapPreviewTexture();
    XboxWeaponWheelReleaseCache();
    if( bReleaseRenderTextures )
        XboxRenderReleaseMenuTextures();
    GXboxLog.Write( "XMENU frontend cleanup reason=%s render=%d availKB=%u->%u", Reason ? Reason : "unknown", bReleaseRenderTextures ? 1 : 0, (unsigned)BeforeKB, (unsigned)XboxMenuAvailPhysKB() );
    XboxMenuLogResourceBuckets( Reason ? Reason : "cleanup post" );
}

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

static UBOOL XboxMenuObjectHasPrefix( const FString& Candidate, const TCHAR* WantedPrefix )
{
    if( !WantedPrefix || !WantedPrefix[0] )
        return 1;
    return appStrnicmp( *Candidate, WantedPrefix, appStrlen(WantedPrefix) ) == 0;
}

static void XboxMenuLoadIntObjectCache()
{
    if( GXboxMenuIntObjectCacheLoaded )
        return;

    GXboxMenuIntObjectCacheLoaded = 1;
    GXboxMenuIntObjectCache.Empty();
#if TARGET_XBOX
    GXboxLog.Write( "XMENU .int cache skipped on Xbox" );
    return;
#endif
    if( !GSys || !GConfig )
        return;

    TCHAR Buffer[32767];
    INT LoggedSearches = 0;
    INT LoggedFiles = 0;
    INT ParsedObjects = 0;
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
                if( Info.Object.Len() == 0 || Info.Class.Len() == 0 )
                    continue;

                UBOOL bDuplicate = 0;
                for( INT Existing=0; Existing<GXboxMenuIntObjectCache.Num(); Existing++ )
                    if( appStricmp( *GXboxMenuIntObjectCache(Existing).Object, *Info.Object ) == 0 )
                        bDuplicate = 1;
                if( !bDuplicate )
                    new(GXboxMenuIntObjectCache)FRegistryObjectInfo(Info);
            }
        }
    }

    GXboxLog.Write( "XMENU .int cache parsed=%d cached=%d", ParsedObjects, GXboxMenuIntObjectCache.Num() );
}

static void XboxMenuCollectIntObjects( TArray<FRegistryObjectInfo>& Out, const TCHAR* WantedClass, const TCHAR* WantedMetaClass, const TCHAR* WantedObjectPrefix=NULL )
{
    Out.Empty();
    XboxMenuLoadIntObjectCache();

    INT ClassFiltered = 0;
    INT MetaFiltered = 0;
    INT PrefixFiltered = 0;
    for( INT i=0; i<GXboxMenuIntObjectCache.Num(); i++ )
    {
        const FRegistryObjectInfo& Info = GXboxMenuIntObjectCache(i);
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
        if( !XboxMenuObjectHasPrefix( Info.Object, WantedObjectPrefix ) )
        {
            PrefixFiltered++;
            continue;
        }

        UBOOL bDuplicate = 0;
        for( INT Existing=0; Existing<Out.Num(); Existing++ )
            if( appStricmp( *Out(Existing).Object, *Info.Object ) == 0 )
                bDuplicate = 1;
        if( !bDuplicate )
            new(Out)FRegistryObjectInfo(Info);
    }

    GXboxLog.Write( "XMENU .int filter class=%s meta=%s prefix=%s cached=%d classFiltered=%d metaFiltered=%d prefixFiltered=%d count=%d",
        WantedClass ? TCHAR_TO_ANSI(WantedClass) : "",
        WantedMetaClass ? TCHAR_TO_ANSI(WantedMetaClass) : "",
        WantedObjectPrefix ? TCHAR_TO_ANSI(WantedObjectPrefix) : "",
        GXboxMenuIntObjectCache.Num(),
        ClassFiltered,
        MetaFiltered,
        PrefixFiltered,
        Out.Num() );
}

static void XboxMenuAddFallbackGameType( const TCHAR* Label, const TCHAR* ClassName, const TCHAR* Prefix )
{
    FXboxDiscoveredOption& Option = *new(GXboxDiscoveredGameTypes)FXboxDiscoveredOption;
    Option.Label = Label;
    Option.URLValue = ClassName;
    Option.MapPrefix = Prefix;
}

static UBOOL XboxMenuPackageFileExists( const TCHAR* PackageFile )
{
    if( !PackageFile || !PackageFile[0] || !GFileManager )
        return 0;

    TCHAR Filename[256];
    appSprintf( Filename, TEXT("%s%s"), appBaseDir(), PackageFile );
    return GFileManager->FileSize( Filename ) >= 0;
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

#if TARGET_XBOX
    GXboxDiscoveredListsLoaded = 1;
    GXboxDiscoveredGameTypes.Empty();
    GXboxDiscoveredMutators.Empty();

    XboxMenuAddFallbackGameType( TEXT("DEATHMATCH"), TEXT("Botpack.DeathMatchPlus"), TEXT("DM") );
    XboxMenuAddFallbackGameType( TEXT("LAST MAN STANDING"), TEXT("Botpack.LastManStanding"), TEXT("DM") );
    XboxMenuAddFallbackGameType( TEXT("CAPTURE THE FLAG"), TEXT("Botpack.CTFGame"), TEXT("CTF") );
    XboxMenuAddFallbackGameType( TEXT("DOMINATION"), TEXT("Botpack.Domination"), TEXT("DOM") );
    XboxMenuAddFallbackGameType( TEXT("ASSAULT"), TEXT("Botpack.Assault"), TEXT("AS") );
    if( XboxMenuPackageFileExists( TEXT("JailBreak.u") ) )
        XboxMenuAddFallbackGameType( TEXT("JAILBREAK"), TEXT("JailBreak.JailBreak"), TEXT("JB") );
    XboxMenuAddFallbackMutator( TEXT("LOW GRAVITY"), TEXT("Botpack.LowGrav") );
    XboxMenuAddFallbackMutator( TEXT("INSTAGIB"), TEXT("Botpack.InstaGibDM") );
    XboxMenuAddFallbackMutator( TEXT("NO POWERUPS"), TEXT("Botpack.NoPowerups") );
    XboxMenuAddFallbackMutator( TEXT("PULSE ARENA"), TEXT("Botpack.PulseArena") );
    XboxMenuAddFallbackMutator( TEXT("FLAK ARENA"), TEXT("Botpack.FlakArena") );
    XboxMenuAddFallbackMutator( TEXT("ROCKET ARENA"), TEXT("Botpack.RocketArena") );
    XboxMenuAddFallbackMutator( TEXT("SHOCK ARENA"), TEXT("Botpack.ShockArena") );
    XboxMenuAddFallbackMutator( TEXT("SNIPER ARENA"), TEXT("Botpack.SniperArena") );
    XboxMenuAddFallbackMutator( TEXT("CHAINSAW MELEE"), TEXT("Botpack.ChainsawMelee") );
    XboxMenuAddFallbackMutator( TEXT("NO REDEEMER"), TEXT("Botpack.NoRedeemer") );
    XboxMenuAddFallbackMutator( TEXT("STEALTH"), TEXT("Botpack.Stealth") );
    XboxMenuAddFallbackMutator( TEXT("FATBOY"), TEXT("Botpack.FatBoy") );
    XboxMenuAddFallbackMutator( TEXT("INSTANT ROCKETS"), TEXT("Botpack.InstantRockets") );
    XboxMenuAddFallbackMutator( TEXT("JUMP MATCH"), TEXT("Botpack.JumpMatch") );
    if( XboxMenuPackageFileExists( TEXT("OLweapons.u") ) )
        XboxMenuAddFallbackMutator( TEXT("OLDSKOOL WEAPONS"), TEXT("olweapons.oldskool") );
    if( XboxMenuPackageFileExists( TEXT("AgentX.u") ) )
        XboxMenuAddFallbackMutator( TEXT("AGENTX ARENA"), TEXT("AgentX.AgentXArena") );
    if( XboxMenuPackageFileExists( TEXT("AkimboArena.u") ) )
        XboxMenuAddFallbackMutator( TEXT("AKIMBO ARENA"), TEXT("AkimboArena.AkimboArena") );
    if( XboxMenuPackageFileExists( TEXT("HaloUTXbox.u") ) )
        XboxMenuAddFallbackMutator( TEXT("HALOUT WEAPONS"), TEXT("HaloUTXbox.HaloWeapons") );

    GXboxLog.Write( "XMENU using fixed Xbox discovery list gameTypes=%d mutators=%d",
        GXboxDiscoveredGameTypes.Num(), GXboxDiscoveredMutators.Num() );
    return;
#endif

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
        XboxMenuAddFallbackGameType( TEXT("LAST MAN STANDING"), TEXT("Botpack.LastManStanding"), TEXT("DM") );
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

static INT XboxMenuFindMapIndexByFile( INT GameType, const TCHAR* MapFile )
{
    if( !MapFile || !MapFile[0] )
        return INDEX_NONE;

    XboxMenuLoadMapsForGameType( GameType );
    for( INT i=0; i<GXboxDiscoveredMaps.Num(); i++ )
        if( appStricmp( *GXboxDiscoveredMaps(i).URLValue, MapFile ) == 0 )
            return i;
    return INDEX_NONE;
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

    TCHAR PackageFilename[256];
    if( !appFindPackageFile(MapName, NULL, PackageFilename) )
    {
        GXboxLog.Write( "XMENU map preview %s skipped: package missing", TCHAR_TO_ANSI(MapName) );
        return NULL;
    }

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
    {
        GXboxMenu.InstantMutatorMask[Word] ^= Bit;
        if( GXboxMenu.InstantMutatorMask[Word] & Bit )
        {
            const TCHAR* SelectedClass = *GXboxDiscoveredMutators(Index).URLValue;
            const UBOOL bSelectedArena
                = appStricmp( SelectedClass, TEXT("Botpack.InstaGibDM") ) == 0
                || appStricmp( SelectedClass, TEXT("Botpack.PulseArena") ) == 0
                || appStricmp( SelectedClass, TEXT("Botpack.FlakArena") ) == 0
                || appStricmp( SelectedClass, TEXT("Botpack.RocketArena") ) == 0
                || appStricmp( SelectedClass, TEXT("Botpack.ShockArena") ) == 0
                || appStricmp( SelectedClass, TEXT("Botpack.SniperArena") ) == 0
                || appStricmp( SelectedClass, TEXT("olweapons.oldskool") ) == 0
                || appStricmp( SelectedClass, TEXT("AgentX.AgentXArena") ) == 0;
            const UBOOL bSelectedAkimbo = appStricmp( SelectedClass, TEXT("AkimboArena.AkimboArena") ) == 0;
            const UBOOL bSelectedChainsaw = appStricmp( SelectedClass, TEXT("Botpack.ChainsawMelee") ) == 0;
            if( bSelectedArena || bSelectedAkimbo || bSelectedChainsaw )
            {
                for( INT i=0; i<GXboxDiscoveredMutators.Num(); i++ )
                {
                    if( i == Index )
                        continue;
                    const TCHAR* OtherClass = *GXboxDiscoveredMutators(i).URLValue;
                    const UBOOL bOtherArena
                        = appStricmp( OtherClass, TEXT("Botpack.InstaGibDM") ) == 0
                        || appStricmp( OtherClass, TEXT("Botpack.PulseArena") ) == 0
                        || appStricmp( OtherClass, TEXT("Botpack.FlakArena") ) == 0
                        || appStricmp( OtherClass, TEXT("Botpack.RocketArena") ) == 0
                        || appStricmp( OtherClass, TEXT("Botpack.ShockArena") ) == 0
                        || appStricmp( OtherClass, TEXT("Botpack.SniperArena") ) == 0
                        || appStricmp( OtherClass, TEXT("olweapons.oldskool") ) == 0
                        || appStricmp( OtherClass, TEXT("AgentX.AgentXArena") ) == 0;
                    const UBOOL bOtherAkimbo = appStricmp( OtherClass, TEXT("AkimboArena.AkimboArena") ) == 0;
                    const UBOOL bOtherChainsaw = appStricmp( OtherClass, TEXT("Botpack.ChainsawMelee") ) == 0;
                    const UBOOL bConflict
                        = (bSelectedArena && (bOtherArena || bOtherAkimbo))
                        || (bSelectedAkimbo && (bOtherArena || bOtherChainsaw))
                        || (bSelectedChainsaw && bOtherAkimbo);
                    if( bConflict )
                        GXboxMenu.InstantMutatorMask[i >> 5] &= ~(1 << (i & 31));
                }
            }
        }
    }
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

static INT XboxControlClampAction( INT Action )
{
    return Clamp<INT>( Action, XCA_None, XCA_CenterView );
}

static INT XboxControlButtonAction( UXboxClient* Client, INT Button )
{
    if( !Client )
        return GXboxControlPresets[0].Actions[Clamp<INT>(Button, 0, XCB_Count-1)];

    switch( Button )
    {
        case XCB_A:            return XboxControlClampAction( Client->ButtonActionA );
        case XCB_B:            return XboxControlClampAction( Client->ButtonActionB );
        case XCB_X:            return XboxControlClampAction( Client->ButtonActionX );
        case XCB_Y:            return XboxControlClampAction( Client->ButtonActionY );
        case XCB_LeftTrigger:  return XboxControlClampAction( Client->ButtonActionLeftTrigger );
        case XCB_RightTrigger: return XboxControlClampAction( Client->ButtonActionRightTrigger );
        case XCB_White:        return XboxControlClampAction( Client->ButtonActionWhite );
        case XCB_Black:        return XboxControlClampAction( Client->ButtonActionBlack );
        case XCB_Back:         return XboxControlClampAction( Client->ButtonActionBack );
        case XCB_RightThumb:   return XboxControlClampAction( Client->ButtonActionRightThumb );
    }
    return XCA_None;
}

static void XboxControlSetButtonAction( UXboxClient* Client, INT Button, INT Action )
{
    if( !Client )
        return;

    Action = XboxControlClampAction( Action );
    switch( Button )
    {
        case XCB_A:            Client->ButtonActionA = Action; break;
        case XCB_B:            Client->ButtonActionB = Action; break;
        case XCB_X:            Client->ButtonActionX = Action; break;
        case XCB_Y:            Client->ButtonActionY = Action; break;
        case XCB_LeftTrigger:  Client->ButtonActionLeftTrigger = Action; break;
        case XCB_RightTrigger: Client->ButtonActionRightTrigger = Action; break;
        case XCB_White:        Client->ButtonActionWhite = Action; break;
        case XCB_Black:        Client->ButtonActionBlack = Action; break;
        case XCB_Back:         Client->ButtonActionBack = Action; break;
        case XCB_RightThumb:   Client->ButtonActionRightThumb = Action; break;
    }
}

static void XboxControlApplyPreset( UXboxClient* Client, INT PresetIndex )
{
    if( !Client )
        return;

    PresetIndex = XboxMenuWrapInt( PresetIndex, 0, ARRAY_COUNT(GXboxControlPresets) );
    const FXboxControlPreset& Preset = GXboxControlPresets[PresetIndex];
    for( INT i=0; i<XCB_Count; i++ )
        XboxControlSetButtonAction( Client, i, Preset.Actions[i] );
    Client->ControlPreset = PresetIndex;
    Client->ButtonLayout = Preset.LegacyButtonLayout;
    Client->StickLayout = XboxStickLayoutClamp( Preset.StickLayout );
    if( !GXboxSplitActive )
        Client->SaveConfig();
    GXboxLog.Write( "XMENU controls preset=%d label=%s stick=%d", PresetIndex, TCHAR_TO_ANSI(Preset.Label), Client->StickLayout );
}

static void XboxControlMarkCustom( UXboxClient* Client )
{
    if( !Client )
        return;
    Client->ControlPreset = -1;
    Client->ButtonLayout = 0;
}

static void XboxProfileSectionName( INT ProfileIndex, TCHAR* Out, INT OutCount )
{
    appSprintf( Out, TEXT("XboxProfile%i"), Clamp<INT>(ProfileIndex, 0, XBOX_PROFILE_COUNT-1) );
    Out[OutCount-1] = 0;
}

static INT XboxProfileConfigInt( const TCHAR* Section, const TCHAR* Key, INT Fallback )
{
    if( !GConfig )
        return Fallback;
    const TCHAR* Value = GConfig->GetStr( Section, Key, TEXT("User.ini") );
    return (Value && Value[0]) ? appAtoi(Value) : Fallback;
}

static FLOAT XboxProfileConfigFloat( const TCHAR* Section, const TCHAR* Key, FLOAT Fallback )
{
    if( !GConfig )
        return Fallback;
    const TCHAR* Value = GConfig->GetStr( Section, Key, TEXT("User.ini") );
    return (Value && Value[0]) ? appAtof(Value) : Fallback;
}

static void XboxProfileSetInt( const TCHAR* Section, const TCHAR* Key, INT Value )
{
    if( !GConfig )
        return;
    TCHAR Text[32];
    appSprintf( Text, TEXT("%i"), Value );
    GConfig->SetString( Section, Key, Text, TEXT("User.ini") );
}

static void XboxProfileSetFloat( const TCHAR* Section, const TCHAR* Key, FLOAT Value )
{
    if( !GConfig )
        return;
    TCHAR Text[32];
    appSprintf( Text, TEXT("%.3f"), Value );
    GConfig->SetString( Section, Key, Text, TEXT("User.ini") );
}

static void XboxProfileLoadDirectory( UBOOL ForceReload=0 )
{
    if( GXboxProfilesLoaded && !ForceReload )
        return;

    GXboxProfilesLoaded = 1;
    appMemzero( GXboxProfiles, sizeof(GXboxProfiles) );
    GXboxActiveProfile = Clamp<INT>( XboxProfileConfigInt(TEXT("XboxProfiles"), TEXT("Active"), 0), 0, XBOX_PROFILE_COUNT-1 );

    for( INT i=0; i<XBOX_PROFILE_COUNT; i++ )
    {
        TCHAR Section[32];
        TCHAR DefaultName[32];
        XboxProfileSectionName( i, Section, ARRAY_COUNT(Section) );
        GXboxProfiles[i].Created = XboxProfileConfigInt( Section, TEXT("Created"), 0 ) != 0;
        appSprintf( DefaultName, TEXT("PLAYER %i"), i + 1 );
        const TCHAR* Name = GConfig ? GConfig->GetStr( Section, TEXT("Name"), TEXT("User.ini") ) : NULL;
        appStrncpy( GXboxProfiles[i].Name, (Name && Name[0]) ? Name : DefaultName, ARRAY_COUNT(GXboxProfiles[i].Name) );
        GXboxProfiles[i].Name[ARRAY_COUNT(GXboxProfiles[i].Name)-1] = 0;
    }
}

static UBOOL XboxProfileActiveCreated()
{
    XboxProfileLoadDirectory();
    return GXboxProfiles[GXboxActiveProfile].Created;
}

static UBOOL XboxProfileSelectedCreated()
{
    XboxProfileLoadDirectory();
    return GXboxProfiles[Clamp<INT>(GXboxProfileSelected, 0, XBOX_PROFILE_COUNT-1)].Created;
}

static void XboxProfileSaveClientConfig( UXboxClient* Client, const TCHAR* Section )
{
    if( !Client || !GConfig )
        return;

    XboxProfileSetInt( Section, TEXT("ControlPreset"), Client->ControlPreset );
    XboxProfileSetInt( Section, TEXT("ButtonLayout"), Client->ButtonLayout );
    XboxProfileSetInt( Section, TEXT("StickLayout"), Client->StickLayout );
    XboxProfileSetFloat( Section, TEXT("LookSensitivity"), Client->ScaleRUV );
    XboxProfileSetFloat( Section, TEXT("MoveSensitivity"), Client->ScaleXYZ );
    XboxProfileSetInt( Section, TEXT("InvertY"), Client->InvertVertical ? 1 : 0 );
    XboxProfileSetFloat( Section, TEXT("DeadZone"), Client->DeadZone );
    for( INT i=0; i<XCB_Count; i++ )
    {
        TCHAR Key[32];
        appSprintf( Key, TEXT("ButtonAction%i"), i );
        XboxProfileSetInt( Section, Key, XboxControlButtonAction(Client, i) );
    }
}

static void XboxProfileApplyClientConfigForIndex( UXboxClient* Client, INT ProfileIndex, UBOOL bSaveGlobal )
{
    if( !Client || !GConfig )
        return;

    XboxProfileLoadDirectory();
    ProfileIndex = Clamp<INT>( ProfileIndex, 0, XBOX_PROFILE_COUNT-1 );
    if( !GXboxProfiles[ProfileIndex].Created )
        return;

    TCHAR Section[32];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    Client->ControlPreset = Clamp<INT>( XboxProfileConfigInt(Section, TEXT("ControlPreset"), Client->ControlPreset), -1, ARRAY_COUNT(GXboxControlPresets)-1 );
    Client->ButtonLayout = Clamp<INT>( XboxProfileConfigInt(Section, TEXT("ButtonLayout"), Client->ButtonLayout), 0, 2 );
    Client->StickLayout = XboxStickLayoutClamp( XboxProfileConfigInt(Section, TEXT("StickLayout"), Client->StickLayout) );
    Client->ScaleRUV = Clamp<FLOAT>( XboxProfileConfigFloat(Section, TEXT("LookSensitivity"), Client->ScaleRUV), 25.0f, 200.0f );
    Client->ScaleXYZ = Clamp<FLOAT>( XboxProfileConfigFloat(Section, TEXT("MoveSensitivity"), Client->ScaleXYZ), 25.0f, 200.0f );
    Client->InvertVertical = XboxProfileConfigInt( Section, TEXT("InvertY"), Client->InvertVertical ? 1 : 0 ) != 0;
    Client->DeadZone = Clamp<FLOAT>( XboxProfileConfigFloat(Section, TEXT("DeadZone"), Client->DeadZone), 0.05f, 0.40f );
    for( INT i=0; i<XCB_Count; i++ )
    {
        TCHAR Key[32];
        appSprintf( Key, TEXT("ButtonAction%i"), i );
        XboxControlSetButtonAction( Client, i, XboxProfileConfigInt(Section, Key, XboxControlButtonAction(Client, i)) );
    }
    if( bSaveGlobal )
        Client->SaveConfig();
    GXboxLog.Write( "XPROFILE applied client slot=%d name=%s preset=%d stick=%d look=%.1f move=%.1f invert=%d deadzone=%.2f",
        ProfileIndex + 1,
        TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name),
        Client->ControlPreset,
        Client->StickLayout,
        Client->ScaleRUV,
        Client->ScaleXYZ,
        Client->InvertVertical ? 1 : 0,
        Client->DeadZone );
}

extern "C" void XboxProfileApplyClientConfig( UXboxClient* Client )
{
    XboxProfileLoadDirectory();
    XboxProfileApplyClientConfigForIndex( Client, GXboxActiveProfile, 1 );
}

static void XboxSplitLoadProfileControls( INT Port, INT ProfileIndex, UXboxClient* Client )
{
    Port = Clamp<INT>( Port, 0, 3 );
    ProfileIndex = Clamp<INT>( ProfileIndex, 0, XBOX_PROFILE_COUNT-1 );
    FXboxRuntimeProfileControls& Controls = GXboxSplitProfileControls[Port];
    appMemzero( &Controls, sizeof(Controls) );
    Controls.Profile = ProfileIndex;
    XboxProfileLoadDirectory();
    if( !GXboxProfiles[ProfileIndex].Created )
        return;

    TCHAR Section[32];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    Controls.Valid = 1;
    Controls.LookSensitivity = Clamp<FLOAT>( XboxProfileConfigFloat(Section, TEXT("LookSensitivity"), Client ? Client->ScaleRUV : 100.0f), 25.0f, 200.0f );
    Controls.MoveSensitivity = Clamp<FLOAT>( XboxProfileConfigFloat(Section, TEXT("MoveSensitivity"), Client ? Client->ScaleXYZ : 100.0f), 25.0f, 200.0f );
    Controls.DeadZone = Clamp<FLOAT>( XboxProfileConfigFloat(Section, TEXT("DeadZone"), Client ? Client->DeadZone : 0.20f), 0.05f, 0.40f );
    Controls.InvertY = XboxProfileConfigInt( Section, TEXT("InvertY"), Client && Client->InvertVertical ? 1 : 0 ) != 0;
    Controls.StickLayout = XboxStickLayoutClamp( XboxProfileConfigInt(Section, TEXT("StickLayout"), Client ? Client->StickLayout : XSL_Default) );
    for( INT i=0; i<XCB_Count; i++ )
    {
        TCHAR Key[32];
        appSprintf( Key, TEXT("ButtonAction%i"), i );
        Controls.Actions[i] = XboxControlClampAction( XboxProfileConfigInt(Section, Key, XboxControlButtonAction(Client, i)) );
    }
    GXboxLog.Write( "XPROFILE multiplayer controls port=%d slot=%d name=%s stick=%d look=%.1f move=%.1f invert=%d deadzone=%.2f",
        Port + 1,
        ProfileIndex + 1,
        TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name),
        Controls.StickLayout,
        Controls.LookSensitivity,
        Controls.MoveSensitivity,
        Controls.InvertY ? 1 : 0,
        Controls.DeadZone );
}

static FXboxRuntimeProfileControls* XboxSplitControlsForPort( INT Port )
{
    Port = Clamp<INT>( Port, 0, 3 );
    return (GXboxSplitActive && GXboxSplitProfileControls[Port].Valid) ? &GXboxSplitProfileControls[Port] : NULL;
}

static INT XboxControlButtonActionForPort( UXboxClient* Client, INT Port, INT Button )
{
    FXboxRuntimeProfileControls* Controls = XboxSplitControlsForPort( Port );
    return Controls ? XboxControlClampAction(Controls->Actions[Clamp<INT>(Button, 0, XCB_Count-1)]) : XboxControlButtonAction(Client, Button);
}

static void XboxProfileApplyPlayerOptionsForPort( APlayerPawn* Player, INT Port )
{
    Port = Clamp<INT>( Port, 0, 3 );
    if( !Player )
        return;
    INT ProfileIndex = GXboxSplitReadySlots[Port].Profile;
    if( ProfileIndex < 0 || ProfileIndex >= XBOX_PROFILE_COUNT || !GXboxProfiles[ProfileIndex].Created )
        return;

    TCHAR Section[32];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    INT Hand = Clamp<INT>( XboxProfileConfigInt(Section, TEXT("WeaponHand"), 0), 0, ARRAY_COUNT(GXboxWeaponHands)-1 );
    Player->Handedness = GXboxWeaponHandValues[Hand];
    Player->bNeverAutoSwitch = XboxProfileConfigInt( Section, TEXT("AutoSwitch"), 1 ) == 0;
    Player->bNeverSwitchOnPickup = Player->bNeverAutoSwitch;
    GXboxLog.Write( "XPROFILE gameplay options applied port=%d slot=%d hand=%d autoSwitch=%d",
        Port + 1, ProfileIndex + 1, Hand, Player->bNeverAutoSwitch ? 0 : 1 );
}

static void XboxMenuCleanExportedText( FString& Value )
{
    const TCHAR* Text = *Value;
    if( Value.Len() >= 2 && Text[0] == '"' && Text[Value.Len()-1] == '"' )
        Value = Value.Mid( 1, Value.Len() - 2 );
    if( appStricmp( *Value, TEXT("None") ) == 0 )
        Value = TEXT("");
}

static UBOOL XboxMenuClassDefaultStringAt( UClass* Class, const TCHAR* PropertyName, INT ArrayIndex, FString& OutValue )
{
    OutValue = TEXT("");
    if( !Class || !Class->Defaults.Num() )
        return 0;

    UProperty* Property = FindField<UProperty>( Class, PropertyName );
    if( !Property || ArrayIndex < 0 || ArrayIndex >= Property->ArrayDim )
        return 0;

    TCHAR Temp[1024] = TEXT("");
    Property->ExportText( ArrayIndex, Temp, &Class->Defaults(0), &Class->Defaults(0), 0 );
    OutValue = Temp;
    XboxMenuCleanExportedText( OutValue );
    return OutValue.Len() > 0;
}

static UBOOL XboxMenuClassDefaultString( UClass* Class, const TCHAR* PropertyName, FString& OutValue )
{
    return XboxMenuClassDefaultStringAt( Class, PropertyName, 0, OutValue );
}

static INT XboxMenuClassDefaultIntAt( UClass* Class, const TCHAR* PropertyName, INT ArrayIndex, INT DefaultValue )
{
    FString Value;
    if( XboxMenuClassDefaultStringAt( Class, PropertyName, ArrayIndex, Value ) )
        return appAtoi( *Value );
    return DefaultValue;
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

static UBOOL XboxSetClassDefaultPropertyText( UClass* Class, const TCHAR* PropertyName, const TCHAR* Value )
{
    if( !Class || !PropertyName || !Value || Class->Defaults.Num() <= 0 )
        return 0;

    UProperty* Property = FindField<UProperty>( Class, PropertyName );
    if( !Property || Property->Offset + Property->ElementSize > Class->Defaults.Num() )
        return 0;

    Property->ImportText( Value, &Class->Defaults(0) + Property->Offset, 0 );
    return 1;
}

static UBOOL XboxGetObjectPropertyString( UObject* Object, const TCHAR* PropertyName, FString& OutValue )
{
    OutValue = TEXT("");
    if( !Object || !PropertyName )
        return 0;

    UProperty* Property = FindField<UProperty>( Object->GetClass(), PropertyName );
    if( !Property )
        return 0;

    TCHAR Temp[256] = TEXT("");
    Property->ExportText( 0, Temp, (BYTE*)Object, (BYTE*)Object, 0 );
    OutValue = Temp;
    XboxMenuCleanExportedText( OutValue );
    return OutValue.Len() > 0;
}

static INT XboxGetObjectPropertyInt( UObject* Object, const TCHAR* PropertyName, INT DefaultValue )
{
    FString Value;
    if( XboxGetObjectPropertyString( Object, PropertyName, Value ) )
        return appAtoi( *Value );
    return DefaultValue;
}

static UObject* XboxGetObjectPropertyObjectAt( UObject* Object, const TCHAR* PropertyName, INT ArrayIndex )
{
    if( !Object || !PropertyName )
        return NULL;

    UProperty* Property = FindField<UProperty>( Object->GetClass(), PropertyName );
    UObjectProperty* ObjectProperty = Cast<UObjectProperty>( Property );
    if( !ObjectProperty || ArrayIndex < 0 || ArrayIndex >= Property->ArrayDim )
        return NULL;

    BYTE* Value = (BYTE*)Object + Property->Offset + ArrayIndex * Property->ElementSize;
    return *(UObject**)Value;
}

static UBOOL XboxSetObjectPropertyInt( UObject* Object, const TCHAR* PropertyName, INT Value )
{
    TCHAR Temp[32];
    appSprintf( Temp, TEXT("%i"), Value );
    return XboxSetObjectPropertyText( Object, PropertyName, Temp );
}

static void XboxTournamentLogReadyState( UXboxViewport* Viewport, const char* Reason, UBOOL bForce )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    if( !Player || !XboxIsTournamentLevel(Level) )
        return;

    if( !bForce && GXboxTournamentLastLoggedLevel == Level && GXboxTournamentLastLoggedPlayer == Player )
        return;

    GXboxTournamentLastLoggedLevel = Level;
    GXboxTournamentLastLoggedPlayer = Player;

    UObject* Game = (Level->GetLevelInfo() && Level->GetLevelInfo()->Game) ? Level->GetLevelInfo()->Game : NULL;
    FString bRequireReady;
    FString bRatedGame;
    XboxGetObjectPropertyString( Game, TEXT("bRequireReady"), bRequireReady );
    XboxGetObjectPropertyString( Game, TEXT("bRatedGame"), bRatedGame );
    INT CountDown = XboxGetObjectPropertyInt( Game, TEXT("CountDown"), -1 );
    INT NumPlayers = XboxGetObjectPropertyInt( Game, TEXT("NumPlayers"), -1 );
    INT RemainingBots = XboxGetObjectPropertyInt( Game, TEXT("RemainingBots"), -1 );
    GXboxLog.Write( "XTOUR ready state reason=%s url=%s playerClass=%s state=%s ready=%d showMenu=%d specialMenu=%d game=%s requireReady=%s rated=%s countDown=%d numPlayers=%d remainingBots=%d actualSpectator=%d",
        Reason ? Reason : "",
        Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.String()) : "",
        Player->GetClass() ? TCHAR_TO_ANSI(Player->GetClass()->GetName()) : "None",
        TCHAR_TO_ANSI(XboxPlayerStateName(Player)),
        Player->bReadyToPlay ? 1 : 0,
        Player->bShowMenu ? 1 : 0,
        Player->bSpecialMenu ? 1 : 0,
        Game && Game->GetClass() ? TCHAR_TO_ANSI(Game->GetClass()->GetName()) : "None",
        bRequireReady.Len() ? TCHAR_TO_ANSI(*bRequireReady) : "",
        bRatedGame.Len() ? TCHAR_TO_ANSI(*bRatedGame) : "",
        CountDown,
        NumPlayers,
        RemainingBots,
        XboxClassIsNamed( Player->GetClass(), TEXT("Spectator") ) ? 1 : 0 );
}

static void XboxTournamentHandleReadyInput( UXboxViewport* Viewport, UBOOL bFireEdge, UBOOL bAltFireEdge )
{
    if( !bFireEdge && !bAltFireEdge )
        return;

    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    if( !Player || !XboxIsTournamentLevel(Level) )
        return;

    UObject* Game = (Level->GetLevelInfo() && Level->GetLevelInfo()->Game) ? Level->GetLevelInfo()->Game : NULL;
    FString bRequireReady;
    XboxGetObjectPropertyString( Game, TEXT("bRequireReady"), bRequireReady );
    INT CountDown = XboxGetObjectPropertyInt( Game, TEXT("CountDown"), 0 );
    UBOOL bWaiting = appStricmp( XboxPlayerStateName(Player), TEXT("PlayerWaiting") ) == 0;
    UBOOL bRequiresReady = bRequireReady.Len() && appStricmp( *bRequireReady, TEXT("True") ) == 0;
    if( !bWaiting && !bRequiresReady && CountDown <= 0 )
        return;

    Player->bReadyToPlay = 1;
    Player->bShowMenu = 0;
    Player->bSpecialMenu = 0;
    XboxTournamentLogReadyState( Viewport, bFireEdge ? "fire-ready" : "alt-ready", 1 );

    if( XboxClassIsNamed( Player->GetClass(), TEXT("Spectator") ) )
    {
        GXboxLog.Write( "XTOUR ready blocked: player is actual Spectator class, not PlayerWaiting tournament pawn" );
        return;
    }

    XboxSetObjectPropertyText( Game, TEXT("bRequireReady"), TEXT("False") );
    XboxSetObjectPropertyInt( Game, TEXT("CountDown"), 0 );
    UFunction* StartMatch = Game ? Game->FindFunction( FName(TEXT("StartMatch"), FNAME_Find) ) : NULL;
    if( StartMatch )
    {
        Game->ProcessEvent( StartMatch, NULL );
        GXboxLog.Write( "XTOUR StartMatch forced from controller ready fire=%d alt=%d state=%s class=%s",
            bFireEdge ? 1 : 0,
            bAltFireEdge ? 1 : 0,
            TCHAR_TO_ANSI(XboxPlayerStateName(Player)),
            Player->GetClass() ? TCHAR_TO_ANSI(Player->GetClass()->GetName()) : "None" );
    }
    else
    {
        GXboxLog.Write( "XTOUR ready failed: StartMatch function missing game=%s",
            Game && Game->GetClass() ? TCHAR_TO_ANSI(Game->GetClass()->GetName()) : "None" );
    }
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

static UBOOL XboxSetClassDefaultPropertyInt( const TCHAR* ClassName, const TCHAR* PropertyName, INT Value )
{
    TCHAR Text[32];
    appSprintf( Text, TEXT("%i"), Value );
    return XboxSetClassDefaultPropertyText( ClassName, PropertyName, Text );
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

static INT XboxMenuFindPlayerCharacter( const FString& ClassValue, const FString& SkinValue, const FString& FaceValue, const FString& CharacterValue )
{
    INT ClassFallback = 0;
    UBOOL bHaveClassFallback = 0;

#if TARGET_XBOX
    if( CharacterValue.Len() > 0 )
    {
        for( INT i=0; i<GXboxPlayerClasses.Num(); i++ )
        {
            const FXboxPlayerClassOption& Player = GXboxPlayerClasses(i);
            if( appStricmp( *Player.Label, *CharacterValue ) != 0 )
                continue;

            if( appStricmp(*Player.URLValue, *ClassValue) != 0
            ||  appStricmp(*Player.SkinValue, *SkinValue) != 0
            ||  appStricmp(*Player.FaceValue, *FaceValue) != 0 )
            {
                GXboxLog.Write( "XMENU character identity overrides sanitized config character=%s class=%s skin=%s face=%s",
                    TCHAR_TO_ANSI(*CharacterValue),
                    ClassValue.Len() ? TCHAR_TO_ANSI(*ClassValue) : "",
                    SkinValue.Len() ? TCHAR_TO_ANSI(*SkinValue) : "",
                    FaceValue.Len() ? TCHAR_TO_ANSI(*FaceValue) : "" );
            }
            return i;
        }
    }
#endif

    for( INT i=0; i<GXboxPlayerClasses.Num(); i++ )
    {
        const FXboxPlayerClassOption& Player = GXboxPlayerClasses(i);
        if( appStricmp( *Player.URLValue, *ClassValue ) != 0 )
            continue;

        if( !bHaveClassFallback )
        {
            ClassFallback = i;
            bHaveClassFallback = 1;
        }

        UBOOL bSkinMatches = (SkinValue.Len() == 0 && Player.SkinValue.Len() == 0)
            || (SkinValue.Len() > 0 && appStricmp( *Player.SkinValue, *SkinValue ) == 0);
        UBOOL bFaceMatches = (FaceValue.Len() == 0 && Player.FaceValue.Len() == 0)
            || (FaceValue.Len() > 0 && appStricmp( *Player.FaceValue, *FaceValue ) == 0);

        if( bSkinMatches && bFaceMatches )
            return i;
    }

    return bHaveClassFallback ? ClassFallback : 0;
}

static UBOOL XboxMenuHasPlayerClass( const FString& Value )
{
    for( INT i=0; i<GXboxPlayerClasses.Num(); i++ )
        if( appStricmp( *GXboxPlayerClasses(i).URLValue, *Value ) == 0 )
            return 1;
    return 0;
}

static FXboxPlayerClassOption& XboxMenuAddPlayerClassOption(
    const TCHAR* Label,
    const TCHAR* URLValue,
    const TCHAR* MeshName,
    const TCHAR* MeshPath,
    const TCHAR* SelectionMesh,
    const TCHAR* VoiceMetaClass,
    const TCHAR* DefaultVoice,
    const TCHAR* DefaultPackage,
    const TCHAR* DefaultSkinName,
    INT FixedSkin,
    INT FaceSkin,
    INT TeamSkin1,
    INT TeamSkin2,
    UBOOL bMultiSkinned )
{
    FXboxPlayerClassOption& Option = *new(GXboxPlayerClasses)FXboxPlayerClassOption;
    Option.Label = Label;
    Option.URLValue = URLValue;
    Option.MeshName = MeshName;
    Option.MeshPath = MeshPath;
    Option.SelectionMesh = SelectionMesh;
    Option.VoiceMetaClass = VoiceMetaClass;
    Option.DefaultVoice = DefaultVoice;
    Option.DefaultPackage = DefaultPackage;
    Option.DefaultSkinName = DefaultSkinName;
    Option.FixedSkin = FixedSkin;
    Option.FaceSkin = FaceSkin;
    Option.TeamSkin1 = TeamSkin1;
    Option.TeamSkin2 = TeamSkin2;
    Option.bMultiSkinned = bMultiSkinned;
    return Option;
}

static void XboxMenuAddFallbackPlayerClass()
{
    XboxMenuAddPlayerClassOption(
        TEXT("MALE SOLDIER"),
        TEXT("Botpack.TMale2"),
        TEXT("Soldier"),
        TEXT("Botpack.Soldier"),
        TEXT("Botpack.SelectionMale2"),
        TEXT("BotPack.VoiceMale"),
        TEXT("BotPack.VoiceMaleTwo"),
        TEXT("SoldierSkins."),
        TEXT("SoldierSkins.blkt"),
        2, 3, 0, 1, 1 );
}

static void XboxMenuAddKnownPlayerClasses()
{
    XboxMenuAddPlayerClassOption( TEXT("MALE COMMANDO"), TEXT("Botpack.TMale1"), TEXT("Commando"), TEXT("Botpack.Commando"), TEXT("Botpack.SelectionMale1"), TEXT("BotPack.VoiceMale"), TEXT("BotPack.VoiceMaleOne"), TEXT("CommandoSkins."), TEXT("CommandoSkins.cmdo"), 0, 1, 2, 3, 1 );
    XboxMenuAddFallbackPlayerClass();
    XboxMenuAddPlayerClassOption( TEXT("FEMALE COMMANDO"), TEXT("Botpack.TFemale1"), TEXT("FCommando"), TEXT("Botpack.FCommando"), TEXT("Botpack.SelectionFemale1"), TEXT("BotPack.VoiceFemale"), TEXT("BotPack.VoiceFemaleOne"), TEXT("FCommandoSkins."), TEXT("FCommandoSkins.cmdo"), 0, 3, 0, 1, 1 );
    XboxMenuAddPlayerClassOption( TEXT("FEMALE SOLDIER"), TEXT("Botpack.TFemale2"), TEXT("SGirl"), TEXT("Botpack.SGirl"), TEXT("Botpack.SelectionFemale2"), TEXT("BotPack.VoiceFemale"), TEXT("BotPack.VoiceFemaleTwo"), TEXT("SGirlSkins."), TEXT("SGirlSkins.army"), 2, 3, 0, 1, 1 );
    XboxMenuAddPlayerClassOption( TEXT("BOSS"), TEXT("Botpack.TBoss"), TEXT("Boss"), TEXT("Botpack.Boss"), TEXT("Botpack.SelectionBoss"), TEXT("BotPack.VoiceMale"), TEXT("BotPack.VoiceBoss"), TEXT("BossSkins."), TEXT("BossSkins.Boss"), 0, 1, 2, 3, 1 );
}

#if TARGET_XBOX
struct FXboxKnownPlayerCharacter
{
    const TCHAR* Label;
    const TCHAR* ClassName;
    const TCHAR* SkinName;
    const TCHAR* FaceName;
    const TCHAR* VoiceName;
    INT Team;
    const char* PortraitName;
};

static void XboxMenuSetKnownClassDefaults( FXboxPlayerClassOption& Option )
{
    if( appStricmp( *Option.URLValue, TEXT("Botpack.TMale1") ) == 0 )
    {
        Option.MeshName = TEXT("Commando");
        Option.MeshPath = TEXT("Botpack.Commando");
        Option.SelectionMesh = TEXT("Botpack.SelectionMale1");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceMaleOne");
        Option.DefaultPackage = TEXT("CommandoSkins.");
        Option.DefaultSkinName = TEXT("CommandoSkins.cmdo");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("Botpack.TMale2") ) == 0 )
    {
        Option.MeshName = TEXT("Soldier");
        Option.MeshPath = TEXT("Botpack.Soldier");
        Option.SelectionMesh = TEXT("Botpack.SelectionMale2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceMaleTwo");
        Option.DefaultPackage = TEXT("SoldierSkins.");
        Option.DefaultSkinName = TEXT("SoldierSkins.blkt");
        Option.FixedSkin = 2; Option.FaceSkin = 3; Option.TeamSkin1 = 0; Option.TeamSkin2 = 1; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("Botpack.TFemale1") ) == 0 )
    {
        Option.MeshName = TEXT("FCommando");
        Option.MeshPath = TEXT("Botpack.FCommando");
        Option.SelectionMesh = TEXT("Botpack.SelectionFemale1");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceFemale");
        Option.DefaultVoice = TEXT("BotPack.VoiceFemaleOne");
        Option.DefaultPackage = TEXT("FCommandoSkins.");
        Option.DefaultSkinName = TEXT("FCommandoSkins.cmdo");
        Option.FixedSkin = 0; Option.FaceSkin = 3; Option.TeamSkin1 = 0; Option.TeamSkin2 = 1; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("Botpack.TFemale2") ) == 0 )
    {
        Option.MeshName = TEXT("SGirl");
        Option.MeshPath = TEXT("Botpack.SGirl");
        Option.SelectionMesh = TEXT("Botpack.SelectionFemale2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceFemale");
        Option.DefaultVoice = TEXT("BotPack.VoiceFemaleTwo");
        Option.DefaultPackage = TEXT("SGirlSkins.");
        Option.DefaultSkinName = TEXT("SGirlSkins.army");
        Option.FixedSkin = 2; Option.FaceSkin = 3; Option.TeamSkin1 = 0; Option.TeamSkin2 = 1; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("Botpack.TBoss") ) == 0 )
    {
        Option.MeshName = TEXT("Boss");
        Option.MeshPath = TEXT("Botpack.Boss");
        Option.SelectionMesh = TEXT("Botpack.SelectionBoss");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceBoss");
        Option.DefaultPackage = TEXT("BossSkins.");
        Option.DefaultSkinName = TEXT("BossSkins.Boss");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("MultiMesh.TSkaarj") ) == 0 )
    {
        Option.MeshName = TEXT("TSkaarj");
        Option.MeshPath = TEXT("EpicCustomModels.TSkM");
        Option.SelectionMesh = TEXT("EpicCustomModels.TSkM");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("MultiMesh.SkaarjVoice");
        Option.DefaultPackage = TEXT("TSkMSkins.");
        Option.DefaultSkinName = TEXT("TSkMSkins.Warr");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("MultiMesh.TNali") ) == 0 )
    {
        Option.MeshName = TEXT("TNali");
        Option.MeshPath = TEXT("EpicCustomModels.TNaliMesh");
        Option.SelectionMesh = TEXT("EpicCustomModels.TNaliMesh");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("MultiMesh.NaliVoice");
        Option.DefaultPackage = TEXT("TNaliMeshSkins.");
        Option.DefaultSkinName = TEXT("TNaliMeshSkins.Ouboudah");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 0;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("MultiMesh.TCow") ) == 0 )
    {
        Option.MeshName = TEXT("TCow");
        Option.MeshPath = TEXT("EpicCustomModels.TCowMesh");
        Option.SelectionMesh = TEXT("EpicCustomModels.TCowMesh");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("MultiMesh.CowVoice");
        Option.DefaultPackage = TEXT("TCowMeshSkins.");
        Option.DefaultSkinName = TEXT("TCowMeshSkins.WarCow");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 0;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("UTPS2Characters.DamienPS2M") ) == 0
    ||  appStricmp( *Option.URLValue, TEXT("UTPS2Characters.DamienPS2") ) == 0 )
    {
        Option.MeshName = TEXT("DamienPS2");
        Option.MeshPath = TEXT("UTPS2Characters.DamienPS2");
        Option.SelectionMesh = TEXT("UTPS2Characters.DamienPS2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceMaleTwo");
        Option.DefaultPackage = TEXT("DamienPS2Skins.");
        Option.DefaultSkinName = TEXT("DamienPS2Skins.kane");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("UTPS2Characters.DominatorPS2M") ) == 0 )
    {
        Option.MeshName = TEXT("SkaarjBossPS2");
        Option.MeshPath = TEXT("UTPS2Characters.SkaarjBossPS2");
        Option.SelectionMesh = TEXT("UTPS2Characters.SkaarjBossPS2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("UTPS2Characters.DominatorVoice");
        Option.DefaultPackage = TEXT("SkaarjBPS2Skins.");
        Option.DefaultSkinName = TEXT("SkaarjBPS2Skins.Domi");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("UTPS2Characters.WarbossPS2") ) == 0 )
    {
        Option.MeshName = TEXT("WarbossPS2");
        Option.MeshPath = TEXT("UTPS2Characters.WarbossPS2");
        Option.SelectionMesh = TEXT("UTPS2Characters.WarbossPS2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceBoss");
        Option.DefaultPackage = TEXT("WarbossPS2Skins_PS2Purple.");
        Option.DefaultSkinName = TEXT("WarbossPS2Skins_PS2Purple.WarP");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("UTPS2Characters.XanPS2") ) == 0 )
    {
        Option.MeshName = TEXT("XanPS2");
        Option.MeshPath = TEXT("UTPS2Characters.XanPS2");
        Option.SelectionMesh = TEXT("UTPS2Characters.XanPS2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceBoss");
        Option.DefaultPackage = TEXT("XanPS2Skins_PS2Lighter.");
        Option.DefaultSkinName = TEXT("XanPS2Skins_PS2Lighter.XnPS");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("UTPS2Characters.SkaarjHybridPS2") ) == 0 )
    {
        Option.MeshName = TEXT("SkaarjHybridPS2");
        Option.MeshPath = TEXT("UTPS2Characters.SkaarjHybridPS2");
        Option.SelectionMesh = TEXT("UTPS2Characters.SkaarjHybridPS2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("UTPS2Characters.SkaarjHybridPS2Voice");
        Option.DefaultPackage = TEXT("SkaarjHybridPS2Skins.");
        Option.DefaultSkinName = TEXT("SkaarjHybridPS2Skins.Warr");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("UTPS2Characters.SkaarjBossPS2") ) == 0 )
    {
        Option.MeshName = TEXT("SkaarjBossPS2");
        Option.MeshPath = TEXT("UTPS2Characters.SkaarjBossPS2");
        Option.SelectionMesh = TEXT("UTPS2Characters.SkaarjBossPS2");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("UTPS2Characters.SkaarjHybridPS2Voice");
        Option.DefaultPackage = TEXT("SkaarjBPS2Skins.");
        Option.DefaultSkinName = TEXT("SkaarjBPS2Skins.Warr");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 2; Option.TeamSkin2 = 3; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("HaloMasterChief.HaloMasterChief") ) == 0 )
    {
        Option.MeshName = TEXT("HaloMasterChief");
        Option.MeshPath = TEXT("HaloMasterChief.HaloMasterChief");
        Option.SelectionMesh = TEXT("HaloMasterChief.HaloMasterChief");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceMaleOne");
        Option.DefaultPackage = TEXT("HaloMasterChiefSkins.");
        Option.DefaultSkinName = TEXT("HaloMasterChiefSkins.chef");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 0; Option.TeamSkin2 = 0; Option.bMultiSkinned = 1;
        return;
    }
    if( appStricmp( *Option.URLValue, TEXT("HaloUTXbox.Elite") ) == 0 )
    {
        Option.MeshName = TEXT("EliteMesh");
        Option.MeshPath = TEXT("HaloUTXbox.EliteMesh");
        Option.SelectionMesh = TEXT("HaloUTXbox.EliteMesh");
        Option.VoiceMetaClass = TEXT("BotPack.VoiceMale");
        Option.DefaultVoice = TEXT("BotPack.VoiceMaleOne");
        Option.DefaultPackage = TEXT("HaloUTXbox.Skins.");
        Option.DefaultSkinName = TEXT("HaloUTXbox.Skins.EliteBlue");
        Option.FixedSkin = 0; Option.FaceSkin = 1; Option.TeamSkin1 = 0; Option.TeamSkin2 = 1; Option.bMultiSkinned = 1;
        return;
    }
}

static FXboxPlayerClassOption& XboxMenuAddPlayerCharacterOption( const FXboxKnownPlayerCharacter& Character )
{
    FXboxPlayerClassOption& Option = XboxMenuAddPlayerClassOption(
        Character.Label,
        Character.ClassName,
        TEXT(""),
        TEXT(""),
        TEXT(""),
        TEXT(""),
        TEXT(""),
        TEXT(""),
        TEXT(""),
        2, 3, 0, 1, 1 );

    XboxMenuSetKnownClassDefaults( Option );
    Option.SkinValue = Character.SkinName ? Character.SkinName : TEXT("");
    Option.FaceValue = Character.FaceName ? Character.FaceName : TEXT("");
    if( Option.SkinValue.Len() )
        Option.DefaultSkinName = Option.SkinValue;
    if( Character.VoiceName && Character.VoiceName[0] )
        Option.DefaultVoice = Character.VoiceName;
    Option.DefaultTeam = Clamp<INT>( Character.Team, 0, 255 );
    if( Character.PortraitName && Character.PortraitName[0] )
    {
        appStrncpy( Option.PortraitName, Character.PortraitName, ARRAY_COUNT(Option.PortraitName) );
        Option.PortraitName[ARRAY_COUNT(Option.PortraitName)-1] = 0;
    }
    return Option;
}

static const FXboxKnownPlayerCharacter GXboxKnownPlayerCharacters[] =
{
    { TEXT("ANNAKA"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.fbth"), TEXT("SGirlSkins.Annaka"), TEXT("BotPack.VoiceFemaleTwo"), 0, "char_annaka.xui" },
    { TEXT("ARKON"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.RawS"), TEXT("SoldierSkins.Arkon"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_arkon.xui" },
    { TEXT("ARYSS"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.fbth"), TEXT("SGirlSkins.Aryss"), TEXT("BotPack.VoiceFemaleTwo"), 0, "char_aryss.xui" },
    { TEXT("ATOMIC COW"), TEXT("MultiMesh.TCow"), TEXT("TCowMeshSkins.AtomicCow"), TEXT("TCowMeshSkins.WarCowFace"), TEXT("MultiMesh.CowVoice"), 255, "char_atomiccow.xui" },
    { TEXT("BAETAL"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.PitF"), TEXT("TSkMSkins.Baetal"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_baetal.xui" },
    { TEXT("BERSERKER"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.Warr"), TEXT("TSkMSkins.Berserker"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_berserker.xui" },
    { TEXT("BLAKE"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.cmdo"), TEXT("CommandoSkins.Blake"), TEXT("BotPack.VoiceMaleOne"), 255, "char_blake.xui" },
    { TEXT("BORIS"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.daco"), TEXT("CommandoSkins.Boris"), TEXT("BotPack.VoiceMaleOne"), 1, "char_boris.xui" },
    { TEXT("CATHODE"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.fwar"), TEXT("SGirlSkins.Cathode"), TEXT("BotPack.VoiceFemaleTwo"), 0, "char_cathode.xui" },
    { TEXT("CILIA"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.Venm"), TEXT("SGirlSkins.Cilia"), TEXT("BotPack.VoiceFemaleTwo"), 3, "char_cilia.xui" },
    { TEXT("CRYSS"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.goth"), TEXT("FCommandoSkins.Cryss"), TEXT("BotPack.VoiceFemaleOne"), 255, "char_cryss.xui" },
    { TEXT("DAMIEN"), TEXT("UTPS2Characters.DamienPS2"), TEXT("DamienPS2Skins.kane"), TEXT(""), TEXT("BotPack.VoiceMaleTwo"), 255, "char_damien.xui" },
    { TEXT("DISCONNECT"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.MekS"), TEXT("TSkMSkins.Disconnect"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_disconnect.xui" },
    { TEXT("DOMINATOR"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.Warr"), TEXT("TSkMSkins.Dominator"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_dominator.xui" },
    { TEXT("DOMINATOR PS2"), TEXT("UTPS2Characters.SkaarjBossPS2"), TEXT("SkaarjBPS2Skins.Domi"), TEXT("SkaarjBPS2Skins.Dominator"), TEXT("UTPS2Characters.DominatorVoice"), 255, "char_dominator_ps2.xui" },
    { TEXT("FIREWALL"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.MekS"), TEXT("TSkMSkins.Firewall"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_firewall.xui" },
    { TEXT("FREYLIS"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.goth"), TEXT("FCommandoSkins.Freylis"), TEXT("BotPack.VoiceFemaleOne"), 2, "char_freylis.xui" },
    { TEXT("FURY"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.fwar"), TEXT("SGirlSkins.Fury"), TEXT("BotPack.VoiceFemaleTwo"), 255, "char_fury.xui" },
    { TEXT("GORN"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.cmdo"), TEXT("CommandoSkins.Gorn"), TEXT("BotPack.VoiceMaleOne"), 255, "char_gorn.xui" },
    { TEXT("GRAIL"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.goth"), TEXT("CommandoSkins.Grail"), TEXT("BotPack.VoiceMaleOne"), 255, "char_grail.xui" },
    { TEXT("GRAVES"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.daco"), TEXT("CommandoSkins.Graves"), TEXT("BotPack.VoiceMaleOne"), 255, "char_graves.xui" },
    { TEXT("GUARDIAN"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.Warr"), TEXT("TSkMSkins.Guardian"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_guardian.xui" },
    { TEXT("ISIS"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.Garf"), TEXT("SGirlSkins.Isis"), TEXT("BotPack.VoiceFemaleTwo"), 1, "char_isis.xui" },
    { TEXT("JAYCE"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.daco"), TEXT("FCommandoSkins.Jayce"), TEXT("BotPack.VoiceFemaleOne"), 3, "char_jayce.xui" },
    { TEXT("JOHNSON"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.sldr"), TEXT("SoldierSkins.Johnson"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_johnson.xui" },
    { TEXT("KRAGOTH"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.goth"), TEXT("CommandoSkins.Kragoth"), TEXT("BotPack.VoiceMaleOne"), 255, "char_kragoth.xui" },
    { TEXT("KREGORE"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.RawS"), TEXT("SoldierSkins.Kregore"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_kregore.xui" },
    { TEXT("KYLA"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.daco"), TEXT("FCommandoSkins.Kyla"), TEXT("BotPack.VoiceFemaleOne"), 3, "char_kyla.xui" },
    { TEXT("LAUREN"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.army"), TEXT("SGirlSkins.Lauren"), TEXT("BotPack.VoiceFemaleTwo"), 2, "char_lauren.xui" },
    { TEXT("LILITH"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.fwar"), TEXT("SGirlSkins.Lilith"), TEXT("BotPack.VoiceFemaleTwo"), 255, "char_lilith.xui" },
    { TEXT("LUTHOR"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.daco"), TEXT("CommandoSkins.Luthor"), TEXT("BotPack.VoiceMaleOne"), 1, "char_luthor.xui" },
    { TEXT("MALCOM"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.blkt"), TEXT("SoldierSkins.Malcom"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_malcom.xui" },
    { TEXT("MALISE"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.goth"), TEXT("FCommandoSkins.Malise"), TEXT("BotPack.VoiceFemaleOne"), 1, "char_malise.xui" },
    { TEXT("MARIANA"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.daco"), TEXT("FCommandoSkins.Mariana"), TEXT("BotPack.VoiceFemaleOne"), 0, "char_mariana.xui" },
    { TEXT("MASTER CHIEF"), TEXT("HaloMasterChief.HaloMasterChief"), TEXT("HaloMasterChiefSkins.chef"), TEXT("HaloMasterChiefSkins.chef2Face"), TEXT("BotPack.VoiceMaleOne"), 255, "char_masterchief.xui" },
    { TEXT("MATRIX"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.hkil"), TEXT("SoldierSkins.Matrix"), TEXT("BotPack.VoiceMaleTwo"), 1, "char_matrix.xui" },
    { TEXT("OTHELLO"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.blkt"), TEXT("SoldierSkins.Othello"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_othello.xui" },
    { TEXT("OUBOUDAH"), TEXT("MultiMesh.TNali"), TEXT("TNaliMeshSkins.Ouboudah"), TEXT("TNaliMeshSkins.nali-Face"), TEXT("MultiMesh.NaliVoice"), 255, "char_ouboudah.xui" },
    { TEXT("PHAROH"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.PitF"), TEXT("TSkMSkins.Pharoh"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_pharoh.xui" },
    { TEXT("PRIEST"), TEXT("MultiMesh.TNali"), TEXT("TNaliMeshSkins.Priest"), TEXT("TNaliMeshSkins.nali-Face"), TEXT("MultiMesh.NaliVoice"), 255, "char_priest.xui" },
    { TEXT("RAMIREZ"), TEXT("Botpack.TMale1"), TEXT("CommandoSkins.daco"), TEXT("CommandoSkins.Ramirez"), TEXT("BotPack.VoiceMaleOne"), 255, "char_ramirez.xui" },
    { TEXT("RAMPAGE"), TEXT("UTPS2Characters.WarbossPS2"), TEXT("WarbossPS2Skins_PS2Purple.WarP"), TEXT(""), TEXT("BotPack.VoiceBoss"), 255, "char_rampage.xui" },
    { TEXT("RANKIN"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.sldr"), TEXT("SoldierSkins.Rankin"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_rankin.xui" },
    { TEXT("RIKER"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.blkt"), TEXT("SoldierSkins.Riker"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_riker.xui" },
    { TEXT("SARA"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.army"), TEXT("SGirlSkins.Sara"), TEXT("BotPack.VoiceFemaleTwo"), 2, "char_sara.xui" },
    { TEXT("SARENA"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.Venm"), TEXT("SGirlSkins.Sarena"), TEXT("BotPack.VoiceFemaleTwo"), 3, "char_sarena.xui" },
    { TEXT("SKAARJ BOSS"), TEXT("UTPS2Characters.SkaarjBossPS2"), TEXT("SkaarjBPS2Skins.Warr"), TEXT("SkaarjBPS2Skins.Superfly"), TEXT("UTPS2Characters.SkaarjHybridPS2Voice"), 255, "char_skaarj_boss.xui" },
    { TEXT("SKRILAX"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkMSkins.PitF"), TEXT("TSkMSkins.Skrilax"), TEXT("MultiMesh.SkaarjVoice"), 255, "char_skrilax.xui" },
    { TEXT("TANYA"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.daco"), TEXT("FCommandoSkins.Tanya"), TEXT("BotPack.VoiceFemaleOne"), 0, "char_tanya.xui" },
    { TEXT("TENSOR"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.hkil"), TEXT("SoldierSkins.Tensor"), TEXT("BotPack.VoiceMaleTwo"), 1, "char_tensor.xui" },
    { TEXT("VECTOR"), TEXT("Botpack.TMale2"), TEXT("SoldierSkins.hkil"), TEXT("SoldierSkins.Vector"), TEXT("BotPack.VoiceMaleTwo"), 255, "char_vector.xui" },
    { TEXT("VISSE"), TEXT("Botpack.TFemale1"), TEXT("FCommandoSkins.goth"), TEXT("FCommandoSkins.Visse"), TEXT("BotPack.VoiceFemaleOne"), 2, "char_visse.xui" },
    { TEXT("VIXEN"), TEXT("Botpack.TFemale2"), TEXT("SGirlSkins.Garf"), TEXT("SGirlSkins.Vixen"), TEXT("BotPack.VoiceFemaleTwo"), 255, "char_vixen.xui" },
    { TEXT("WARCOW"), TEXT("MultiMesh.TCow"), TEXT("TCowMeshSkins.WarCow"), TEXT("TCowMeshSkins.WarCowFace"), TEXT("MultiMesh.CowVoice"), 255, "char_warcow.xui" },
    { TEXT("XAN"), TEXT("Botpack.TBoss"), TEXT("BossSkins.Boss"), TEXT(""), TEXT("BotPack.VoiceBoss"), 255, "char_xan.xui" },
    { TEXT("XAN PS2"), TEXT("UTPS2Characters.XanPS2"), TEXT("XanPS2Skins_PS2Lighter.XnPS"), TEXT(""), TEXT("BotPack.VoiceBoss"), 255, "char_xan_ps2.xui" }
};

static void XboxMenuAddKnownPlayerCharacters()
{
    for( INT i=0; i<ARRAY_COUNT(GXboxKnownPlayerCharacters); i++ )
        XboxMenuAddPlayerCharacterOption( GXboxKnownPlayerCharacters[i] );
    if( XboxMenuPackageFileExists( TEXT("HaloUTXbox.u") ) )
    {
        const FXboxKnownPlayerCharacter Elite = { TEXT("HALO ELITE"), TEXT("HaloUTXbox.Elite"), TEXT("HaloUTXbox.Skins.EliteBlue"), TEXT(""), TEXT("BotPack.VoiceMaleOne"), 1, "char_haloelite.xui" };
        XboxMenuAddPlayerCharacterOption( Elite );
    }
}

static void XboxMenuSortPlayerCharacters()
{
    for( INT i=1; i<GXboxPlayerClasses.Num(); i++ )
    {
        FXboxPlayerClassOption Value = GXboxPlayerClasses(i);
        INT Insert = i;
        while( Insert > 0 && appStricmp(*GXboxPlayerClasses(Insert-1).Label, *Value.Label) > 0 )
        {
            GXboxPlayerClasses(Insert) = GXboxPlayerClasses(Insert-1);
            Insert--;
        }
        GXboxPlayerClasses(Insert) = Value;
    }
}

static void XboxMenuAuditPlayerCharacters()
{
    UBOOL bSorted = 1;
    INT DuplicateAppearances = 0;
    INT NumberedPS2SkinPrefixes = 0;

    for( INT i=0; i<GXboxPlayerClasses.Num(); i++ )
    {
        const FXboxPlayerClassOption& Player = GXboxPlayerClasses(i);
        if( i > 0 && appStricmp(*GXboxPlayerClasses(i-1).Label, *Player.Label) > 0 )
            bSorted = 0;

        if( appStrnicmp(*Player.URLValue, TEXT("UTPS2Characters."), 16) == 0 && Player.SkinValue.Len() )
        {
            const TCHAR Last = (*Player.SkinValue)[Player.SkinValue.Len()-1];
            if( Last >= TEXT('0') && Last <= TEXT('9') )
                NumberedPS2SkinPrefixes++;
        }

        for( INT j=0; j<i; j++ )
        {
            const FXboxPlayerClassOption& Other = GXboxPlayerClasses(j);
            if( appStricmp(*Other.URLValue, *Player.URLValue) == 0
            &&  appStricmp(*Other.SkinValue, *Player.SkinValue) == 0
            &&  appStricmp(*Other.FaceValue, *Player.FaceValue) == 0 )
                DuplicateAppearances++;
        }

        GXboxLog.Write( "XCHAR index=%d label=%s class=%s skin=%s face=%s portrait=%s",
            i,
            TCHAR_TO_ANSI(*Player.Label),
            TCHAR_TO_ANSI(*Player.URLValue),
            TCHAR_TO_ANSI(*Player.SkinValue),
            Player.FaceValue.Len() ? TCHAR_TO_ANSI(*Player.FaceValue) : "",
            Player.PortraitName );
    }

    GXboxLog.Write( "XCHAR audit count=%d sorted=%d duplicateAppearances=%d numberedPS2SkinPrefixes=%d",
        GXboxPlayerClasses.Num(), bSorted ? 1 : 0, DuplicateAppearances, NumberedPS2SkinPrefixes );
}

static void XboxMenuAddKnownBonusPlayerClass( const FRegistryObjectInfo& Info )
{
    if( XboxMenuHasPlayerClass( Info.Object ) )
        return;

    if( appStricmp( *Info.Object, TEXT("MultiMesh.TSkaarj") ) == 0 )
    {
        XboxMenuAddPlayerClassOption( TEXT("SKAARJ HYBRID"), TEXT("MultiMesh.TSkaarj"), TEXT("TSkaarj"), TEXT("EpicCustomModels.TSkM"), TEXT("EpicCustomModels.TSkM"), TEXT("BotPack.VoiceMale"), TEXT("MultiMesh.SkaarjVoice"), TEXT("TSkMSkins."), TEXT("TSkMSkins.Warr"), 0, 1, 2, 3, 1 );
        return;
    }
    if( appStricmp( *Info.Object, TEXT("MultiMesh.TNali") ) == 0 )
    {
        XboxMenuAddPlayerClassOption( TEXT("NALI"), TEXT("MultiMesh.TNali"), TEXT("TNali"), TEXT("EpicCustomModels.TNaliMesh"), TEXT("EpicCustomModels.TNaliMesh"), TEXT("BotPack.VoiceMale"), TEXT("MultiMesh.NaliVoice"), TEXT("TNaliMeshSkins."), TEXT("TNaliMeshSkins.Ouboudah"), 0, 1, 2, 3, 0 );
        return;
    }
    if( appStricmp( *Info.Object, TEXT("MultiMesh.TCow") ) == 0 )
    {
        XboxMenuAddPlayerClassOption( TEXT("NALI WARCOW"), TEXT("MultiMesh.TCow"), TEXT("TCow"), TEXT("EpicCustomModels.TCowMesh"), TEXT("EpicCustomModels.TCowMesh"), TEXT("BotPack.VoiceMale"), TEXT("MultiMesh.CowVoice"), TEXT("TCowMeshSkins."), TEXT("TCowMeshSkins.WarCow"), 0, 1, 2, 3, 0 );
        return;
    }

    GXboxLog.Write( "XMENU skipped unknown player class metadata=%s description=%s",
        TCHAR_TO_ANSI(*Info.Object),
        TCHAR_TO_ANSI(*Info.Description) );
}

static void XboxMenuDiscoverKnownBonusPlayerClasses()
{
    TArray<FRegistryObjectInfo> Players;
    XboxMenuCollectIntObjects( Players, TEXT("Class"), TEXT("TournamentPlayer") );
    for( INT i=0; i<Players.Num(); i++ )
        XboxMenuAddKnownBonusPlayerClass( Players(i) );
}
#endif

static void XboxMenuLoadPlayerClasses()
{
    if( GXboxPlayerListsLoaded )
        return;

    GXboxPlayerListsLoaded = 1;
    GXboxPlayerClasses.Empty();

#if TARGET_XBOX
    XboxMenuAddKnownPlayerCharacters();
    XboxMenuSortPlayerCharacters();
    XboxMenuAuditPlayerCharacters();

    GXboxLog.Write( "XMENU using Xbox lightweight player character metadata=%d", GXboxPlayerClasses.Num() );
    return;
#endif

    XboxMenuEnsureRegistryCache();

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
            XboxMenuClassDefaultString( PlayerClass, TEXT("DefaultPackage"), Option.DefaultPackage );
            XboxMenuClassDefaultString( PlayerClass, TEXT("DefaultSkinName"), Option.DefaultSkinName );
            Option.FixedSkin = XboxMenuClassDefaultInt( PlayerClass, TEXT("FixedSkin"), 2 );
            Option.FaceSkin = XboxMenuClassDefaultInt( PlayerClass, TEXT("FaceSkin"), 3 );
            Option.TeamSkin1 = XboxMenuClassDefaultInt( PlayerClass, TEXT("TeamSkin1"), 0 );
            Option.TeamSkin2 = XboxMenuClassDefaultInt( PlayerClass, TEXT("TeamSkin2"), 1 );
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

static void XboxMenuAppendInt( FString& Value, INT Number );

static INT XboxMenuFirstDigit( const FString& Value )
{
    for( INT i=0; i<Value.Len(); i++ )
        if( (*Value)[i] >= '0' && (*Value)[i] <= '9' )
            return i;
    return -1;
}

static UBOOL XboxMenuTextureObjectExists( const TArray<FRegistryObjectInfo>& Textures, const FString& ObjectName )
{
    for( INT i=0; i<Textures.Num(); i++ )
        if( appStricmp( *Textures(i).Object, *ObjectName ) == 0 )
            return 1;
    return 0;
}

static UBOOL XboxMenuBuildSkinRoot( const FXboxPlayerClassOption& Player, const TArray<FRegistryObjectInfo>& Textures, const FString& FullTextureName, FString& OutSkinRoot, UBOOL& bOutMultiSkinGroup )
{
    FString Item;
    FString Prefix;
    XboxMenuItemName( FullTextureName, Item );
    XboxMenuPackagePrefix( FullTextureName, Prefix );
    if( appStrnicmp( *Item, TEXT("T_"), 2 ) == 0 )
        return 0;

    bOutMultiSkinGroup = 0;
    if( !Player.bMultiSkinned )
    {
        OutSkinRoot = FullTextureName;
        return 1;
    }

    INT Digit = XboxMenuFirstDigit( Item );
    if( Digit >= 0 )
    {
        if( (*Item)[Digit] != '1' || Digit + 1 != Item.Len() )
            return 0;
        OutSkinRoot = Prefix + Item.Left(Digit);
        bOutMultiSkinGroup = 1;
        return 1;
    }

    FString Companion = Prefix + Item;
    Companion += TEXT("2");
    if( XboxMenuTextureObjectExists( Textures, Companion ) )
    {
        OutSkinRoot = Prefix + Item;
        bOutMultiSkinGroup = 1;
        return 1;
    }

    OutSkinRoot = FullTextureName;
    return 1;
}

static void XboxMenuAddUniqueDiscoveredOption( TArray<FXboxDiscoveredOption>& Options, const FString& Label, const FString& URLValue, UBOOL bMultiSkinGroup )
{
    for( INT Existing=0; Existing<Options.Num(); Existing++ )
        if( appStricmp( *Options(Existing).URLValue, *URLValue ) == 0 )
            return;

    FXboxDiscoveredOption& Option = *new(Options)FXboxDiscoveredOption;
    Option.Label = Label;
    Option.URLValue = URLValue;
    Option.bMultiSkinGroup = bMultiSkinGroup;
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

#if TARGET_XBOX
    FXboxDiscoveredOption& XboxSkin = *new(GXboxPlayerSkins)FXboxDiscoveredOption;
    XboxSkin.Label = TEXT("DEFAULT");
    XboxSkin.URLValue = Player.SkinValue.Len() ? Player.SkinValue : Player.DefaultSkinName;
    if( XboxSkin.URLValue.Len() == 0 )
        XboxSkin.URLValue = TEXT("SoldierSkins.blkt");
    XboxSkin.bMultiSkinGroup = Player.bMultiSkinned;
    GXboxLog.Write( "XMENU using fixed Xbox skin for character=%s skin=%s",
        TCHAR_TO_ANSI(*Player.Label),
        TCHAR_TO_ANSI(*XboxSkin.URLValue) );
    return;
#endif

    if( Player.DefaultPackage.Len() > 0 )
    {
        TArray<FRegistryObjectInfo> Textures;
        UObject::GetRegistryObjects( Textures, UTexture::StaticClass(), NULL, 0 );
        if( Textures.Num() <= 1 )
            XboxMenuCollectIntObjects( Textures, TEXT("Texture"), NULL, *Player.DefaultPackage );

        for( INT i=0; i<Textures.Num(); i++ )
        {
            if( Textures(i).Description.Len() == 0 )
                continue;

            FString SkinRoot;
            UBOOL bMultiSkinGroup = 0;
            if( !XboxMenuBuildSkinRoot( Player, Textures, Textures(i).Object, SkinRoot, bMultiSkinGroup ) )
                continue;

            FString Label;
            XboxMenuStripDescriptionLabel( Textures(i).Description, Label );
            XboxMenuAddUniqueDiscoveredOption( GXboxPlayerSkins, Label, SkinRoot, bMultiSkinGroup );
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

    GXboxLog.Write( "XMENU discovered %d skins for player=%s package=%s",
        GXboxPlayerSkins.Num(), TCHAR_TO_ANSI(*Player.URLValue), TCHAR_TO_ANSI(*Player.DefaultPackage) );
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

#if TARGET_XBOX
    FXboxDiscoveredOption& XboxFace = *new(GXboxPlayerFaces)FXboxDiscoveredOption;
    XboxFace.Label = TEXT("DEFAULT");
    XboxFace.URLValue = Player.FaceValue;
    GXboxLog.Write( "XMENU using fixed Xbox face for character=%s face=%s",
        TCHAR_TO_ANSI(*Player.Label),
        XboxFace.URLValue.Len() ? TCHAR_TO_ANSI(*XboxFace.URLValue) : "" );
    return;
#endif

    if( Player.bMultiSkinned && GXboxPlayerSkins(SkinIndex).bMultiSkinGroup )
    {
        FString SkinItem;
        XboxMenuItemName( GXboxPlayerSkins(SkinIndex).URLValue, SkinItem );

        TArray<FRegistryObjectInfo> Textures;
        UObject::GetRegistryObjects( Textures, UTexture::StaticClass(), NULL, 0 );
        if( Textures.Num() <= 1 )
            XboxMenuCollectIntObjects( Textures, TEXT("Texture"), NULL, *Player.DefaultPackage );

        FString FacePrefix = SkinItem;
        XboxMenuAppendInt( FacePrefix, Player.FaceSkin + 1 );

        for( INT i=0; i<Textures.Num(); i++ )
        {
            if( Textures(i).Description.Len() == 0 )
                continue;

            FString Item;
            FString Prefix;
            XboxMenuItemName( Textures(i).Object, Item );
            XboxMenuPackagePrefix( Textures(i).Object, Prefix );
            if( appStrnicmp( *Item, *FacePrefix, FacePrefix.Len() ) != 0 || Item.Len() <= FacePrefix.Len() )
                continue;

            FString Label;
            FString FaceSuffix = Prefix + Item.Mid(FacePrefix.Len());
            XboxMenuStripDescriptionLabel( Textures(i).Description, Label );
            XboxMenuAddUniqueDiscoveredOption( GXboxPlayerFaces, Label, FaceSuffix, 0 );
        }
    }

    if( GXboxPlayerFaces.Num() == 0 )
    {
        FXboxDiscoveredOption& Option = *new(GXboxPlayerFaces)FXboxDiscoveredOption;
        Option.Label = TEXT("DEFAULT");
        Option.URLValue = TEXT("");
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

#if TARGET_XBOX
    FXboxDiscoveredOption& Option = *new(GXboxPlayerVoices)FXboxDiscoveredOption;
    Option.Label = TEXT("DEFAULT");
    Option.URLValue = Player.DefaultVoice.Len() ? Player.DefaultVoice : FString(TEXT("BotPack.VoiceMaleOne"));
    GXboxLog.Write( "XMENU using fixed Xbox voices for player=%s",
        TCHAR_TO_ANSI(*Player.URLValue) );
    return;
#endif

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

static void XboxMenuNormalizePlayerSetupState()
{
    XboxMenuLoadPlayerClasses();
    GXboxMenu.PlayerClass = Clamp<INT>( GXboxMenu.PlayerClass, 0, GXboxPlayerClasses.Num()-1 );
    XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
    GXboxMenu.PlayerSkin = Clamp<INT>( GXboxMenu.PlayerSkin, 0, GXboxPlayerSkins.Num()-1 );
    XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
    GXboxMenu.PlayerFace = Clamp<INT>( GXboxMenu.PlayerFace, 0, GXboxPlayerFaces.Num()-1 );
    XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
    GXboxMenu.PlayerVoice = Clamp<INT>( GXboxMenu.PlayerVoice, 0, GXboxPlayerVoices.Num()-1 );
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
    XboxMenuNormalizePlayerSetupState();

    TCHAR TeamValue[16];
    appSprintf( TeamValue, TEXT("%i"), Clamp<INT>(GXboxMenu.PlayerTeam, 0, 255) );
    XboxMenuSaveDefaultPlayerString( TEXT("Name"), GXboxProfileName );
    XboxMenuSaveDefaultPlayerString( TEXT("Character"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).Label );
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
    XboxProfileLoadDirectory();
    TCHAR ProfileSection[32];
    const TCHAR* PlayerSection = TEXT("DefaultPlayer");
    if( XboxProfileActiveCreated() )
    {
        XboxProfileSectionName( GXboxActiveProfile, ProfileSection, ARRAY_COUNT(ProfileSection) );
        PlayerSection = ProfileSection;
        appStrncpy( GXboxProfileName, GXboxProfiles[GXboxActiveProfile].Name, ARRAY_COUNT(GXboxProfileName) );
    }
    else
    {
        appStrncpy( GXboxProfileName, XboxMenuUserString(TEXT("DefaultPlayer"), TEXT("Name"), TEXT("PLAYER 1")), ARRAY_COUNT(GXboxProfileName) );
    }
    GXboxProfileName[ARRAY_COUNT(GXboxProfileName)-1] = 0;

    FString CharacterValue = XboxMenuUserString( PlayerSection, TEXT("Character"), TEXT("") );
    FString ClassValue = XboxMenuUserString( PlayerSection, TEXT("Class"), TEXT("Botpack.TMale1") );
    FString SkinValue = XboxMenuUserString( PlayerSection, TEXT("Skin"), TEXT("CommandoSkins.cmdo") );
    FString FaceValue = XboxMenuUserString( PlayerSection, TEXT("Face"), TEXT("CommandoSkins.Blake") );
    GXboxMenu.PlayerClass = XboxMenuFindPlayerCharacter( ClassValue, SkinValue, FaceValue, CharacterValue );

    XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
    GXboxMenu.PlayerSkin = XboxMenuFindURLValue( GXboxPlayerSkins, SkinValue );

    XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
    GXboxMenu.PlayerFace = XboxMenuFindURLValue( GXboxPlayerFaces, FaceValue );

    XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
    FString VoiceValue = XboxMenuUserString( PlayerSection, TEXT("Voice"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultVoice );
    GXboxMenu.PlayerVoice = XboxMenuFindURLValue( GXboxPlayerVoices, VoiceValue );

    GXboxMenu.PlayerTeam = appAtoi( XboxMenuUserString( PlayerSection, TEXT("Team"), TEXT("255") ) );
    GXboxMenu.PlayerTeam = Clamp<INT>( GXboxMenu.PlayerTeam, 0, 255 );
}

static void XboxMakeURLSafePlayerName( const TCHAR* In, TCHAR* Out, INT OutCount )
{
    if( !Out || OutCount <= 0 )
        return;

    INT OutLen = 0;
    if( In )
    {
        for( INT i=0; In[i] && OutLen<OutCount-1; i++ )
        {
            TCHAR C = In[i];
            UBOOL bSafe =
                (C >= 'A' && C <= 'Z')
            ||  (C >= 'a' && C <= 'z')
            ||  (C >= '0' && C <= '9')
            ||  C == '_'
            ||  C == '-'
            ||  C == '.';
            Out[OutLen++] = bSafe ? C : '_';
        }
    }

    if( OutLen <= 0 )
    {
        appStrncpy( Out, TEXT("Player"), OutCount );
        Out[OutCount-1] = 0;
        return;
    }
    Out[OutLen] = 0;
}

static void XboxMenuBuildPlayerURL( TCHAR* Out, INT OutCount )
{
    XboxMenuLoadPlayerState();
    XboxMenuSaveDefaultPlayer();

    TCHAR URLName[ARRAY_COUNT(GXboxProfileName)];
    XboxMakeURLSafePlayerName( GXboxProfileName, URLName, ARRAY_COUNT(URLName) );

    if( appStrcmp(URLName, GXboxProfileName) != 0 )
        GXboxLog.Write( "XPROFILE URL-safe gameplay name profile=%s url=%s",
            TCHAR_TO_ANSI(GXboxProfileName), TCHAR_TO_ANSI(URLName) );

    appSprintf
    (
        Out,
        TEXT("?Name=%s?Class=%s?Skin=%s?Face=%s?Voice=%s?Team=%i"),
        URLName,
        *GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue,
        *GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue,
        *GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue,
        *GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue,
        Clamp<INT>(GXboxMenu.PlayerTeam, 0, 255)
    );
    Out[OutCount-1] = 0;
}

static UBOOL XboxSplitProfileUsedByOther( INT Port, INT ProfileIndex )
{
    for( INT i=0; i<4; i++ )
        if( i != Port && GXboxSplitReadySlots[i].Joined && GXboxSplitReadySlots[i].Profile == ProfileIndex )
            return 1;
    return 0;
}

static INT XboxSplitFindUnusedCreatedProfile( INT Port, INT StartProfile, INT Delta )
{
    XboxProfileLoadDirectory();
    INT Profile = Clamp<INT>( StartProfile, 0, XBOX_PROFILE_COUNT-1 );
    for( INT Step=0; Step<XBOX_PROFILE_COUNT; Step++ )
    {
        if( GXboxProfiles[Profile].Created && !XboxSplitProfileUsedByOther(Port, Profile) )
            return Profile;
        Profile = XboxMenuWrapInt( Profile, Delta >= 0 ? 1 : -1, XBOX_PROFILE_COUNT );
    }
    return -1;
}

static void XboxProfileCreateMultiplayerSlot( UXboxViewport* Viewport, INT ProfileIndex )
{
    ProfileIndex = Clamp<INT>( ProfileIndex, 0, XBOX_PROFILE_COUNT-1 );
    XboxMenuLoadPlayerState();
    XboxMenuNormalizePlayerSetupState();
    TCHAR Section[32];
    TCHAR ProfileName[32];
    TCHAR TeamValue[16];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    appSprintf( ProfileName, TEXT("PLAYER %i"), ProfileIndex + 1 );
    appSprintf( TeamValue, TEXT("%i"), Clamp<INT>(GXboxMenu.PlayerTeam, 0, 255) );
    XboxProfileSetInt( Section, TEXT("Created"), 1 );
    GConfig->SetString( Section, TEXT("Name"), ProfileName, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Character"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).Label, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Class"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Skin"), *GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Face"), *GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Voice"), *GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Team"), TeamValue, TEXT("User.ini") );
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    XboxProfileSaveClientConfig( Client, Section );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    XboxProfileSetInt( Section, TEXT("WeaponHand"), XboxMenuWeaponHandIndex(Player) );
    XboxProfileSetInt( Section, TEXT("AutoSwitch"), Player && Player->bNeverAutoSwitch ? 0 : 1 );
    GConfig->Flush( 0, TEXT("User.ini") );
    GXboxProfiles[ProfileIndex].Created = 1;
    appStrncpy( GXboxProfiles[ProfileIndex].Name, ProfileName, ARRAY_COUNT(GXboxProfiles[ProfileIndex].Name) );
    GXboxProfiles[ProfileIndex].Name[ARRAY_COUNT(GXboxProfiles[ProfileIndex].Name)-1] = 0;
    GXboxLog.Write( "XPROFILE multiplayer created slot=%d name=%s", ProfileIndex + 1, TCHAR_TO_ANSI(ProfileName) );
}

static void XboxSplitAssignProfile( INT Port, INT ProfileIndex, UXboxClient* Client )
{
    Port = Clamp<INT>( Port, 0, 3 );
    ProfileIndex = Clamp<INT>( ProfileIndex, 0, XBOX_PROFILE_COUNT-1 );
    TCHAR Section[32];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    FString CharacterValue = XboxMenuUserString( Section, TEXT("Character"), TEXT("") );
    FString ClassValue = XboxMenuUserString( Section, TEXT("Class"), TEXT("Botpack.TMale1") );
    FString SkinValue = XboxMenuUserString( Section, TEXT("Skin"), TEXT("CommandoSkins.cmdo") );
    FString FaceValue = XboxMenuUserString( Section, TEXT("Face"), TEXT("CommandoSkins.Blake") );
    GXboxSplitReadySlots[Port].Profile = ProfileIndex;
    GXboxSplitReadySlots[Port].Character = XboxMenuFindPlayerCharacter( ClassValue, SkinValue, FaceValue, CharacterValue );
    GXboxSplitReadySlots[Port].Team = Clamp<INT>( XboxProfileConfigInt(Section, TEXT("Team"), 255), 0, 255 );
    XboxSplitLoadProfileControls( Port, ProfileIndex, Client );
    GXboxLog.Write( "XPROFILE multiplayer assigned port=%d slot=%d name=%s character=%d team=%d",
        Port + 1,
        ProfileIndex + 1,
        TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name),
        GXboxSplitReadySlots[Port].Character,
        GXboxSplitReadySlots[Port].Team );
}

static void XboxSplitSaveProfileIdentity( INT Port )
{
    Port = Clamp<INT>( Port, 0, 3 );
    INT ProfileIndex = GXboxSplitReadySlots[Port].Profile;
    if( !GConfig
    ||  ProfileIndex < 0
    ||  ProfileIndex >= XBOX_PROFILE_COUNT
    ||  !GXboxProfiles[ProfileIndex].Created )
        return;

    const FXboxPlayerClassOption& Player = XboxSplitReadyPlayerClass( Port );
    TCHAR Section[32];
    TCHAR TeamValue[16];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    appSprintf( TeamValue, TEXT("%i"), Clamp<INT>(GXboxSplitReadySlots[Port].Team, 0, 255) );
    GConfig->SetString( Section, TEXT("Character"), *Player.Label, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Class"), *Player.URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Skin"), *Player.SkinValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Face"), *Player.FaceValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Voice"), *Player.DefaultVoice, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Team"), TeamValue, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
    GXboxLog.Write( "XPROFILE multiplayer identity saved port=%d slot=%d name=%s character=%s team=%d",
        Port + 1,
        ProfileIndex + 1,
        TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name),
        TCHAR_TO_ANSI(*Player.Label),
        GXboxSplitReadySlots[Port].Team );
}

static UBOOL XboxSplitEnsureProfileForJoin( UXboxViewport* Viewport, INT Port, UBOOL bCreateIfMissing )
{
    XboxProfileLoadDirectory();
    INT ProfileIndex = XboxSplitFindUnusedCreatedProfile( Port, GXboxActiveProfile, 1 );
    if( ProfileIndex < 0 && bCreateIfMissing )
    {
        for( INT i=0; i<XBOX_PROFILE_COUNT; i++ )
        {
            if( !GXboxProfiles[i].Created )
            {
                XboxProfileCreateMultiplayerSlot( Viewport, i );
                ProfileIndex = i;
                break;
            }
        }
    }
    if( ProfileIndex < 0 || XboxSplitProfileUsedByOther(Port, ProfileIndex) )
        return 0;
    XboxSplitAssignProfile( Port, ProfileIndex, XboxMenuGetClient(Viewport) );
    return 1;
}

static UBOOL XboxSplitReadyActivatePrimary( UXboxViewport* Viewport, UBOOL bCreateIfMissing=0 )
{
    XboxSplitReadyEnsure();
    if( !XboxSplitEnsureProfileForJoin(Viewport, 0, bCreateIfMissing) )
    {
        GXboxLog.Write( "XSPLIT P1 activation blocked: no available profile" );
        return 0;
    }

    GXboxSplitReadySlots[0].Joined = 1;
    GXboxSplitReadySlots[0].Locked = 0;
    GXboxSplitReadySlots[0].Focus = 0;
    GXboxLog.Write( "XSPLIT fixed slot active physical=P1 profile=%d name=%s",
        GXboxSplitReadySlots[0].Profile + 1,
        TCHAR_TO_ANSI(GXboxProfiles[GXboxSplitReadySlots[0].Profile].Name) );
    return 1;
}

static void XboxSplitReadyReset( UXboxViewport* Viewport, UBOOL bPersistProfile )
{
    if( Viewport )
        XboxProfileOpen( Viewport, bPersistProfile );
    else
        XboxMenuLoadPlayerState();
    XboxMenuLoadPlayerClasses();

    INT CharacterCount = Max<INT>( 1, GXboxPlayerClasses.Num() );
    for( INT i=0; i<4; i++ )
    {
        GXboxSplitReadySlots[i].Joined = 0;
        GXboxSplitReadySlots[i].Locked = 0;
        GXboxSplitReadySlots[i].Profile = -1;
        GXboxSplitReadySlots[i].Character = XboxMenuWrapInt( GXboxMenu.PlayerClass, i, CharacterCount );
        GXboxSplitReadySlots[i].Team = 255;
        GXboxSplitReadySlots[i].Focus = 0;
        appMemzero( &GXboxSplitProfileControls[i], sizeof(GXboxSplitProfileControls[i]) );
    }

    GXboxSplitReadyInitialized = 1;
    GXboxLog.Write( "XSPLIT ready reset defaultCharacter=%d count=%d", GXboxMenu.PlayerClass, CharacterCount );
}

static void XboxSplitReadyEnsure()
{
    if( !GXboxSplitReadyInitialized )
        XboxSplitReadyReset( NULL );
}

static INT XboxSplitReadyJoinedCount()
{
    XboxSplitReadyEnsure();
    INT Count = 0;
    for( INT i=0; i<4; i++ )
        if( GXboxSplitReadySlots[i].Joined )
            Count++;
    return Count;
}

static UBOOL XboxSplitReadyCanBegin()
{
    XboxSplitReadyEnsure();
    XboxProfileLoadDirectory();
    INT Joined = 0;
    for( INT i=0; i<4; i++ )
    {
        if( !GXboxSplitReadySlots[i].Joined )
            continue;
        Joined++;
        INT Profile = GXboxSplitReadySlots[i].Profile;
        if( !GXboxSplitReadySlots[i].Locked
        ||  Profile < 0
        ||  Profile >= XBOX_PROFILE_COUNT
        ||  !GXboxProfiles[Profile].Created )
            return 0;
        for( INT Other=0; Other<i; Other++ )
            if( GXboxSplitReadySlots[Other].Joined && GXboxSplitReadySlots[Other].Profile == Profile )
                return 0;
    }
    return Joined > 0;
}

static const FXboxPlayerClassOption& XboxSplitReadyPlayerClass( INT Port )
{
    XboxSplitReadyEnsure();
    XboxMenuLoadPlayerClasses();
    INT Count = Max<INT>( 1, GXboxPlayerClasses.Num() );
    INT Character = Clamp<INT>( GXboxSplitReadySlots[Clamp<INT>(Port,0,3)].Character, 0, Count-1 );
    return XboxMenuPlayerClass( Character );
}

static void XboxSplitBuildPlayerURLForSlot( INT Port, TCHAR* Out, INT OutCount, UBOOL bForceDummy )
{
    XboxSplitReadyEnsure();
    Port = Clamp<INT>( Port, 0, 3 );
    const FXboxPlayerClassOption& Player = XboxSplitReadyPlayerClass( Port );
    INT Team = Clamp<INT>( GXboxSplitReadySlots[Port].Team, 0, 255 );

    if( bForceDummy || !GXboxSplitReadySlots[Port].Joined )
    {
        appSprintf
        (
            Out,
            TEXT("?Name=Dummy%i?Class=%s?Skin=%s?Face=%s?Voice=%s?Team=%i"),
            Port + 1,
            *Player.URLValue,
            *Player.SkinValue,
            *Player.FaceValue,
            *Player.DefaultVoice,
            Team
        );
    }
    else
    {
        INT ProfileIndex = GXboxSplitReadySlots[Port].Profile;
        if( ProfileIndex < 0 || ProfileIndex >= XBOX_PROFILE_COUNT || !GXboxProfiles[ProfileIndex].Created )
        {
            appSprintf( Out, TEXT("?Name=PROFILE_REQUIRED?Team=%i"), Team );
            Out[OutCount-1] = 0;
            GXboxLog.Write( "XPROFILE multiplayer URL blocked port=%d invalidProfile=%d", Port + 1, ProfileIndex );
            return;
        }

        TCHAR Section[32];
        XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
        FString ProfileName = XboxMenuUserString( Section, TEXT("Name"), GXboxProfiles[ProfileIndex].Name );
        FString ProfileClass = XboxMenuUserString( Section, TEXT("Class"), *Player.URLValue );
        FString ProfileSkin = XboxMenuUserString( Section, TEXT("Skin"), *Player.SkinValue );
        FString ProfileFace = XboxMenuUserString( Section, TEXT("Face"), *Player.FaceValue );
        FString ProfileVoice = XboxMenuUserString( Section, TEXT("Voice"), *Player.DefaultVoice );
        TCHAR URLName[XBOX_PROFILE_NAME_MAX+1];
        XboxMakeURLSafePlayerName( *ProfileName, URLName, ARRAY_COUNT(URLName) );
        appSprintf
        (
            Out,
            TEXT("?Name=%s?Class=%s?Skin=%s?Face=%s?Voice=%s?Team=%i"),
            URLName,
            *ProfileClass,
            *ProfileSkin,
            *ProfileFace,
            *ProfileVoice,
            Team
        );
        GXboxLog.Write( "XPROFILE multiplayer URL port=%d slot=%d profileName=%s netName=%s class=%s team=%d",
            Port + 1,
            ProfileIndex + 1,
            TCHAR_TO_ANSI(*ProfileName),
            TCHAR_TO_ANSI(URLName),
            TCHAR_TO_ANSI(*ProfileClass),
            Team );
    }
    Out[OutCount-1] = 0;
}

static void XboxSplitReadyReleaseControllers()
{
    for( INT i=1; i<4; i++ )
    {
        if( GXboxSplitReadyControllerHandles[i] )
        {
            XInputClose( GXboxSplitReadyControllerHandles[i] );
            GXboxSplitReadyControllerHandles[i] = NULL;
        }
        appMemzero( &GXboxSplitReadyControllerState[i], sizeof(GXboxSplitReadyControllerState[i]) );
        appMemzero( &GXboxSplitReadyPrevControllerState[i], sizeof(GXboxSplitReadyPrevControllerState[i]) );
    }
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

static void XboxMenuDrawTexture( UCanvas* Canvas, UTexture* Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL );

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

static UBOOL XboxMenuIsBossPlayerValue( const FString& PlayerValue )
{
    return appStricmp( *PlayerValue, TEXT("Botpack.TBoss") ) == 0
        || appStricmp( *PlayerValue, TEXT("Botpack.TBossBot") ) == 0;
}

static UBOOL XboxMenuIsCowOrNaliPlayerValue( const FString& PlayerValue )
{
    return appStricmp( *PlayerValue, TEXT("MultiMesh.TNali") ) == 0
        || appStricmp( *PlayerValue, TEXT("MultiMesh.TCow") ) == 0;
}

static UBOOL XboxMenuIsCowPlayerValue( const FString& PlayerValue )
{
    return appStricmp( *PlayerValue, TEXT("MultiMesh.TCow") ) == 0;
}

static UBOOL XboxMenuIsNaliPlayerValue( const FString& PlayerValue )
{
    return appStricmp( *PlayerValue, TEXT("MultiMesh.TNali") ) == 0;
}

static UBOOL XboxMenuIsSkaarjPlayerValue( const FString& PlayerValue )
{
    return appStricmp( *PlayerValue, TEXT("MultiMesh.TSkaarj") ) == 0;
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
    XboxMenuNormalizePlayerSetupState();

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
    FString SkinName = GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue;
    FString FaceName = GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue;

    Actor->Skin = NULL;
    for( INT i=0; i<ARRAY_COUNT(Actor->MultiSkins); i++ )
        Actor->MultiSkins[i] = NULL;

    UBOOL bMultiSkinGroup = GXboxPlayerSkins(GXboxMenu.PlayerSkin).bMultiSkinGroup;
#if TARGET_XBOX
    UClass* PlayerClass = NULL;
#else
    UClass* PlayerClass = FindObject<UClass>( ANY_PACKAGE, *Player.URLValue );
    if( !PlayerClass )
        PlayerClass = UObject::StaticLoadClass( APawn::StaticClass(), NULL, *Player.URLValue, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
#endif

    GXboxLog.Write( "XMENU preview skin class=%s loaded=%s multi=%d group=%d skin=%s face=%s team=%d",
        TCHAR_TO_ANSI(*Player.URLValue),
        PlayerClass ? "yes" : "no",
        Player.bMultiSkinned,
        bMultiSkinGroup,
        TCHAR_TO_ANSI(*SkinName),
        TCHAR_TO_ANSI(*FaceName),
        GXboxMenu.PlayerTeam );

    if( !bMultiSkinGroup )
    {
        if( XboxMenuIsCowPlayerValue( Player.URLValue ) )
        {
            XboxMenuSetSkinElement( Actor, 1, SkinName, Player.DefaultSkinName );
        }
        else if( XboxMenuIsNaliPlayerValue( Player.URLValue ) )
        {
            Actor->Skin = XboxMenuLoadTexture( SkinName );
        }
        else
        {
            Actor->Skin = XboxMenuLoadTexture( SkinName );
        }
        GXboxLog.Write( "XMENU preview skin simple end" );
        return;
    }

#if TARGET_XBOX
    if( XboxMenuIsBossPlayerValue( Player.URLValue ) )
#else
    if( XboxMenuIsBossPlayerClass( PlayerClass ) )
#endif
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

    FString DefaultPackage = Player.DefaultPackage;
    FString DefaultSkinName = Player.DefaultSkinName;
#if !TARGET_XBOX
    XboxMenuClassDefaultString( PlayerClass, TEXT("DefaultPackage"), DefaultPackage );
    XboxMenuClassDefaultString( PlayerClass, TEXT("DefaultSkinName"), DefaultSkinName );
#endif
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

    INT FixedSkin = Player.FixedSkin;
    INT FaceSkin = Player.FaceSkin;
    INT TeamSkin1 = Player.TeamSkin1;
    INT TeamSkin2 = Player.TeamSkin2;
#if !TARGET_XBOX
    FixedSkin = XboxMenuClassDefaultInt( PlayerClass, TEXT("FixedSkin"), FixedSkin );
    FaceSkin = XboxMenuClassDefaultInt( PlayerClass, TEXT("FaceSkin"), FaceSkin );
    TeamSkin1 = XboxMenuClassDefaultInt( PlayerClass, TEXT("TeamSkin1"), TeamSkin1 );
    TeamSkin2 = XboxMenuClassDefaultInt( PlayerClass, TEXT("TeamSkin2"), TeamSkin2 );
#endif

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
    UBOOL bFaceLoaded = XboxMenuSetSkinElement( Actor, FaceSkin, FaceTex, FaceFallback );

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

    GXboxLog.Write( "XMENU preview skin generic end fixedSlot=%d fixed=%d faceSlot=%d face=%d requestedFace=%d",
        FixedSkin, Actor->MultiSkins[FixedSkin] ? 1 : 0,
        FaceSkin, Actor->MultiSkins[FaceSkin] ? 1 : 0, bFaceLoaded ? 1 : 0 );
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

    XboxMenuNormalizePlayerSetupState();
    if( GXboxPlayerPreviewClass == GXboxMenu.PlayerClass
    &&  GXboxPlayerPreviewSkin == GXboxMenu.PlayerSkin
    &&  GXboxPlayerPreviewFace == GXboxMenu.PlayerFace
    &&  GXboxPlayerPreviewTeam == GXboxMenu.PlayerTeam )
        return;

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
    FString MeshName = Player.SelectionMesh.Len() ? Player.SelectionMesh : Player.MeshPath;
    const UBOOL bClassChanged = (GXboxPlayerPreviewClass != GXboxMenu.PlayerClass);
    UMesh* Mesh = Actor->Mesh;
    if( bClassChanged || !Mesh )
    {
        GXboxLog.Write( "XMENU player preview class switch old=%d new=%d",
            GXboxPlayerPreviewClass, GXboxMenu.PlayerClass );
        GXboxLog.Flush();
        XboxMenuPreparePlayerPreviewClassSwitch( Viewport, Actor );

        GXboxLog.Write( "XMENU player preview mesh load begin %s",
            TCHAR_TO_ANSI(*MeshName) );
        Mesh = MeshName.Len()
            ? Cast<UMesh>( UObject::StaticLoadObject( UMesh::StaticClass(), NULL, *MeshName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) )
            : NULL;
        GXboxLog.Write( "XMENU player preview mesh load end %s",
            Mesh ? "OK" : "missing" );
        GXboxLog.Flush();
        Actor->Mesh = Mesh;
    }
    Actor->DrawScale = 0.10f;
    Actor->AmbientGlow = 255;
    Actor->bMeshEnviroMap = 0;
    if( Mesh )
    {
        Actor->AnimSequence = FName(TEXT("Breath3"));
        Actor->AnimFrame = 0.001f;
        Actor->AnimRate = 0.0f;
        Actor->TweenRate = 0.0f;
        Actor->bAnimLoop = 0;
    }
    GXboxLog.Write( "XMENU player preview skin apply begin" );
    GXboxLog.Flush();
    XboxMenuApplyPreviewSkin( Actor );
    GXboxLog.Write( "XMENU player preview skin apply end" );
    GXboxLog.Flush();
    if( XboxMenuTrackPlayerPreviewAssets( Actor, GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin, GXboxMenu.PlayerFace, GXboxMenu.PlayerTeam ) )
        XboxMenuMaybeCollectPlayerPreviewGarbage( Viewport, bClassChanged ? "class" : "skin" );

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

static UTexture* XboxMenuResolvePlayerPreviewPlaceholder()
{
    XboxMenuNormalizePlayerSetupState();

    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
    FString SkinName = GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue;
    FString FaceName = GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue;

    FString SkinItem;
    FString FaceItem;
    FString SkinPackage;
    FString FacePackage;
    XboxMenuItemName( SkinName, SkinItem );
    XboxMenuItemName( FaceName, FaceItem );
    XboxMenuPackagePrefix( SkinName, SkinPackage );
    XboxMenuPackagePrefix( FaceName, FacePackage );

    if( SkinPackage.Len() == 0 )
        SkinPackage = Player.DefaultPackage;
    if( FacePackage.Len() == 0 )
        FacePackage = SkinPackage.Len() ? SkinPackage : Player.DefaultPackage;

    FString TextureName;
    if( XboxMenuIsBossPlayerValue( Player.URLValue ) )
    {
        TextureName = SkinName;
        if( TextureName.Len() == 0 )
            TextureName = Player.DefaultSkinName;
        TextureName += TEXT("5Xan");
    }
    else if( Player.bMultiSkinned && FaceItem.Len() )
    {
        TextureName = FacePackage + SkinItem + TEXT("5") + FaceItem;
    }
    else if( SkinName.Len() )
    {
        TextureName = SkinName;
    }

    UTexture* Texture = TextureName.Len() ? XboxMenuLoadTexture( TextureName ) : NULL;
    GXboxLog.Write( "XMENU player preview placeholder texture=%s %s",
        TextureName.Len() ? TCHAR_TO_ANSI(*TextureName) : "",
        Texture ? "OK" : "missing" );
    return Texture;
}

static const char* XboxMenuCurrentPlayerPortraitName()
{
    XboxMenuNormalizePlayerSetupState();
    const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
    return Player.PortraitName[0] ? Player.PortraitName : "char_missing.xui";
}

static const char* XboxMenuPlayerPortraitName( INT PlayerClass )
{
    XboxMenuLoadPlayerClasses();
    if( GXboxPlayerClasses.Num() <= 0 )
        return "char_missing.xui";
    const FXboxPlayerClassOption& Player = GXboxPlayerClasses(Clamp<INT>(PlayerClass, 0, GXboxPlayerClasses.Num()-1));
    return Player.PortraitName[0] ? Player.PortraitName : "char_missing.xui";
}

static UBOOL XboxMenuDrawPlayerPortrait( UCanvas* Canvas, INT PlayerClass, FLOAT X, FLOAT Y, FLOAT W, FLOAT H )
{
#if TARGET_XBOX
    if( !Canvas || !Canvas->Frame )
        return 0;
    const char* PortraitName = XboxMenuPlayerPortraitName( PlayerClass );
    if( !PortraitName || !PortraitName[0] )
        return 0;

    FLOAT DrawH = H;
    FLOAT DrawW = DrawH * (256.0f / 512.0f);
    if( DrawW > W )
    {
        DrawW = W;
        DrawH = DrawW * (512.0f / 256.0f);
    }
    if( DrawW <= 0.0f || DrawH <= 0.0f )
        return 0;

    FLOAT DrawX = X + (W - DrawW) * 0.5f;
    FLOAT DrawY = Y + (H - DrawH) * 0.5f;
    return XboxRenderDrawMenuTexture( Canvas->Frame, PortraitName, DrawX, DrawY, DrawW, DrawH, 1.0f );
#else
    return 0;
#endif
}

static void XboxMenuReleaseProfilePreviewPortrait()
{
#if TARGET_XBOX
    if( GXboxProfilePreviewPlayerClass >= 0 )
    {
        const char* PortraitName = XboxMenuPlayerPortraitName( GXboxProfilePreviewPlayerClass );
        if( PortraitName && PortraitName[0] )
            XboxRenderReleaseMenuTexture( PortraitName );
    }
#endif
    GXboxProfilePreviewPlayerClass = -1;
}

static const char* XboxMenuCurrentPlayerPortraitNameRaw()
{
    if( !GXboxPlayerListsLoaded || GXboxPlayerClasses.Num() <= 0 )
        return "";

    INT Index = Clamp<INT>( GXboxMenu.PlayerClass, 0, GXboxPlayerClasses.Num()-1 );
    const FXboxPlayerClassOption& Player = GXboxPlayerClasses(Index);
    return Player.PortraitName[0] ? Player.PortraitName : "";
}

static void XboxMenuReleaseCurrentPlayerPortrait()
{
#if TARGET_XBOX
    const char* PortraitName = XboxMenuCurrentPlayerPortraitNameRaw();
    if( PortraitName && PortraitName[0] )
        XboxRenderReleaseMenuTexture( PortraitName );
#endif
}

static UBOOL XboxMenuDrawPlayerPreviewActor( UXboxViewport* Viewport, UCanvas* Canvas, FLOAT X, FLOAT Y, FLOAT W, FLOAT H )
{
    if( !Viewport || !Canvas || !Canvas->Frame || !Canvas->Render || !Viewport->Actor || !Viewport->RenDev )
        return 0;

#if TARGET_XBOX
    const char* PortraitName = XboxMenuCurrentPlayerPortraitName();
    if( !PortraitName || !PortraitName[0] )
        return 0;

    FLOAT DrawH = H;
    FLOAT DrawW = DrawH * (256.0f / 512.0f);
    if( DrawW > W )
    {
        DrawW = W;
        DrawH = DrawW * (512.0f / 256.0f);
    }
    if( DrawW <= 0.0f || DrawH <= 0.0f )
        return 0;

    FLOAT DrawX = X + (W - DrawW) * 0.5f;
    FLOAT DrawY = Y + (H - DrawH) * 0.5f;
    return XboxRenderDrawMenuTexture( Canvas->Frame, PortraitName, DrawX, DrawY, DrawW, DrawH, 1.0f );
#endif

    XboxMenuUpdatePlayerPreviewActor( Viewport );
    AActor* Actor = XboxMenuGetPlayerPreviewActor( Viewport );
    if( !Actor || !Actor->Mesh )
        return 0;

    FLOAT OldFov = Viewport->Actor->FovAngle;
    Viewport->Actor->FovAngle = 30.0f;
    FLOAT FovRadians = Viewport->Actor->FovAngle * PI / 180.0f;
    Actor->Location = FVector( 4.0f / appTan(FovRadians * 0.5f), 0.0f, 0.0f );
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
    XboxRenderBeginMenuMeshSlot( Canvas->Frame, X, Y, W, H );
    Canvas->Render->DrawActor( Canvas->Frame, Actor );
    XboxRenderEndMenuMeshSlot( Canvas->Frame );
    Actor->bHidden = bOldHidden;
    Viewport->Actor->RendMap = OldRendMap;

    Canvas->Frame->X = OldX;
    Canvas->Frame->Y = OldY;
    Canvas->Frame->XB = OldXB;
    Canvas->Frame->YB = OldYB;
    Canvas->Frame->ComputeRenderSize();
    Viewport->Actor->FovAngle = OldFov;
    return 1;
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
    {
        GXboxLog.Write( "XMENU voice sample skipped class=%s loaded=%d defaults=%d",
            TCHAR_TO_ANSI(*GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue),
            VoiceClass ? 1 : 0,
            VoiceClass ? VoiceClass->Defaults.Num() : 0 );
        return;
    }

    INT NumAcks = XboxMenuClassDefaultInt( VoiceClass, TEXT("NumAcks"), 0 );
    UProperty* AckProp = FindField<UProperty>( VoiceClass, TEXT("AckSound") );
    UObjectProperty* AckObjectProp = Cast<UObjectProperty>( AckProp );
    if( !AckObjectProp || NumAcks <= 0 )
    {
        GXboxLog.Write( "XMENU voice sample skipped class=%s NumAcks=%d AckProp=%s",
            TCHAR_TO_ANSI(*GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue),
            NumAcks,
            AckProp ? TCHAR_TO_ANSI(AckProp->GetName()) : "None" );
        return;
    }

    INT AckIndex = appRand() % Min<INT>( NumAcks, AckProp->ArrayDim );
    BYTE* AckData = &VoiceClass->Defaults(0) + AckProp->Offset + AckIndex * AckProp->ElementSize;
    USound* Sound = *(USound**)AckData;
    if( !Sound )
    {
        GXboxLog.Write( "XMENU voice sample skipped class=%s ack=%d sound=None",
            TCHAR_TO_ANSI(*GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue),
            AckIndex );
        return;
    }

    GXboxMenuVoiceSampleBypass++;
    UBOOL bPlayed = Client->Engine->Audio->PlaySound( Viewport->Actor, SLOT_Interface, Sound, Viewport->Actor->Location, 16.0f, 1600.0f, 1.0f );
    GXboxMenuVoiceSampleBypass--;
    GXboxLog.Write( "XMENU voice sample class=%s ack=%d sound=%s played=%d",
        TCHAR_TO_ANSI(*GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue),
        AckIndex,
        Sound ? Sound->GetName() : "None",
        bPlayed ? 1 : 0 );
}

static void XboxMenuFormatTripletColor( INT ColorIndex, TCHAR* Value, INT ValueCount )
{
    if( !Value || ValueCount <= 0 )
        return;
    ColorIndex = Clamp<INT>( ColorIndex, 0, ARRAY_COUNT(GXboxColorNames)-1 );
    appSprintf( Value, TEXT("(R=%i,G=%i,B=%i)"),
        GXboxColorTriples[ColorIndex][0],
        GXboxColorTriples[ColorIndex][1],
        GXboxColorTriples[ColorIndex][2] );
    Value[ValueCount-1] = 0;
}

static void XboxMenuSaveTripletColor( const TCHAR* Key, INT ColorIndex )
{
    if( !GConfig )
        return;
    TCHAR Value[64];
    XboxMenuFormatTripletColor( ColorIndex, Value, ARRAY_COUNT(Value) );
    GConfig->SetString( TEXT("Botpack.ChallengeHUD"), Key, Value, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
}

static INT XboxMenuLoadTripletColor( const TCHAR* Key, INT DefaultIndex )
{
    if( !GConfig )
        return DefaultIndex;

    FString Value;
    if( !GConfig->GetString( TEXT("Botpack.ChallengeHUD"), Key, Value, TEXT("User.ini") ) )
        return DefaultIndex;

    INT R = -1;
    INT G = -1;
    INT B = -1;
    Parse( *Value, TEXT("R="), R );
    Parse( *Value, TEXT("G="), G );
    Parse( *Value, TEXT("B="), B );
    for( INT i=0; i<ARRAY_COUNT(GXboxColorTriples); i++ )
        if( GXboxColorTriples[i][0] == R && GXboxColorTriples[i][1] == G && GXboxColorTriples[i][2] == B )
            return i;
    return DefaultIndex;
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
        GXboxSettingsHudColor = XboxMenuLoadTripletColor( TEXT("FavoriteHUDColor"), GXboxSettingsHudColor );
        GXboxSettingsCrosshairColor = XboxMenuLoadTripletColor( TEXT("CrosshairColor"), GXboxSettingsCrosshairColor );
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
    if( !GXboxSplitActive )
        Player->SaveConfig();
}

static INT XboxProfileForViewport( UXboxViewport* Viewport )
{
    XboxProfileLoadDirectory();
    if( GXboxSplitActive && Viewport )
    {
        INT Port = Clamp<INT>( XboxViewportIndex(Viewport), 0, 3 );
        INT ProfileIndex = GXboxSplitReadySlots[Port].Profile;
        if( GXboxSplitReadySlots[Port].Joined
        &&  ProfileIndex >= 0
        &&  ProfileIndex < XBOX_PROFILE_COUNT
        &&  GXboxProfiles[ProfileIndex].Created )
            return ProfileIndex;
    }
    return GXboxActiveProfile;
}

static void XboxProfileLoadControlsForContext( UXboxViewport* Viewport )
{
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    INT ProfileIndex = XboxProfileForViewport( Viewport );
    XboxProfileApplyClientConfigForIndex( Client, ProfileIndex, 0 );

    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    if( Player )
    {
        TCHAR Section[32];
        XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
        XboxMenuSetWeaponHand( Player, Clamp<INT>(XboxProfileConfigInt(Section, TEXT("WeaponHand"), XboxMenuWeaponHandIndex(Player)), 0, ARRAY_COUNT(GXboxWeaponHands)-1) );
        Player->bNeverAutoSwitch = XboxProfileConfigInt( Section, TEXT("AutoSwitch"), Player->bNeverAutoSwitch ? 0 : 1 ) == 0;
        Player->bNeverSwitchOnPickup = Player->bNeverAutoSwitch;
    }
    GXboxLog.Write( "XPROFILE controls context loaded viewport=%d slot=%d",
        Viewport ? XboxViewportIndex(Viewport) + 1 : 0, ProfileIndex + 1 );
}

static void XboxProfileSaveControlsForContext( UXboxViewport* Viewport )
{
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    INT ProfileIndex = XboxProfileForViewport( Viewport );
    if( !Client || !GConfig || !GXboxProfiles[ProfileIndex].Created )
        return;

    TCHAR Section[32];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    XboxProfileSaveClientConfig( Client, Section );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    XboxProfileSetInt( Section, TEXT("WeaponHand"), XboxMenuWeaponHandIndex(Player) );
    XboxProfileSetInt( Section, TEXT("AutoSwitch"), Player && Player->bNeverAutoSwitch ? 0 : 1 );
    GConfig->Flush( 0, TEXT("User.ini") );
    if( !GXboxSplitActive )
        Client->SaveConfig();

    if( GXboxSplitActive && Viewport )
        XboxSplitLoadProfileControls( Clamp<INT>(XboxViewportIndex(Viewport), 0, 3), ProfileIndex, Client );
    GXboxLog.Write( "XPROFILE controls context saved viewport=%d slot=%d name=%s preset=%d stick=%d look=%.1f move=%.1f invert=%d deadzone=%.2f actionA=%d global=%d",
        Viewport ? XboxViewportIndex(Viewport) + 1 : 0,
        ProfileIndex + 1,
        TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name),
        Client->ControlPreset,
        Client->StickLayout,
        Client->ScaleRUV,
        Client->ScaleXYZ,
        Client->InvertVertical ? 1 : 0,
        Client->DeadZone,
        XboxControlButtonAction(Client, XCB_A),
        GXboxSplitActive ? 0 : 1 );
}

static void XboxProfileSaveActive( UXboxViewport* Viewport )
{
    XboxProfileLoadDirectory();
    if( !GConfig || !GXboxProfiles[GXboxActiveProfile].Created )
        return;

    XboxMenuNormalizePlayerSetupState();
    TCHAR Section[32];
    TCHAR TeamValue[16];
    XboxProfileSectionName( GXboxActiveProfile, Section, ARRAY_COUNT(Section) );
    appSprintf( TeamValue, TEXT("%i"), Clamp<INT>(GXboxMenu.PlayerTeam, 0, 255) );

    XboxProfileSetInt( Section, TEXT("Created"), 1 );
    GConfig->SetString( Section, TEXT("Name"), GXboxProfileName, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Character"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).Label, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Class"), *GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Skin"), *GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Face"), *GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Voice"), *GXboxPlayerVoices(GXboxMenu.PlayerVoice).URLValue, TEXT("User.ini") );
    GConfig->SetString( Section, TEXT("Team"), TeamValue, TEXT("User.ini") );

    UXboxClient* Client = XboxMenuGetClient( Viewport );
    XboxProfileSaveClientConfig( Client, Section );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    XboxProfileSetInt( Section, TEXT("WeaponHand"), XboxMenuWeaponHandIndex(Player) );
    XboxProfileSetInt( Section, TEXT("AutoSwitch"), Player && Player->bNeverAutoSwitch ? 0 : 1 );
    XboxProfileSetInt( TEXT("XboxProfiles"), TEXT("Active"), GXboxActiveProfile );
    GConfig->Flush( 0, TEXT("User.ini") );

    GXboxProfiles[GXboxActiveProfile].Created = 1;
    appStrncpy( GXboxProfiles[GXboxActiveProfile].Name, GXboxProfileName, ARRAY_COUNT(GXboxProfiles[GXboxActiveProfile].Name) );
    GXboxProfiles[GXboxActiveProfile].Name[ARRAY_COUNT(GXboxProfiles[GXboxActiveProfile].Name)-1] = 0;
    XboxMenuSaveDefaultPlayer();
    if( Client )
        Client->SaveConfig();
    GXboxLog.Write( "XPROFILE saved slot=%d name=%s class=%s team=%d preset=%d",
        GXboxActiveProfile + 1,
        TCHAR_TO_ANSI(GXboxProfileName),
        TCHAR_TO_ANSI(*GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue),
        GXboxMenu.PlayerTeam,
        Client ? Client->ControlPreset : 0 );
    GXboxLog.Flush();
}

static void XboxProfileApplyActive( UXboxViewport* Viewport, UBOOL bPersistGlobal=1 )
{
    DOUBLE ApplyStart = appSeconds();
    XboxProfileLoadDirectory();
    if( !GXboxProfiles[GXboxActiveProfile].Created )
        return;

    char OldPortraitName[64];
    appMemzero( OldPortraitName, sizeof(OldPortraitName) );
    const char* CurrentPortraitName = XboxMenuCurrentPlayerPortraitName();
    if( CurrentPortraitName && CurrentPortraitName[0] )
    {
        appStrncpy( OldPortraitName, CurrentPortraitName, ARRAY_COUNT(OldPortraitName) );
        OldPortraitName[ARRAY_COUNT(OldPortraitName)-1] = 0;
    }

    GXboxPlayerStateLoaded = 0;
    GXboxPlayerSkinsClass = -1;
    GXboxPlayerFacesClass = -1;
    GXboxPlayerVoicesClass = -1;
    XboxMenuLoadPlayerState();
    XboxMenuNormalizePlayerSetupState();
    if( OldPortraitName[0] )
        XboxRenderReleaseMenuTexture( OldPortraitName );

    UXboxClient* Client = XboxMenuGetClient( Viewport );
    if( bPersistGlobal )
        XboxProfileApplyClientConfig( Client );
    else
    {
        // Ordinary local-menu entry is a read/apply operation. On optical
        // media, rewriting global config here can stall the frontend.
        XboxProfileApplyClientConfigForIndex( Client, GXboxActiveProfile, 0 );
    }
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    TCHAR Section[32];
    XboxProfileSectionName( GXboxActiveProfile, Section, ARRAY_COUNT(Section) );
    if( Player )
    {
        XboxMenuSetWeaponHand( Player, Clamp<INT>(XboxProfileConfigInt(Section, TEXT("WeaponHand"), XboxMenuWeaponHandIndex(Player)), 0, ARRAY_COUNT(GXboxWeaponHands)-1) );
        Player->bNeverAutoSwitch = XboxProfileConfigInt( Section, TEXT("AutoSwitch"), Player->bNeverAutoSwitch ? 0 : 1 ) == 0;
        Player->bNeverSwitchOnPickup = Player->bNeverAutoSwitch;
        if( bPersistGlobal )
            Player->SaveConfig();
        if( Player->PlayerReplicationInfo )
            Player->PlayerReplicationInfo->PlayerName = GXboxProfileName;
    }
    if( bPersistGlobal )
        XboxMenuSaveDefaultPlayer();
    if( bPersistGlobal )
        GXboxLog.Write( "XPROFILE loaded slot=%d name=%s class=%s team=%d",
            GXboxActiveProfile + 1,
            TCHAR_TO_ANSI(GXboxProfileName),
            TCHAR_TO_ANSI(*GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue),
            GXboxMenu.PlayerTeam );
    else
        GXboxLog.Write( "XPROFILE loaded slot=%d name=%s class=%s team=%d applyOnly=1 elapsedMS=%.1f",
            GXboxActiveProfile + 1,
            TCHAR_TO_ANSI(GXboxProfileName),
            TCHAR_TO_ANSI(*GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue),
            GXboxMenu.PlayerTeam,
            (appSeconds() - ApplyStart) * 1000.0 );
}

static void XboxProfileOpen( UXboxViewport* Viewport, UBOOL bPersistGlobal )
{
    DOUBLE OpenStart = appSeconds();
    XboxProfileLoadDirectory( 1 );
    GXboxProfileSelected = GXboxActiveProfile;
    if( GXboxProfiles[GXboxActiveProfile].Created )
        XboxProfileApplyActive( Viewport, bPersistGlobal );
    else
    {
        GXboxPlayerStateLoaded = 0;
        XboxMenuLoadPlayerState();
    }
    if( !bPersistGlobal )
        GXboxLog.Write( "XPROFILE open complete slot=%d created=%d persist=0 elapsedMS=%.1f",
            GXboxActiveProfile + 1,
            GXboxProfiles[GXboxActiveProfile].Created ? 1 : 0,
            (appSeconds() - OpenStart) * 1000.0 );
}

static INT XboxProfileCreatedCount()
{
    XboxProfileLoadDirectory();
    INT Count = 0;
    for( INT i=0; i<XBOX_PROFILE_COUNT; i++ )
        if( GXboxProfiles[i].Created )
            Count++;
    return Count;
}

static INT XboxProfileFirstEmpty()
{
    XboxProfileLoadDirectory();
    for( INT i=0; i<XBOX_PROFILE_COUNT; i++ )
        if( !GXboxProfiles[i].Created )
            return i;
    return -1;
}

static INT XboxProfileIndexForGateRow( INT Row )
{
    XboxProfileLoadDirectory();
    INT Visible = 0;
    for( INT i=0; i<XBOX_PROFILE_COUNT; i++ )
    {
        if( !GXboxProfiles[i].Created )
            continue;
        if( Visible == Row )
            return i;
        Visible++;
    }
    return -1;
}

static INT XboxProfileGateRowForIndex( INT ProfileIndex )
{
    XboxProfileLoadDirectory();
    INT Row = 0;
    for( INT i=0; i<XBOX_PROFILE_COUNT; i++ )
    {
        if( !GXboxProfiles[i].Created )
            continue;
        if( i == ProfileIndex )
            return Row;
        Row++;
    }
    return 0;
}

static INT XboxProfilePlayerClassIndex( INT ProfileIndex )
{
    XboxMenuLoadPlayerClasses();
    if( ProfileIndex < 0 || ProfileIndex >= XBOX_PROFILE_COUNT || !GXboxProfiles[ProfileIndex].Created )
        return -1;

    TCHAR Section[32];
    XboxProfileSectionName( ProfileIndex, Section, ARRAY_COUNT(Section) );
    FString CharacterValue = XboxMenuUserString( Section, TEXT("Character"), TEXT("") );
    FString ClassValue = XboxMenuUserString( Section, TEXT("Class"), TEXT("Botpack.TMale1") );
    FString SkinValue = XboxMenuUserString( Section, TEXT("Skin"), TEXT("CommandoSkins.cmdo") );
    FString FaceValue = XboxMenuUserString( Section, TEXT("Face"), TEXT("CommandoSkins.Blake") );
    return XboxMenuFindPlayerCharacter( ClassValue, SkinValue, FaceValue, CharacterValue );
}

static INT XboxProfileGateRowCount()
{
    INT Count = XboxProfileCreatedCount();
    return Count + (XboxProfileFirstEmpty() >= 0 ? 1 : 0);
}

static void XboxProfileBeginCreate( EXboxProfileNameMode Mode, INT Port, EXboxMenuScreen ReturnScreen )
{
    INT EmptyProfile = XboxProfileFirstEmpty();
    if( EmptyProfile < 0 )
    {
        GXboxLog.Write( "XPROFILE create blocked: profile directory full count=%d", XBOX_PROFILE_COUNT );
        return;
    }

    GXboxProfileSelected = EmptyProfile;
    GXboxProfileEditName[0] = 0;
    GXboxProfileKeyboardFocus = 0;
    GXboxProfileNameMode = Mode;
    GXboxProfileNamePort = Port;
    GXboxProfileReturnScreen = ReturnScreen;
    XboxMenuReleaseProfilePreviewPortrait();
    GXboxMenu.Screen = XMS_ProfileName;
    GXboxLog.Write( "XPROFILE create keyboard opened slot=%d mode=%d port=%d", EmptyProfile + 1, (INT)Mode, Port + 1 );
}

static void XboxProfileBeginFrontendGate( UXboxViewport* Viewport )
{
    XboxProfileLoadDirectory( 1 );
    GXboxSessionProfileLoaded = 0;
    GXboxProfileGateFocus = 0;
    if( XboxProfileCreatedCount() == 0 )
        XboxProfileBeginCreate( XPNM_StartupCreate, -1, XMS_ProfileSelect );
    else
    {
        GXboxMenu.Screen = XMS_ProfileSelect;
        GXboxLog.Write( "XPROFILE frontend gate opened profiles=%d", XboxProfileCreatedCount() );
    }
}

static void XboxProfileLoadFrontend( UXboxViewport* Viewport, INT ProfileIndex )
{
    XboxProfileLoadDirectory();
    if( ProfileIndex < 0 || ProfileIndex >= XBOX_PROFILE_COUNT || !GXboxProfiles[ProfileIndex].Created )
        return;

    GXboxActiveProfile = ProfileIndex;
    GXboxProfileSelected = ProfileIndex;
    XboxProfileSetInt( TEXT("XboxProfiles"), TEXT("Active"), GXboxActiveProfile );
    if( GConfig )
        GConfig->Flush( 0, TEXT("User.ini") );
    XboxProfileApplyActive( Viewport );
    GXboxSessionProfileLoaded = 1;
    XboxMenuReleaseProfilePreviewPortrait();
    GXboxMenu.Screen = XMS_Main;
    GXboxMenu.MainFocus = 0;
    GXboxLog.Write( "XPROFILE frontend loaded slot=%d name=%s -> main menu",
        ProfileIndex + 1, TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name) );
}

static void XboxProfileCommitName( UXboxViewport* Viewport )
{
    INT NameLength = appStrlen( GXboxProfileEditName );
    while( NameLength > 0 && GXboxProfileEditName[NameLength-1] == ' ' )
        GXboxProfileEditName[--NameLength] = 0;
    if( !GXboxProfileEditName[0] )
        appSprintf( GXboxProfileEditName, TEXT("PLAYER %i"), GXboxProfileSelected + 1 );

    EXboxProfileNameMode CompletedMode = GXboxProfileNameMode;
    INT CompletedPort = GXboxProfileNamePort;
    EXboxMenuScreen ReturnScreen = GXboxProfileReturnScreen;
    INT CreatedProfile = GXboxProfileSelected;

    if( CompletedMode == XPNM_MultiplayerCreate )
    {
        XboxProfileCreateMultiplayerSlot( Viewport, CreatedProfile );
        TCHAR Section[32];
        XboxProfileSectionName( CreatedProfile, Section, ARRAY_COUNT(Section) );
        if( GConfig )
        {
            GConfig->SetString( Section, TEXT("Name"), GXboxProfileEditName, TEXT("User.ini") );
            GConfig->Flush( 0, TEXT("User.ini") );
        }
        appStrncpy( GXboxProfiles[CreatedProfile].Name, GXboxProfileEditName, ARRAY_COUNT(GXboxProfiles[CreatedProfile].Name) );
        GXboxProfiles[CreatedProfile].Name[ARRAY_COUNT(GXboxProfiles[CreatedProfile].Name)-1] = 0;
        XboxSplitAssignProfile( CompletedPort, CreatedProfile, XboxMenuGetClient(Viewport) );
        GXboxSplitReadySlots[CompletedPort].Joined = 1;
        GXboxSplitReadySlots[CompletedPort].Locked = 0;
        GXboxSplitReadySlots[CompletedPort].Focus = 0;
        GXboxMenu.Screen = ReturnScreen;
        XboxSystemLinkMarkLocalReadyChanged();
    }
    else
    {
        GXboxActiveProfile = CreatedProfile;
        GXboxPlayerStateLoaded = 0;
        XboxMenuLoadPlayerState();
        GXboxProfiles[CreatedProfile].Created = 1;
        appStrncpy( GXboxProfileName, GXboxProfileEditName, ARRAY_COUNT(GXboxProfileName) );
        GXboxProfileName[ARRAY_COUNT(GXboxProfileName)-1] = 0;
        appStrncpy( GXboxProfiles[CreatedProfile].Name, GXboxProfileName, ARRAY_COUNT(GXboxProfiles[CreatedProfile].Name) );
        GXboxProfiles[CreatedProfile].Name[ARRAY_COUNT(GXboxProfiles[CreatedProfile].Name)-1] = 0;
        XboxProfileSaveActive( Viewport );
        GXboxSessionProfileLoaded = 1;
        GXboxMenu.Screen = XMS_Main;
        GXboxMenu.MainFocus = 0;
    }

    GXboxProfileNameMode = XPNM_None;
    GXboxProfileNamePort = -1;
    GXboxLog.Write( "XPROFILE created slot=%d name=%s mode=%d port=%d",
        CreatedProfile + 1, TCHAR_TO_ANSI(GXboxProfileEditName), (INT)CompletedMode, CompletedPort + 1 );
}

static void XboxProfileCancelName()
{
    if( GXboxProfileNameMode == XPNM_StartupCreate && XboxProfileCreatedCount() == 0 )
    {
        GXboxLog.Write( "XPROFILE startup create cannot cancel: no saved profiles" );
        return;
    }

    EXboxMenuScreen ReturnScreen = GXboxProfileReturnScreen;
    INT CancelledPort = GXboxProfileNamePort;
    GXboxProfileEditName[0] = 0;
    GXboxProfileNameMode = XPNM_None;
    GXboxProfileNamePort = -1;
    GXboxMenu.Screen = ReturnScreen;
    GXboxLog.Write( "XPROFILE create cancelled port=%d return=%d", CancelledPort + 1, (INT)ReturnScreen );
}

static void XboxMenuApplyHudColor( UXboxViewport* Viewport, INT Index )
{
    GXboxSettingsHudColor = XboxMenuWrapInt( Index, 0, ARRAY_COUNT(GXboxColorNames) );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    if( Player && Player->myHUD )
    {
        TCHAR Value[64];
        XboxMenuFormatTripletColor( GXboxSettingsHudColor, Value, ARRAY_COUNT(Value) );
        XboxSetObjectPropertyText( Player->myHUD, TEXT("FavoriteHUDColor"), Value );
        XboxSetClassDefaultPropertyText( Player->myHUD->GetClass(), TEXT("FavoriteHUDColor"), Value );
        Player->myHUD->SaveConfig();
    }
    XboxMenuSaveTripletColor( TEXT("FavoriteHUDColor"), GXboxSettingsHudColor );
}

static void XboxMenuApplyCrosshairColor( UXboxViewport* Viewport, INT Index )
{
    GXboxSettingsCrosshairColor = XboxMenuWrapInt( Index, 0, ARRAY_COUNT(GXboxColorNames) );
    XboxMenuSaveTripletColor( TEXT("CrosshairColor"), GXboxSettingsCrosshairColor );
    if( Viewport && Viewport->Actor && Viewport->Actor->myHUD )
    {
        TCHAR Value[64];
        XboxMenuFormatTripletColor( GXboxSettingsCrosshairColor, Value, ARRAY_COUNT(Value) );
        XboxSetObjectPropertyText( Viewport->Actor->myHUD, TEXT("CrosshairColor"), Value );
        XboxSetClassDefaultPropertyText( Viewport->Actor->myHUD->GetClass(), TEXT("CrosshairColor"), Value );
        Viewport->Actor->myHUD->SaveConfig();
    }
}

static UBOOL XboxMenuIsGameplayMatch( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor || !Viewport->Actor->Level )
        return 0;

    ALevelInfo* Info = Viewport->Actor->Level;
    if( !Info->Game )
        return 0;

    if( Info->XLevel && Info->XLevel->URL.Map.Len()
    && (appStricmp( *Info->XLevel->URL.Map, TEXT("CityIntro") ) == 0
    ||  appStricmp( *Info->XLevel->URL.Map, TEXT("CityIntro.unr") ) == 0) )
        return 0;

    UClass* GameClass = Info->Game->GetClass();
    const TCHAR* GameName = GameClass ? GameClass->GetName() : TEXT("");
    if( appStricmp( GameName, TEXT("UTIntro") ) == 0 )
        return 0;

    return 1;
}

static UBOOL XboxMenuShouldPauseMatch( UXboxViewport* Viewport )
{
    return XboxMenuIsGameplayMatch(Viewport)
        && Viewport->Actor->Level->NetMode == NM_Standalone;
}

static void XboxMenuSetMusicPaused( UXboxViewport* Viewport, UBOOL bPaused )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client && Client->Engine && Client->Engine->Audio )
        Client->Engine->Audio->Exec( bPaused ? TEXT("XAUDIOPAUSEMUSIC 1") : TEXT("XAUDIOPAUSEMUSIC 0") );
}

static UBOOL XboxMenuEnsureConsoleClass( UXboxViewport* Viewport, const TCHAR* ConsoleClassName, const char* Reason )
{
    return XboxEnsureConsoleClass( Viewport, ConsoleClassName, Reason );
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
    {
        GXboxLog.Write( "XMENU opened" );
        GXboxMenuOwnerViewport = Clamp<INT>( XboxViewportIndex(Viewport), 0, 3 );
    }
    UBOOL bGameplayMatch = XboxMenuIsGameplayMatch( Viewport );
    UBOOL bPauseMatch = XboxMenuShouldPauseMatch( Viewport );
    GXboxMenu.Active = 1;
    GXboxMenuGameplayContinues = bGameplayMatch && !bPauseMatch;
    GXboxMenu.Screen = bGameplayMatch ? XMS_Pause : XMS_Main;
    GXboxMenu.PauseFocus = 0;
    GXboxPauseReturnConfirm = 0;
    GXboxPauseReturnConfirmFocus = 1;
    GXboxMenu.MainFocus = 0;
    if( !bGameplayMatch && !GXboxSessionProfileLoaded )
        XboxProfileBeginFrontendGate( Viewport );
    GXboxLog.Write( "XMENU open screen=%s owner=%d gameplay=%d continues=%d net=%d",
        GXboxMenu.Screen == XMS_Pause ? "PAUSE" :
        GXboxMenu.Screen == XMS_Main  ? "MAIN"  : "OTHER",
        GXboxMenuOwnerViewport,
        bGameplayMatch ? 1 : 0,
        GXboxMenuGameplayContinues ? 1 : 0,
        (Viewport && Viewport->Actor && Viewport->Actor->Level) ? (INT)Viewport->Actor->Level->NetMode : -1 );

    XboxMenuApplyMatchPause( Viewport );

    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client && Client->Engine && Client->Engine->Audio && !GXboxMenuGameplayContinues )
    {
        Client->Engine->Audio->Exec( TEXT("XAUDIOSETMENUMODE 1") );
        Client->Engine->Audio->Exec( TEXT("XAUDIOSTOPFX") );
        GXboxMenuAudioModeActive = 1;
    }
}

static void XboxMenuTickPendingFrontendOpen( UXboxViewport* Viewport )
{
    if( !GXboxFrontendMenuOpenPending || !Viewport || !Viewport->Actor )
        return;

    ULevel* Level = Viewport->Actor->GetLevel();
    if( !XboxIsFrontendLevel(Level) )
        return;

    GXboxFrontendMenuOpenPending = 0;
    XboxMenuOpen( Viewport );
    if( GXboxSessionProfileLoaded )
        GXboxMenu.Screen = XMS_Main;
    GXboxMenu.MainFocus = 0;
    GXboxMenu.PauseFocus = 0;
    if( GXboxFrontendTournamentOpenPending )
    {
        GXboxFrontendTournamentOpenPending = 0;
        XboxTournamentClampSelection( Viewport );
        if( GXboxFrontendTournamentPostMatchPending && GXboxTournamentPostMatch.Valid )
        {
            GXboxFrontendTournamentPostMatchPending = 0;
            GXboxMenu.Screen = XMS_TournamentPostMatch;
            GXboxLog.Write( "XMENU Tournament post-match opened after travel" );
        }
        else
        {
            GXboxFrontendTournamentPostMatchPending = 0;
            GXboxMenu.Screen = XMS_Tournament;
            GXboxMenu.TournamentFocus = 0;
        }
        XboxMenuReleaseMapPreviewTexture();
    }
    GXboxMenu.PausedMatch = 0;
    GXboxLog.Write( "XMENU frontend reopened after travel map=%s availKB=%u",
        Level && Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
        (unsigned)XboxMenuAvailPhysKB() );
}

static void XboxHaloPortraitProofTick( UXboxViewport* Viewport )
{
    static INT HaloPortraitProof = -1;
    static UBOOL HaloPortraitShown = 0;
    // Early UpdateInput calls can precede the first loaded player/level.
    // Do not cache a missing disc marker before the runtime is ready.
    if( HaloPortraitProof < 0 && Viewport && Viewport->Actor && Viewport->Actor->Level )
        HaloPortraitProof = GetFileAttributesA( "D:\\XboxHaloPortraitProof.ini" ) != 0xFFFFFFFF;
    if( HaloPortraitProof > 0 && Viewport && Viewport->Actor )
    {
        if( !HaloPortraitShown )
        {
            XboxMenuOpen( Viewport );
            // Load the saved profile before overriding the proof selection;
            // DrawPlayerSetup otherwise restores Othello over this choice.
            XboxMenuLoadPlayerState();
            for( INT i=0; i<GXboxPlayerClasses.Num(); i++ )
                if( appStricmp( *GXboxPlayerClasses(i).URLValue, TEXT("HaloUTXbox.Elite") ) == 0 )
                {
                    GXboxMenu.PlayerClass = i;
                    GXboxMenu.PlayerTeam = GXboxPlayerClasses(i).DefaultTeam;
                }
            GXboxMenu.Screen = XMS_PlayerSetup;
            GXboxMenu.PlayerFocus = 0;
            GXboxLog.Write( "HALOPORTRAITPROOF selected Elite default portrait" );
            HaloPortraitShown = 1;
        }
        return;
    }
}

static void XboxMenuSmokeTick( UXboxViewport* Viewport )
{
    static INT SmokeStage = 0;
    static DOUBLE SmokeStartTime = 0.0;

    if( XboxSoakSmokeEnabled() || !XboxMenuSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    if( SmokeStage == 0 )
    {
        XboxMenuOpen( Viewport );
        GXboxMenu.Screen = XMS_Main;
        SmokeStartTime = appSeconds();
        SmokeStage = 1;
        GXboxLog.Write( "XMENU SMOKE opened main menu" );
    }
    else if( SmokeStage == 1 && (appSeconds() - SmokeStartTime) > 2.0 )
    {
        GXboxMenu.Screen = XMS_InstantAction;
        GXboxMenu.InstantGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1 );
        if( XboxMenuSmokeJailbreakEnabled() )
        {
            for( INT i=0; i<XboxMenuGameTypeCount(); i++ )
            {
                const FXboxDiscoveredOption& Game = XboxMenuGameType(i);
                if( appStricmp( *Game.URLValue, TEXT("JailBreak.JailBreak") ) == 0
                ||  appStricmp( *Game.Label, TEXT("JAILBREAK") ) == 0 )
                {
                    GXboxMenu.InstantGameType = i;
                    break;
                }
            }
        }
        XboxMenuLoadMapsForGameType( GXboxMenu.InstantGameType );
        if( XboxMenuSmokeJailbreakEnabled() )
        {
            INT TalaeronIndex = XboxMenuFindMapIndexByFile( GXboxMenu.InstantGameType, TEXT("JB-Talaeron-Gold.unr") );
            if( TalaeronIndex >= 0 )
                GXboxMenu.InstantMap[GXboxMenu.InstantGameType] = TalaeronIndex;
        }
        SmokeStage = 2;
        SmokeStartTime = appSeconds();
        const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
        const INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
        const FXboxDiscoveredOption* Map = MapCount > 0 ? &XboxMenuMap( GXboxMenu.InstantGameType, GXboxMenu.InstantMap[GXboxMenu.InstantGameType] ) : NULL;
        GXboxLog.Write( "XMENU SMOKE opened Instant Action gameTypes=%d selected=%d label=%s class=%s prefix=%s maps=%d map=%s mutators=%d",
            XboxMenuGameTypeCount(),
            GXboxMenu.InstantGameType,
            TCHAR_TO_ANSI(*Game.Label),
            TCHAR_TO_ANSI(*Game.URLValue),
            TCHAR_TO_ANSI(*Game.MapPrefix),
            MapCount,
            Map ? TCHAR_TO_ANSI(*Map->URLValue) : "",
            XboxMenuMutatorCount() );
    }
    else if( SmokeStage == 2 && !XboxMenuSmokeJailbreakEnabled() && (appSeconds() - SmokeStartTime) > 2.0 )
    {
        GXboxMenu.Screen = XMS_PlayerSetup;
        GXboxMenu.PlayerFocus = 0;
        XboxProfileOpen( Viewport, 0 );
        SmokeStage = 3;
        SmokeStartTime = appSeconds();
        const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
        GXboxLog.Write( "XMENU SMOKE opened Player Setup character=%d %s portrait=%s",
            GXboxMenu.PlayerClass, TCHAR_TO_ANSI(*Player.Label), Player.PortraitName );
    }
    else if( SmokeStage >= 3 && SmokeStage < 11 && (appSeconds() - SmokeStartTime) > 1.0 )
    {
        char OldPortraitName[64];
        appMemzero( OldPortraitName, sizeof(OldPortraitName) );
        const char* CurrentPortraitName = XboxMenuCurrentPlayerPortraitName();
        if( CurrentPortraitName && CurrentPortraitName[0] )
        {
            appStrncpy( OldPortraitName, CurrentPortraitName, ARRAY_COUNT(OldPortraitName) );
            OldPortraitName[ARRAY_COUNT(OldPortraitName)-1] = 0;
        }
        XboxMenuLoadPlayerClasses();
        GXboxMenu.PlayerClass = XboxMenuWrapInt( GXboxMenu.PlayerClass, 1, GXboxPlayerClasses.Num() );
        GXboxPlayerSkinsClass = -1;
        GXboxPlayerVoicesClass = -1;
        GXboxMenu.PlayerSkin = 0;
        GXboxMenu.PlayerFace = 0;
        GXboxMenu.PlayerVoice = 0;
        GXboxMenu.PlayerTeam = GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultTeam;
        XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
        XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
        XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
        if( OldPortraitName[0] )
            XboxRenderReleaseMenuTexture( OldPortraitName );
        const FXboxPlayerClassOption& Player = XboxMenuPlayerClass( GXboxMenu.PlayerClass );
        GXboxLog.Write( "XMENU SMOKE cycled Player Setup stage=%d character=%d %s url=%s portrait=%s",
            SmokeStage,
            GXboxMenu.PlayerClass,
            TCHAR_TO_ANSI(*Player.Label),
            TCHAR_TO_ANSI(*Player.URLValue),
            Player.PortraitName );
        GXboxLog.Flush();
        SmokeStage++;
        SmokeStartTime = appSeconds();
    }
}

static void XboxMenuClose( UXboxViewport* Viewport )
{
    if( GXboxMenu.Active )
        GXboxLog.Write( "XMENU closed" );
    UBOOL bWasPauseMenu = GXboxMenu.PausedMatch;
    GXboxMenu.Active = 0;
    GXboxPauseReturnConfirm = 0;
    GXboxPauseReturnConfirmFocus = 1;
    XboxSplitReadyReleaseControllers();
    XboxMenuReleaseFrontendTransientAssets( "menu close", bWasPauseMenu ? 0 : 1 );

    XboxMenuReleaseMatchPause( Viewport );

    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client && Client->Engine && Client->Engine->Audio && GXboxMenuAudioModeActive )
        Client->Engine->Audio->Exec( TEXT("XAUDIOSETMENUMODE 0") );
    GXboxMenuAudioModeActive = 0;
    GXboxMenuGameplayContinues = 0;
    GXboxMenuOwnerViewport = 0;
}

static UBOOL XboxMenuIsActive()
{
    return GXboxMenu.Active;
}

extern "C" UBOOL XboxMenuWantsEffectSuppression()
{
    return GXboxMenu.Active && !GXboxMenuGameplayContinues;
}

extern "C" UBOOL XboxMenuAllowsEffectSound( INT Id )
{
    return GXboxMenuVoiceSampleBypass > 0 && Id == SLOT_Interface;
}

static void XboxMenuBack( UXboxViewport* Viewport )
{
    if( GXboxMenu.Screen == XMS_Pause )
    {
        XboxMenuClose( Viewport );
    }
    else if( GXboxMenu.Screen == XMS_Main )
    {
        ULevel* Level = Viewport && Viewport->Actor ? Viewport->Actor->GetLevel() : NULL;
        if( !XboxIsFrontendLevel(Level) )
            XboxMenuClose( Viewport );
    }
    else if( GXboxMenu.Screen == XMS_Mutators )
    {
        GXboxMenu.Screen = XMS_InstantAction;
        GXboxLog.Write( "XMENU close mutator overlay" );
    }
    else if( GXboxMenu.Screen == XMS_SystemLink )
    {
        XboxSplitReadyReleaseControllers();
        XboxSystemLinkStop();
        GXboxMenu.Screen = XMS_Main;
        GXboxLog.Write( "XMENU back from System Link alpha" );
    }
    else if( GXboxMenu.Screen == XMS_SystemLinkMapSelect )
    {
        GXboxMenu.Screen = XMS_SystemLink;
        GXboxSystemLink.ReadyConfirmed = 0;
        GXboxSystemLink.Phase = XSLP_Ready;
        GXboxLog.Write( "XMENU back from System Link map select" );
    }
    else if( GXboxMenu.Screen == XMS_Tournament )
    {
        XboxMenuReleaseMapPreviewTexture();
        GXboxMenu.Screen = XMS_Main;
        GXboxLog.Write( "XMENU back from Tournament" );
    }
    else if( GXboxMenu.Screen == XMS_TournamentPostMatch )
    {
        XboxMenuReleaseMapPreviewTexture();
        GXboxMenu.Screen = XMS_Tournament;
        GXboxMenu.TournamentFocus = (GXboxTournamentPostMatch.Valid && !GXboxTournamentPostMatch.Advanced) ? 2 : 0;
        GXboxLog.Write( "XMENU back from Tournament post-match" );
    }
    else if( GXboxMenu.Screen == XMS_Controls )
    {
        GXboxMenu.Screen = XMS_Settings;
        GXboxMenu.SettingsFocus = XSH_Controls;
        GXboxLog.Write( "XMENU back from Controls" );
    }
    else if( GXboxMenu.Screen == XMS_ProfileName )
    {
        XboxProfileCancelName();
    }
    else if( GXboxMenu.Screen == XMS_ProfileSelect )
    {
        if( GXboxSessionProfileLoaded )
        {
            XboxMenuReleaseProfilePreviewPortrait();
            GXboxMenu.Screen = XMS_Main;
            GXboxMenu.MainFocus = 0;
            GXboxLog.Write( "XPROFILE optional switch cancelled; returning to main" );
        }
        else
        {
            GXboxLog.Write( "XPROFILE frontend gate back blocked until profile load" );
        }
    }
    else if( GXboxMenu.Screen == XMS_SettingsAudio )
    {
        GXboxMenu.Screen = XMS_Settings;
        GXboxMenu.SettingsFocus = XSH_Audio;
        GXboxLog.Write( "XMENU back from Audio settings" );
    }
    else if( GXboxMenu.Screen == XMS_SettingsVideo )
    {
        GXboxMenu.Screen = XMS_Settings;
        GXboxMenu.SettingsFocus = XSH_Video;
        GXboxLog.Write( "XMENU back from Video settings" );
    }
    else if( GXboxMenu.Screen == XMS_Settings )
    {
        if( GXboxMenu.PausedMatch )
        {
            GXboxMenu.Screen = XMS_Pause;
            GXboxMenu.PauseFocus = 2;
        }
        else
        {
            GXboxMenu.Screen = XMS_Main;
            GXboxMenu.MainFocus = 5;
        }
        GXboxLog.Write( "XMENU back from Settings" );
    }
    else if( GXboxMenu.Screen == XMS_SplitMapSelect )
    {
        GXboxMenu.Screen = XMS_SplitReady;
        GXboxMenu.SplitFocus = 0;
        XboxMenuReleaseMapPreviewTexture();
        GXboxLog.Write( "XMENU back to Splitscreen Ready" );
    }
    else if( GXboxMenu.Screen == XMS_SplitReady )
    {
        XboxSplitReadyReleaseControllers();
        XboxMenuReleaseMapPreviewTexture();
        GXboxMenu.Screen = XMS_Main;
        GXboxLog.Write( "XMENU back from Splitscreen Ready" );
    }
    else
    {
        if( GXboxMenu.Screen == XMS_PlayerSetup )
        {
            XboxProfileSaveActive( Viewport );
            XboxMenuReleaseFrontendTransientAssets( "back from player setup", 0 );
        }
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

static UClass* XboxTournamentLadderClass( INT Index )
{
    Index = Clamp<INT>( Index, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    return UObject::StaticLoadClass( UObject::StaticClass(), NULL, GXboxTournamentLadders[Index].LadderClass, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
}

static INT XboxTournamentMatchCount( INT LadderIndex )
{
    UClass* LadderClass = XboxTournamentLadderClass( LadderIndex );
    return XboxMenuClassDefaultInt( LadderClass, TEXT("Matches"), 0 );
}

static AInventory* XboxTournamentFindLadderInventory( APlayerPawn* Player, UClass* InventoryClass );

static const TCHAR* XboxTournamentPositionProperty( INT LadderIndex )
{
    switch( Clamp<INT>( LadderIndex, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 ) )
    {
        case 0: return TEXT("DMPosition");
        case 1: return TEXT("DOMPosition");
        case 2: return TEXT("CTFPosition");
        case 3: return TEXT("ASPosition");
        case 4: return TEXT("ChalPosition");
    }
    return TEXT("DMPosition");
}

static const TCHAR* XboxTournamentProfilePositionProperty( INT LadderIndex )
{
    switch( Clamp<INT>( LadderIndex, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 ) )
    {
        case 0: return TEXT("TournamentDMPosition");
        case 1: return TEXT("TournamentDOMPosition");
        case 2: return TEXT("TournamentCTFPosition");
        case 3: return TEXT("TournamentASPosition");
        case 4: return TEXT("TournamentChalPosition");
    }
    return TEXT("TournamentDMPosition");
}

static UBOOL XboxTournamentActiveProfileSection( TCHAR* Section, INT SectionCount )
{
    if( !Section || SectionCount <= 0 )
        return 0;

    Section[0] = 0;
    XboxProfileLoadDirectory();
    if( GXboxActiveProfile < 0
    ||  GXboxActiveProfile >= XBOX_PROFILE_COUNT
    ||  !GXboxProfiles[GXboxActiveProfile].Created )
        return 0;

    XboxProfileSectionName( GXboxActiveProfile, Section, SectionCount );
    return 1;
}

static void XboxTournamentMigrateLegacyProgress()
{
    if( !GConfig )
        return;

    TCHAR ProfileSection[32];
    if( !XboxTournamentActiveProfileSection(ProfileSection, ARRAY_COUNT(ProfileSection)) )
        return;

    INT MigrationOwner = -1;
    if( GConfig->GetInt(TEXT("Xbox.Tournament"), TEXT("ProfileMigrationOwner"), MigrationOwner, TEXT("User.ini")) )
        return;

    INT MigratedCount = 0;
    for( INT LadderIndex=0; LadderIndex<ARRAY_COUNT(GXboxTournamentLadders); LadderIndex++ )
    {
        INT ExistingPosition = 0;
        if( GConfig->GetInt(ProfileSection, XboxTournamentProfilePositionProperty(LadderIndex), ExistingPosition, TEXT("User.ini")) )
            continue;

        INT LegacyPosition = GXboxTournamentLadders[LadderIndex].FirstRatedMatch;
        if( GConfig->GetInt(TEXT("Xbox.Tournament"), XboxTournamentPositionProperty(LadderIndex), LegacyPosition, TEXT("User.ini")) )
        {
            GConfig->SetInt( ProfileSection, XboxTournamentProfilePositionProperty(LadderIndex), LegacyPosition, TEXT("User.ini") );
            MigratedCount++;
        }
    }

    GConfig->SetInt( TEXT("Xbox.Tournament"), TEXT("ProfileMigrationOwner"), GXboxActiveProfile, TEXT("User.ini") );
    GConfig->Flush( 0, TEXT("User.ini") );
    GXboxLog.Write( "XTOUR progress migration profile=%d section=%s entries=%d",
        GXboxActiveProfile + 1, TCHAR_TO_ANSI(ProfileSection), MigratedCount );
}

static INT XboxTournamentLadderIndexFromChange( INT Change )
{
    for( INT i=0; i<ARRAY_COUNT(GXboxTournamentLadders); i++ )
        if( GXboxTournamentLadders[i].LastMatchType == Change )
            return i;
    return 0;
}

static INT XboxTournamentSavedPosition( INT LadderIndex, INT DefaultPosition )
{
    INT Saved = DefaultPosition;
    if( !GConfig )
        return Saved;

    XboxTournamentMigrateLegacyProgress();
    TCHAR ProfileSection[32];
    if( XboxTournamentActiveProfileSection(ProfileSection, ARRAY_COUNT(ProfileSection)) )
        GConfig->GetInt( ProfileSection, XboxTournamentProfilePositionProperty(LadderIndex), Saved, TEXT("User.ini") );
    else
        GConfig->GetInt( TEXT("Xbox.Tournament"), XboxTournamentPositionProperty(LadderIndex), Saved, TEXT("User.ini") );
    return Saved;
}

static void XboxTournamentSavePosition( INT LadderIndex, INT Position )
{
    LadderIndex = Clamp<INT>( LadderIndex, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    INT FirstMatch = GXboxTournamentLadders[LadderIndex].FirstRatedMatch;
    INT MatchCount = XboxTournamentMatchCount( LadderIndex );
    if( MatchCount <= 0 )
        return;

    Position = Clamp<INT>( Position, FirstMatch, MatchCount - 1 );
    if( GConfig )
    {
        TCHAR ProfileSection[32];
        if( XboxTournamentActiveProfileSection(ProfileSection, ARRAY_COUNT(ProfileSection)) )
            GConfig->SetInt( ProfileSection, XboxTournamentProfilePositionProperty(LadderIndex), Position, TEXT("User.ini") );
        else
            GConfig->SetInt( TEXT("Xbox.Tournament"), XboxTournamentPositionProperty(LadderIndex), Position, TEXT("User.ini") );
        GConfig->Flush( 0, TEXT("User.ini") );
    }
    GXboxLog.Write( "XTOUR progress save profile=%d ladder=%d property=%s position=%d",
        XboxProfileActiveCreated() ? GXboxActiveProfile + 1 : 0,
        LadderIndex,
        TCHAR_TO_ANSI(XboxTournamentProfilePositionProperty(LadderIndex)),
        Position );
}

static AInventory* XboxTournamentFindInventory( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor )
        return NULL;

    UClass* InventoryClass = UObject::StaticLoadClass( AInventory::StaticClass(), NULL, TEXT("Botpack.LadderInventory"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    return XboxTournamentFindLadderInventory( Viewport->Actor, InventoryClass );
}

static INT XboxTournamentAvailableMatch( UXboxViewport* Viewport, INT LadderIndex )
{
    LadderIndex = Clamp<INT>( LadderIndex, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    INT FirstMatch = GXboxTournamentLadders[LadderIndex].FirstRatedMatch;
    INT MatchCount = XboxTournamentMatchCount( LadderIndex );
    if( MatchCount <= 0 )
        return 0;

    AInventory* LadderInv = XboxTournamentFindInventory( Viewport );
    INT Position = LadderInv ? XboxGetObjectPropertyInt( LadderInv, XboxTournamentPositionProperty( LadderIndex ), FirstMatch ) : FirstMatch;
    Position = Max<INT>( Position, XboxTournamentSavedPosition( LadderIndex, FirstMatch ) );
    if( Position < FirstMatch )
        Position = FirstMatch;
    return Clamp<INT>( Position, FirstMatch, MatchCount - 1 );
}

static void XboxTournamentClampSelection( UXboxViewport* Viewport )
{
    GXboxMenu.TournamentLadder = Clamp<INT>( GXboxMenu.TournamentLadder, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    GXboxMenu.TournamentMatch = XboxTournamentAvailableMatch( Viewport, GXboxMenu.TournamentLadder );
    GXboxMenu.TournamentSkill = Clamp<INT>( GXboxMenu.TournamentSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );
}

static void XboxTournamentClampSelection()
{
    GXboxMenu.TournamentLadder = Clamp<INT>( GXboxMenu.TournamentLadder, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    INT FirstMatch = GXboxTournamentLadders[GXboxMenu.TournamentLadder].FirstRatedMatch;
    INT MatchCount = XboxTournamentMatchCount( GXboxMenu.TournamentLadder );
    if( MatchCount <= FirstMatch )
        GXboxMenu.TournamentMatch = 0;
    else
        GXboxMenu.TournamentMatch = Clamp<INT>( GXboxMenu.TournamentMatch, FirstMatch, MatchCount - 1 );
    GXboxMenu.TournamentSkill = Clamp<INT>( GXboxMenu.TournamentSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );
}

static UBOOL XboxTournamentStringAt( INT LadderIndex, const TCHAR* PropertyName, INT MatchIndex, FString& OutValue )
{
    UClass* LadderClass = XboxTournamentLadderClass( LadderIndex );
    return XboxMenuClassDefaultStringAt( LadderClass, PropertyName, MatchIndex, OutValue );
}

static FString XboxTournamentFullMap( INT LadderIndex, INT MatchIndex );

static void XboxTournamentStorePostMatch( INT LadderIndex, INT CompletedMatch, INT NextMatch, INT PendingRank, INT PreviousPosition, INT NewPosition, UBOOL Advanced )
{
    appMemzero( &GXboxTournamentPostMatch, sizeof(GXboxTournamentPostMatch) );

    LadderIndex = Clamp<INT>( LadderIndex, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    INT FirstMatch = GXboxTournamentLadders[LadderIndex].FirstRatedMatch;
    INT MatchCount = XboxTournamentMatchCount( LadderIndex );
    if( MatchCount <= 0 )
        return;

    CompletedMatch = Clamp<INT>( CompletedMatch, FirstMatch, MatchCount - 1 );
    NextMatch = Clamp<INT>( NextMatch, FirstMatch, MatchCount - 1 );

    FString Title;
    FString Map;
    FString RankTitle;
    XboxTournamentStringAt( LadderIndex, TEXT("MapTitle"), CompletedMatch, Title );
    Map = XboxTournamentFullMap( LadderIndex, CompletedMatch );
    if( PendingRank >= 0 )
        XboxTournamentStringAt( LadderIndex, TEXT("Titles"), PendingRank, RankTitle );

    if( Title.Len() <= 0 )
        Title = Map.Len() ? Map : FString(TEXT("TOURNAMENT MATCH"));
    if( Map.Len() <= 0 )
        Map = Title;
    if( RankTitle.Len() <= 0 )
        RankTitle = Advanced ? FString(TEXT("ADVANCED")) : FString(TEXT("UNCHANGED"));

    GXboxTournamentPostMatch.Valid = 1;
    GXboxTournamentPostMatch.Advanced = Advanced;
    GXboxTournamentPostMatch.LadderIndex = LadderIndex;
    GXboxTournamentPostMatch.CompletedMatch = CompletedMatch;
    GXboxTournamentPostMatch.NextMatch = NextMatch;
    GXboxTournamentPostMatch.PendingRank = PendingRank;
    GXboxTournamentPostMatch.PreviousPosition = PreviousPosition;
    GXboxTournamentPostMatch.NewPosition = NewPosition;
    appStrncpy( GXboxTournamentPostMatch.MatchTitle, *Title, ARRAY_COUNT(GXboxTournamentPostMatch.MatchTitle) );
    appStrncpy( GXboxTournamentPostMatch.MapName, *Map, ARRAY_COUNT(GXboxTournamentPostMatch.MapName) );
    appStrncpy( GXboxTournamentPostMatch.RankTitle, *RankTitle, ARRAY_COUNT(GXboxTournamentPostMatch.RankTitle) );
    GXboxTournamentPostMatch.MatchTitle[ARRAY_COUNT(GXboxTournamentPostMatch.MatchTitle)-1] = 0;
    GXboxTournamentPostMatch.MapName[ARRAY_COUNT(GXboxTournamentPostMatch.MapName)-1] = 0;
    GXboxTournamentPostMatch.RankTitle[ARRAY_COUNT(GXboxTournamentPostMatch.RankTitle)-1] = 0;

    GXboxLog.Write( "XMENU Tournament post-match stored ladder=%d completed=%d next=%d rank=%d advanced=%d old=%d new=%d title=%s map=%s",
        LadderIndex,
        CompletedMatch,
        NextMatch,
        PendingRank,
        Advanced ? 1 : 0,
        PreviousPosition,
        NewPosition,
        TCHAR_TO_ANSI(GXboxTournamentPostMatch.MatchTitle),
        TCHAR_TO_ANSI(GXboxTournamentPostMatch.MapName) );
}

static FString XboxTournamentFullMap( INT LadderIndex, INT MatchIndex )
{
    UClass* LadderClass = XboxTournamentLadderClass( LadderIndex );
    FString Prefix;
    FString Map;
    XboxMenuClassDefaultString( LadderClass, TEXT("MapPrefix"), Prefix );
    XboxMenuClassDefaultStringAt( LadderClass, TEXT("Maps"), MatchIndex, Map );
    return Prefix + Map;
}

static AInventory* XboxTournamentFindLadderInventory( APlayerPawn* Player, UClass* InventoryClass )
{
    if( !Player || !InventoryClass )
        return NULL;

    for( AInventory* Inv=Player->Inventory; Inv; Inv=Inv->Inventory )
        if( Inv->IsA( InventoryClass ) )
            return Inv;
    return NULL;
}

static void XboxTournamentEnsureInventoryLinked( APlayerPawn* Player, AInventory* Inv )
{
    if( !Player || !Inv )
        return;

    for( AInventory* Scan=Player->Inventory; Scan; Scan=Scan->Inventory )
        if( Scan == Inv )
            return;

    Inv->Inventory = Player->Inventory;
    Inv->Owner = Player;
    Player->Inventory = Inv;
}

static UBOOL XboxTournamentEnsureInventory( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor || !Viewport->Actor->XLevel )
        return 0;

    XboxTournamentClampSelection( Viewport );

    UClass* InventoryClass = UObject::StaticLoadClass( AInventory::StaticClass(), NULL, TEXT("Botpack.LadderInventory"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    UClass* LadderClass = XboxTournamentLadderClass( GXboxMenu.TournamentLadder );
    if( !InventoryClass || !LadderClass )
    {
        GXboxLog.Write( "XMENU Tournament blocked: inventory=%s ladder=%s",
            InventoryClass ? "ok" : "missing",
            LadderClass ? "ok" : "missing" );
        return 0;
    }

    APlayerPawn* Player = Viewport->Actor;
    AInventory* LadderInv = XboxTournamentFindLadderInventory( Player, InventoryClass );
    UBOOL bSpawned = 0;
    if( !LadderInv )
    {
        LadderInv = Cast<AInventory>( Player->XLevel->SpawnActor( InventoryClass, NAME_None, Player, NULL, Player->Location, Player->Rotation, NULL, 1 ) );
        bSpawned = LadderInv != NULL;
        if( LadderInv )
        {
            XboxSetObjectPropertyInt( LadderInv, TEXT("DMPosition"), -1 );
            XboxSetObjectPropertyInt( LadderInv, TEXT("DOMPosition"), -1 );
            XboxSetObjectPropertyInt( LadderInv, TEXT("CTFPosition"), -1 );
            XboxSetObjectPropertyInt( LadderInv, TEXT("ASPosition"), -1 );
            XboxSetObjectPropertyInt( LadderInv, TEXT("ChalPosition"), 0 );
        }
    }

    if( !LadderInv )
    {
        GXboxLog.Write( "XMENU Tournament blocked: failed to spawn LadderInventory" );
        return 0;
    }

    for( INT LadderIndex=0; LadderIndex<ARRAY_COUNT(GXboxTournamentLadders); LadderIndex++ )
    {
        INT FirstMatch = GXboxTournamentLadders[LadderIndex].FirstRatedMatch;
        INT CurrentPosition = XboxGetObjectPropertyInt( LadderInv, XboxTournamentPositionProperty(LadderIndex), FirstMatch );
        INT SavedPosition = XboxTournamentSavedPosition( LadderIndex, FirstMatch );
        if( SavedPosition > CurrentPosition )
            XboxSetObjectPropertyInt( LadderInv, XboxTournamentPositionProperty(LadderIndex), SavedPosition );
    }

    TCHAR ClassText[128];
    appSprintf( ClassText, TEXT("class'%s'"), GXboxTournamentLadders[GXboxMenu.TournamentLadder].LadderClass );
    XboxSetObjectPropertyText( LadderInv, TEXT("CurrentLadder"), ClassText );
    XboxSetObjectPropertyInt( LadderInv, TEXT("TournamentDifficulty"), GXboxMenu.TournamentSkill );
    XboxSetObjectPropertyInt( LadderInv, TEXT("PendingChange"), 0 );
    XboxSetObjectPropertyInt( LadderInv, TEXT("PendingPosition"), GXboxMenu.TournamentMatch );
    XboxSetObjectPropertyInt( LadderInv, TEXT("LastMatchType"), GXboxTournamentLadders[GXboxMenu.TournamentLadder].LastMatchType );

    FString TeamText;
    if( XboxMenuClassDefaultStringAt( LadderClass, TEXT("LadderTeams"), 0, TeamText ) )
        XboxSetObjectPropertyText( LadderInv, TEXT("Team"), *TeamText );

    if( bSpawned )
    {
        UFunction* GiveTo = LadderInv->FindFunction( FName(TEXT("GiveTo"), FNAME_Find) );
        if( GiveTo )
        {
            struct FGiveToParms
            {
                APawn* Other;
            } Parms;
            Parms.Other = Player;
            LadderInv->ProcessEvent( GiveTo, &Parms );
        }
    }

    XboxTournamentEnsureInventoryLinked( Player, LadderInv );
    GXboxLog.Write( "XMENU Tournament inventory ready spawned=%d ladder=%s match=%d skill=%d team=%s",
        bSpawned,
        TCHAR_TO_ANSI(GXboxTournamentLadders[GXboxMenu.TournamentLadder].LadderClass),
        GXboxMenu.TournamentMatch,
        GXboxMenu.TournamentSkill,
        TeamText.Len() ? TCHAR_TO_ANSI(*TeamText) : "" );
    return 1;
}

static UBOOL XboxTournamentCapturePendingResult( UXboxViewport* Viewport, ULevel* Level, AInventory* LadderInv, const char* Source )
{
    if( !Viewport || !Level || !LadderInv || GXboxTournamentTransitionHandledLevel == Level )
        return 0;

    INT PendingChange = XboxGetObjectPropertyInt( LadderInv, TEXT("PendingChange"), 0 );
    INT LastMatchType = XboxGetObjectPropertyInt( LadderInv, TEXT("LastMatchType"), PendingChange );
    INT LadderIndex = XboxTournamentLadderIndexFromChange( PendingChange > 0 ? PendingChange : LastMatchType );
    INT FirstMatch = GXboxTournamentLadders[LadderIndex].FirstRatedMatch;
    INT PendingPosition = XboxGetObjectPropertyInt( LadderInv, TEXT("PendingPosition"), FirstMatch );
    INT PendingRank = XboxGetObjectPropertyInt( LadderInv, TEXT("PendingRank"), 0 );
    INT CurrentPosition = XboxGetObjectPropertyInt( LadderInv, XboxTournamentPositionProperty(LadderIndex), FirstMatch );
    INT SavedPosition = XboxTournamentSavedPosition( LadderIndex, FirstMatch );
    INT NewPosition = Max<INT>( Max<INT>( CurrentPosition, SavedPosition ), PendingPosition );
    INT MatchCount = XboxTournamentMatchCount( LadderIndex );
    INT CompletedMatch = PendingPosition > FirstMatch ? PendingPosition - 1 : Max<INT>( CurrentPosition, SavedPosition );
    if( MatchCount > 0 )
        CompletedMatch = Clamp<INT>( CompletedMatch, FirstMatch, MatchCount - 1 );

    XboxTournamentStorePostMatch( LadderIndex, CompletedMatch, NewPosition, PendingRank, Max<INT>( CurrentPosition, SavedPosition ), NewPosition, PendingChange > 0 );

    XboxSetObjectPropertyInt( LadderInv, XboxTournamentPositionProperty(LadderIndex), NewPosition );
    XboxSetObjectPropertyInt( LadderInv, TEXT("PendingChange"), 0 );
    XboxSetObjectPropertyInt( LadderInv, TEXT("PendingPosition"), 0 );
    XboxSetObjectPropertyInt( LadderInv, TEXT("PendingRank"), 0 );
    XboxTournamentSavePosition( LadderIndex, NewPosition );

    GXboxMenu.TournamentLadder = LadderIndex;
    GXboxMenu.TournamentMatch = XboxTournamentAvailableMatch( Viewport, LadderIndex );
    GXboxFrontendTournamentOpenPending = 1;
    GXboxFrontendTournamentPostMatchPending = GXboxTournamentPostMatch.Valid;
    GXboxTournamentTransitionHandledLevel = Level;
    GXboxLog.Write( "XMENU Tournament result captured source=%s change=%d last=%d ladder=%d pending=%d rank=%d current=%d saved=%d new=%d post=%d",
        Source ? Source : "unknown",
        PendingChange,
        LastMatchType,
        LadderIndex,
        PendingPosition,
        PendingRank,
        CurrentPosition,
        SavedPosition,
        NewPosition,
        GXboxFrontendTournamentPostMatchPending ? 1 : 0 );
    XboxMenuReturnToFrontend( Viewport );
    return 1;
}

static void XboxTournamentTransitionTick( UXboxViewport* Viewport )
{
    if( !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player->GetLevel();
    if( !Level || !Level->GetLevelInfo() || !Level->GetLevelInfo()->Game )
        return;

    AGameInfo* Game = Level->GetLevelInfo()->Game;
    UClass* GameClass = Game->GetClass();
    if( !GameClass || appStricmp( GameClass->GetName(), TEXT("LadderTransition") ) != 0 )
        return;

    AInventory* LadderInv = XboxTournamentFindInventory( Viewport );
    if( LadderInv )
        XboxTournamentCapturePendingResult( Viewport, Level, LadderInv, "transition" );
}

static UBOOL XboxTournamentCompletedMatchTick( UXboxViewport* Viewport )
{
    static ULevel* EndedLevel = NULL;
    static DOUBLE EndedSince = 0.0;
    static DOUBLE LastRetryTime = 0.0;

    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    AGameInfo* Game = (Level && Level->GetLevelInfo()) ? Level->GetLevelInfo()->Game : NULL;
    if( !Player || !Game || !XboxIsTournamentLevel(Level) )
    {
        EndedLevel = NULL;
        EndedSince = 0.0;
        LastRetryTime = 0.0;
        return 0;
    }

    UClass* GameClass = Game->GetClass();
    if( GameClass && appStricmp( GameClass->GetName(), TEXT("LadderTransition") ) == 0 )
        return 0;

    FString GameEndedText;
    XboxGetObjectPropertyString( Game, TEXT("bGameEnded"), GameEndedText );
    UBOOL bGameEnded = appStricmp( *GameEndedText, TEXT("True") ) == 0 || appAtoi( *GameEndedText ) != 0;
    if( !bGameEnded )
    {
        if( EndedLevel == Level )
        {
            EndedLevel = NULL;
            EndedSince = 0.0;
            LastRetryTime = 0.0;
        }
        return 0;
    }

    DOUBLE Now = appSeconds();
    if( EndedLevel != Level )
    {
        EndedLevel = Level;
        EndedSince = Now;
        LastRetryTime = 0.0;
        GXboxLog.Write( "XMENU Tournament native result handoff armed map=%s state=%s",
            Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
            TCHAR_TO_ANSI(XboxPlayerStateName(Player)) );
        GXboxLog.Flush();
    }

    // Keep GameEnded input from re-entering RestartGame while its end screen is active.
    if( GXboxTournamentTransitionHandledLevel == Level || (Now - EndedSince) < 3.5 )
        return 1;
    if( LastRetryTime > 0.0 && (Now - LastRetryTime) < 1.0 )
        return 1;

    LastRetryTime = Now;
    AInventory* LadderInv = XboxTournamentFindInventory( Viewport );
    UFunction* RestartGameFunc = Game->FindFunction( FName(TEXT("RestartGame"), FNAME_Find) );
    if( !LadderInv || !RestartGameFunc )
    {
        GXboxLog.Write( "XMENU Tournament native result handoff waiting inventory=0x%08X restart=0x%08X",
            (DWORD)LadderInv,
            (DWORD)RestartGameFunc );
        GXboxLog.Flush();
        return 1;
    }

    Viewport->TravelURL = TEXT("");
    GXboxLog.Write( "XMENU Tournament native result evaluation begin" );
    GXboxLog.Flush();
    Game->ProcessEvent( RestartGameFunc, NULL );
    GXboxLog.Write( "XMENU Tournament native result evaluation returned travel=%s pendingChange=%d pendingPosition=%d pendingRank=%d",
        Viewport->TravelURL.Len() ? TCHAR_TO_ANSI(*Viewport->TravelURL) : "",
        XboxGetObjectPropertyInt( LadderInv, TEXT("PendingChange"), 0 ),
        XboxGetObjectPropertyInt( LadderInv, TEXT("PendingPosition"), 0 ),
        XboxGetObjectPropertyInt( LadderInv, TEXT("PendingRank"), 0 ) );
    GXboxLog.Flush();

    if( Viewport->TravelURL.Len() <= 0 )
        return 1;

    XboxTournamentCapturePendingResult( Viewport, Level, LadderInv, "native-endgame" );
    return 1;
}

static void XboxMenuStartTournamentMatch( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Viewport || !Client || !Client->Engine )
        return;

    XboxTournamentClampSelection( Viewport );
    FString Map = XboxTournamentFullMap( GXboxMenu.TournamentLadder, GXboxMenu.TournamentMatch );
    if( Map.Len() <= 0 )
    {
        GXboxLog.Write( "XMENU Tournament blocked: empty map ladder=%d match=%d", GXboxMenu.TournamentLadder, GXboxMenu.TournamentMatch );
        return;
    }

    if( !XboxTournamentEnsureInventory( Viewport ) )
        return;

    XboxMenuEnsureConsoleClass( Viewport, TEXT("UTMenu.UTConsole"), "TournamentMatch" );

    TCHAR PlayerURL[512];
    TCHAR URL[1024];
    XboxMenuBuildPlayerURL( PlayerURL, ARRAY_COUNT(PlayerURL) );
    appSprintf
    (
        URL,
        TEXT("%s?Game=%s?Tournament=%i%s"),
        *Map,
        GXboxTournamentLadders[GXboxMenu.TournamentLadder].GameClass,
        GXboxMenu.TournamentMatch,
        PlayerURL
    );

    XboxMenuClose( Viewport );
    XboxSplitResetRuntime( Client, "TournamentMatch" );
    GXboxLog.Write( "XMENU Tournament match travel with items: %s", TCHAR_TO_ANSI(URL) );
    GXboxLog.Flush();
    Client->Engine->SetClientTravel( Viewport, URL, 1, TRAVEL_Absolute );
}

static void XboxMenuStartTournament( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    XboxTournamentClampSelection( Viewport );
    GXboxMenu.Screen = XMS_Tournament;
    GXboxMenu.TournamentFocus = 0;
    XboxMenuReleaseMapPreviewTexture();
    GXboxLog.Write( "XTOUR progress menu profile=%d ladder=%d match=%d source=interactive",
        XboxProfileActiveCreated() ? GXboxActiveProfile + 1 : 0,
        GXboxMenu.TournamentLadder,
        GXboxMenu.TournamentMatch );
}

static void XboxTournamentProgressProofPrepareWin( UObject* Game, APlayerPawn* Player, FLOAT HighestBotScore, INT FragLimit )
{
    if( !Game || !Player || !Player->PlayerReplicationInfo )
        return;

    INT LadderIndex = Clamp<INT>( GXboxMenu.TournamentLadder, 0, ARRAY_COUNT(GXboxTournamentLadders)-1 );
    INT PlayerTeam = Player->PlayerReplicationInfo->Team;
    INT TeamGoal = -1;
    INT AssaultWon = -1;
    Player->PlayerReplicationInfo->Score = Max<FLOAT>( (FLOAT)FragLimit, HighestBotScore + 1.0f );

    if( LadderIndex == 1 || LadderIndex == 2 )
    {
        TeamGoal = Max<INT>( 1, XboxGetObjectPropertyInt( Game, TEXT("GoalTeamScore"), 1 ) );
        for( INT TeamIndex=0; TeamIndex<4; TeamIndex++ )
        {
            UObject* Team = XboxGetObjectPropertyObjectAt( Game, TEXT("Teams"), TeamIndex );
            if( Team )
                XboxSetObjectPropertyInt( Team, TEXT("Score"), TeamIndex == PlayerTeam ? TeamGoal : 0 );
        }
    }
    else if( LadderIndex == 3 )
    {
        UObject* Attacker = XboxGetObjectPropertyObjectAt( Game, TEXT("Attacker"), 0 );
        INT AttackerTeam = XboxGetObjectPropertyInt( Attacker, TEXT("TeamIndex"), -1 );
        AssaultWon = PlayerTeam == AttackerTeam ? 1 : 0;
        XboxSetObjectPropertyInt( Game, TEXT("bAssaultWon"), AssaultWon );
        XboxSetObjectPropertyInt( Game, TEXT("bDefenseSet"), 1 );
    }

    GXboxLog.Write( "XTOUR PROGRESS PROOF prepared ladder=%d mode=%s playerTeam=%d playerScore=%.1f highestBot=%.1f fragLimit=%d teamGoal=%d assaultWon=%d",
        LadderIndex,
        TCHAR_TO_ANSI(GXboxTournamentLadders[LadderIndex].Label),
        PlayerTeam,
        Player->PlayerReplicationInfo->Score,
        HighestBotScore,
        FragLimit,
        TeamGoal,
        AssaultWon );
}

static void XboxTournamentSmokeTick( UXboxViewport* Viewport )
{
    static INT SmokeStage = 0;
    static DOUBLE SmokeStartTime = 0.0;
    static UBOOL SmokeReadySent = 0;
    static DOUBLE SmokeLastFirePulse = 0.0;
    static DOUBLE SmokeLastCombatPulse = 0.0;
    static DOUBLE SmokeFireDownTime = 0.0;
    static DOUBLE SmokeLastStatusLog = 0.0;
    static UBOOL SmokeFireDown = 0;
    static UBOOL SmokeCombatLogged = 0;
    static UBOOL SmokeForcedEnd = 0;

    UBOOL bProgressWinProof = XboxTournamentProgressWinSmokeEnabled();
    if( (!XboxTournamentSmokeEnabled() && !bProgressWinProof) || !Viewport || !Viewport->Actor )
        return;

    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    DOUBLE Now = appSeconds();

    if( SmokeFireDown && Client && Client->Engine && (Now - SmokeFireDownTime) > 0.12 )
    {
        Client->Engine->InputEvent( Viewport, IK_LeftMouse, IST_Release, 0.0f );
        SmokeFireDown = 0;
    }

    if( SmokeStage == 0 )
    {
        XboxMenuOpen( Viewport );
        GXboxMenu.Screen = XMS_Main;
        GXboxMenu.MainFocus = 0;
        SmokeStartTime = Now;
        SmokeReadySent = 0;
        SmokeLastFirePulse = 0.0;
        SmokeLastCombatPulse = 0.0;
        SmokeFireDownTime = 0.0;
        SmokeLastStatusLog = 0.0;
        SmokeFireDown = 0;
        SmokeCombatLogged = 0;
        SmokeForcedEnd = 0;
        SmokeStage = 1;
        GXboxLog.Write( "XMENU TOURNAMENT SMOKE opened main menu" );
    }
    else if( SmokeStage == 1 && (Now - SmokeStartTime) > 1.0 )
    {
        GXboxMenu.TournamentLadder = XboxTournamentProofRequestedLadder();
        XboxMenuStartTournament( Viewport );
        SmokeStartTime = Now;
        SmokeStage = 2;
        GXboxLog.Write( "XMENU TOURNAMENT SMOKE selected Tournament ladder=%d mode=%s",
            GXboxMenu.TournamentLadder,
            TCHAR_TO_ANSI(GXboxTournamentLadders[GXboxMenu.TournamentLadder].Label) );
    }
    else if( SmokeStage == 2 && (Now - SmokeStartTime) > 1.0 )
    {
        GXboxMenu.TournamentFocus = 2;
        XboxMenuStartTournamentMatch( Viewport );
        SmokeStartTime = Now;
        SmokeReadySent = 0;
        SmokeLastFirePulse = 0.0;
        SmokeLastCombatPulse = 0.0;
        SmokeFireDownTime = 0.0;
        SmokeLastStatusLog = 0.0;
        SmokeFireDown = 0;
        SmokeCombatLogged = 0;
        SmokeForcedEnd = 0;
        SmokeStage = 3;
        GXboxLog.Write( "XMENU TOURNAMENT SMOKE launched native match" );
    }
    else if( SmokeStage == 3 )
    {
        APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
        ULevel* Level = Player ? Player->GetLevel() : NULL;
        UBOOL bTournamentLevel = Level && !XboxIsFrontendLevel(Level) && XboxIsTournamentLevel(Level);
        if( !SmokeReadySent && bTournamentLevel && (Now - SmokeStartTime) > 3.0 )
        {
            XboxTournamentHandleReadyInput( Viewport, 1, 0 );
            SmokeReadySent = 1;
            GXboxLog.Write( "XMENU TOURNAMENT SMOKE sent ready/start" );
            GXboxLog.Flush();
        }

        if( bTournamentLevel )
        {
            const TCHAR* StateName = XboxPlayerStateName(Player);
            UObject* Game = (Level && Level->GetLevelInfo()) ? Level->GetLevelInfo()->Game : NULL;
            DOUBLE ForcedEndDelay = bProgressWinProof ? 12.0 : 25.0;
            if( !SmokeForcedEnd && Player && Game && (Now - SmokeStartTime) > ForcedEndDelay )
            {
                UClass* BotClass = UObject::StaticLoadClass( APawn::StaticClass(), NULL, TEXT("Botpack.Bot"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
                APawn* WinningBot = NULL;
                FLOAT HighestBotScore = -1000000.0f;
                if( BotClass && Level && Level->GetLevelInfo() )
                {
                    for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; Pawn = Pawn->nextPawn )
                    {
                        if( Pawn->IsA(BotClass) && Pawn->PlayerReplicationInfo )
                        {
                            if( !WinningBot )
                                WinningBot = Pawn;
                            HighestBotScore = Max<FLOAT>( HighestBotScore, Pawn->PlayerReplicationInfo->Score );
                        }
                    }
                }

                if( Player->PlayerReplicationInfo && (bProgressWinProof || (WinningBot && WinningBot->PlayerReplicationInfo)) )
                {
                    INT FragLimit = Max<INT>( 1, XboxGetObjectPropertyInt( Game, TEXT("FragLimit"), 10 ) );
                    if( bProgressWinProof )
                        XboxTournamentProgressProofPrepareWin( Game, Player, HighestBotScore, FragLimit );
                    else
                        WinningBot->PlayerReplicationInfo->Score = Max<FLOAT>( (FLOAT)FragLimit, Player->PlayerReplicationInfo->Score + 1.0f );
                    UFunction* EndGameFunc = Game->FindFunction( FName(TEXT("EndGame"), FNAME_Find) );
                    if( EndGameFunc )
                    {
                        struct FEndGameParms
                        {
                            FString Reason;
                        } Parms;
                        Parms.Reason = TEXT("fraglimit");
                        Game->ProcessEvent( EndGameFunc, &Parms );
                        SmokeForcedEnd = 1;
                        if( bProgressWinProof )
                        {
                            GXboxLog.Write( "XTOUR PROGRESS PROOF forced player win profile=%d player=%.1f highestBot=%.1f fragLimit=%d",
                                GXboxActiveProfile + 1,
                                Player->PlayerReplicationInfo->Score,
                                HighestBotScore,
                                FragLimit );
                        }
                        else
                        {
                            GXboxLog.Write( "XMENU TOURNAMENT SMOKE forced bot win bot=%s score=%.1f player=%.1f fragLimit=%d",
                                WinningBot->PlayerReplicationInfo->PlayerName.Len() ? TCHAR_TO_ANSI(*WinningBot->PlayerReplicationInfo->PlayerName) : "bot",
                                WinningBot->PlayerReplicationInfo->Score,
                                Player->PlayerReplicationInfo->Score,
                                FragLimit );
                        }
                        GXboxLog.Flush();
                    }
                }
            }

            UBOOL bNeedsFire =
                Player
            &&  !GXboxMenu.Active
            &&  ( Player->Health <= 0
                || Player->bHidden
                || appStricmp( StateName, TEXT("Dying") ) == 0 );
            UBOOL bAliveCombat =
                Player
            &&  !GXboxMenu.Active
            &&  Player->Health > 0
            &&  !Player->bHidden
            &&  appStricmp( StateName, TEXT("PlayerWalking") ) == 0;

            if( bAliveCombat )
            {
                FLOAT MoveScale = Client ? Max<FLOAT>( Client->ScaleXYZ, 100.0f ) : 100.0f;
                FLOAT LookScale = Client ? Max<FLOAT>( Client->ScaleRUV, 100.0f ) : 100.0f;
                FLOAT StrafeSign = appSin( Now * 1.4 ) >= 0.0 ? 1.0f : -1.0f;
                Player->aForward += MoveScale;
                Player->aStrafe  += MoveScale * 0.45f * StrafeSign;
                Player->aTurn    += LookScale * 2.2f;

                if( !SmokeCombatLogged )
                {
                    SmokeCombatLogged = 1;
                    GXboxLog.Write( "XMENU TOURNAMENT SMOKE driving live player with movement/fire input" );
                }

                if( Client && Client->Engine && !SmokeFireDown && (Now - SmokeLastCombatPulse) > 0.45 )
                {
                    Client->Engine->InputEvent( Viewport, IK_LeftMouse, IST_Press, 0.0f );
                    SmokeFireDown = 1;
                    SmokeFireDownTime = Now;
                    SmokeLastCombatPulse = Now;
                }
            }

            if( bNeedsFire && Client && Client->Engine && !SmokeFireDown && (Now - SmokeLastFirePulse) > 1.0 )
            {
                Client->Engine->InputEvent( Viewport, IK_LeftMouse, IST_Press, 0.0f );
                SmokeFireDown = 1;
                SmokeFireDownTime = Now;
                SmokeLastFirePulse = Now;
                GXboxLog.Write( "XMENU TOURNAMENT SMOKE fire pulse state=%s health=%d hidden=%d",
                    TCHAR_TO_ANSI(StateName),
                    Player ? Player->Health : -999,
                    Player && Player->bHidden ? 1 : 0 );
                GXboxLog.Flush();
            }

            if( (Now - SmokeLastStatusLog) > 5.0 )
            {
                SmokeLastStatusLog = Now;
                INT FragLimit = XboxGetObjectPropertyInt( Game, TEXT("FragLimit"), -1 );
                FString bGameEnded;
                XboxGetObjectPropertyString( Game, TEXT("bGameEnded"), bGameEnded );
                GXboxLog.Write( "XMENU TOURNAMENT SMOKE status state=%s health=%d hidden=%d pscore=%.1f fragLimit=%d ended=%s",
                    TCHAR_TO_ANSI(StateName),
                    Player ? Player->Health : -999,
                    Player && Player->bHidden ? 1 : 0,
                    (Player && Player->PlayerReplicationInfo) ? Player->PlayerReplicationInfo->Score : -999.0f,
                    FragLimit,
                    bGameEnded.Len() ? TCHAR_TO_ANSI(*bGameEnded) : "" );
            }
        }

        if( GXboxMenu.Active && GXboxMenu.Screen == XMS_TournamentPostMatch )
        {
            SmokeStage = 4;
            SmokeStartTime = Now;
            GXboxLog.Write( "XMENU TOURNAMENT SMOKE reached post-match screen" );
            GXboxLog.Flush();
        }
    }
    else if( SmokeStage == 4 && (Now - SmokeStartTime) > 4.0 )
    {
        SmokeStartTime = Now;
        GXboxLog.Write( "XMENU TOURNAMENT SMOKE holding post-match screen" );
        GXboxLog.Flush();
    }
}

static void XboxConfigureFullMenuProof( UXboxViewport* Viewport, INT Request )
{
    XboxMenuOpen( Viewport );

    if( Request != XFMP_ProfileSelect && Request != XFMP_ProfileName )
    {
        GXboxSessionProfileLoaded = 1;
        GXboxProfileNameMode = XPNM_None;
        GXboxProfileNamePort = -1;
    }

    if( Request == XFMP_Main )
    {
        GXboxMenu.Screen = XMS_Main;
        GXboxMenu.MainFocus = 0;
    }
    else if( Request == XFMP_Pause )
    {
        GXboxMenu.Screen = XMS_Pause;
        GXboxMenu.PauseFocus = 0;
    }
    else if( Request == XFMP_PauseConfirm )
    {
        GXboxMenu.Screen = XMS_Pause;
        GXboxMenu.PauseFocus = 1;
        GXboxPauseReturnConfirm = 1;
        GXboxPauseReturnConfirmFocus = 1;
    }
    else if( Request == XFMP_InstantAction )
    {
        GXboxMenu.Screen = XMS_InstantAction;
        GXboxMenu.InstantFocus = 0;
        if( XboxFullMenuProofExtraMarkerExists("XboxProofSelectLMS.ini") )
        {
            const INT LMS = XboxFullMenuProofFindGameType( TEXT("Botpack.LastManStanding") );
            if( LMS != INDEX_NONE )
                GXboxMenu.InstantGameType = LMS;
        }
    }
    else if( Request == XFMP_Mutators )
    {
        GXboxMenu.Screen = XMS_Mutators;
        GXboxMenu.InstantFocus = 6;
        GXboxMenu.InstantMutatorChoice = 0;
        if( XboxFullMenuProofExtraMarkerExists("XboxProofMutatorsPage2.ini") )
            GXboxMenu.InstantMutatorChoice = Min<INT>( 6, XboxMenuMutatorCount()-1 );
        else if( XboxFullMenuProofExtraMarkerExists("XboxProofMutatorsPage3.ini") )
            GXboxMenu.InstantMutatorChoice = Min<INT>( 10, XboxMenuMutatorCount()-1 );
        else if( XboxFullMenuProofExtraMarkerExists("XboxProofMutatorsPage4.ini") )
            GXboxMenu.InstantMutatorChoice = Min<INT>( 14, XboxMenuMutatorCount()-1 );
        else if( XboxFullMenuProofExtraMarkerExists("XboxProofMutatorsEnd.ini") )
            GXboxMenu.InstantMutatorChoice = Max<INT>( 0, XboxMenuMutatorCount()-1 );
    }
    else if( Request == XFMP_Tournament )
    {
        GXboxMenu.Screen = XMS_Tournament;
        GXboxMenu.TournamentFocus = 0;
        GXboxMenu.TournamentLadder = XboxTournamentProofRequestedLadder();
        XboxTournamentClampSelection( Viewport );
        GXboxLog.Write( "XTOUR progress menu profile=%d ladder=%d match=%d source=proof",
            XboxProfileActiveCreated() ? GXboxActiveProfile + 1 : 0,
            GXboxMenu.TournamentLadder,
            GXboxMenu.TournamentMatch );
    }
    else if( Request == XFMP_TournamentResult )
    {
        GXboxMenu.Screen = XMS_TournamentPostMatch;
        GXboxTournamentPostMatch.Valid = 1;
        GXboxTournamentPostMatch.Advanced = 1;
        GXboxTournamentPostMatch.LadderIndex = 0;
        GXboxTournamentPostMatch.CompletedMatch = 1;
        GXboxTournamentPostMatch.NextMatch = 2;
        GXboxTournamentPostMatch.PreviousPosition = 1;
        GXboxTournamentPostMatch.NewPosition = 2;
        appStrcpy( GXboxTournamentPostMatch.MatchTitle, TEXT("OBLIVION") );
        appStrcpy( GXboxTournamentPostMatch.MapName, TEXT("DM-Oblivion") );
        appStrcpy( GXboxTournamentPostMatch.RankTitle, TEXT("CONTENDER") );
    }
    else if( Request == XFMP_SplitReady )
    {
        XboxSplitReadyReset( Viewport );
        INT LayoutProofMask = XboxSplitLayoutProofMask();
        for( INT Port=0; Port<4; Port++ )
        {
            UBOOL bRequested = LayoutProofMask ? ((LayoutProofMask & (1 << Port)) != 0) : 1;
            if( bRequested && XboxSplitEnsureProfileForJoin(Viewport, Port, 1) )
            {
                GXboxSplitReadySlots[Port].Joined = 1;
                GXboxSplitReadySlots[Port].Locked = 1;
                GXboxSplitReadySlots[Port].Team = Port;
            }
        }
        GXboxMenu.Screen = XMS_SplitReady;
        GXboxLog.Write( "XSPLIT menu proof requestedMask=0x%X readyMask=0x%X lockedMask=0x%X",
            LayoutProofMask, XboxSystemLinkReadyMask(), XboxSystemLinkLockedMask() );
    }
    else if( Request == XFMP_SplitMap )
    {
        GXboxMenu.Screen = XMS_SplitMapSelect;
        GXboxMenu.SplitFocus = 0;
        if( XboxFullMenuProofExtraMarkerExists("XboxProofSelectLMS.ini") )
        {
            const INT LMS = XboxFullMenuProofFindGameType( TEXT("Botpack.LastManStanding") );
            if( LMS != INDEX_NONE )
                GXboxMenu.InstantGameType = LMS;
        }
    }
    else if( Request == XFMP_ProfileSelect )
    {
        XboxProfileLoadDirectory( 1 );
        GXboxProfileGateFocus = 0;
        if( XboxProfileCreatedCount() > 0 )
            GXboxMenu.Screen = XMS_ProfileSelect;
        else
            XboxProfileBeginCreate( XPNM_StartupCreate, -1, XMS_ProfileSelect );
    }
    else if( Request == XFMP_ProfileSwitch )
    {
        XboxProfileLoadDirectory( 1 );
        GXboxProfileGateFocus = XboxProfileGateRowForIndex( GXboxActiveProfile );
        GXboxMenu.Screen = XMS_ProfileSelect;
    }
    else if( Request == XFMP_PlayerSetup )
    {
        GXboxMenu.Screen = XMS_PlayerSetup;
        GXboxMenu.PlayerFocus = 0;
        XboxProfileOpen( Viewport, 0 );
    }
    else if( Request == XFMP_ProfileName )
    {
        XboxProfileOpen( Viewport );
        XboxProfileBeginCreate( XPNM_StartupCreate, -1, XMS_ProfileSelect );
    }
    else if( Request == XFMP_ControlsTop || Request == XFMP_ControlsButtons )
    {
        GXboxMenu.Screen = XMS_Controls;
        GXboxMenu.ControlsFocus = Request == XFMP_ControlsButtons ? XCR_FirstButton + XCB_Count - 1 : XCR_Preset;
    }
    else if( Request == XFMP_Settings )
    {
        GXboxMenu.Screen = XMS_Settings;
        GXboxMenu.SettingsFocus = XSH_Controls;
        XboxMenuLoadSettings();
    }
    else if( Request == XFMP_Audio )
    {
        GXboxMenu.Screen = XMS_SettingsAudio;
        GXboxMenu.SettingsFocus = XAR_MusicVolume;
        XboxMenuLoadSettings();
    }
    else if( Request == XFMP_Video )
    {
        GXboxMenu.Screen = XMS_SettingsVideo;
        GXboxMenu.SettingsFocus = XVR_Brightness;
        XboxMenuLoadSettings();
    }
    else if( Request == XFMP_ComingSoon )
    {
        GXboxMenu.Screen = XMS_ComingSoon;
        appStrcpy( GXboxMenu.ComingSoonTitle, TEXT("PLACEHOLDER") );
    }

    if( XboxIsSystemLinkProofRequest(Request) )
    {
        XboxSystemLinkEnsureState();
        GXboxSystemLink.Started = 1;
        GXboxSystemLink.SocketsReady = 1;
        GXboxSystemLink.Socket = INVALID_SOCKET;
        GXboxSystemLink.LocalPort = GXboxSystemLinkBasePort;
        GXboxSystemLink.LocalId = 0x10000001;
        GXboxSystemLink.HostId = Request == XFMP_SystemLinkDiscovery ? 0 : GXboxSystemLink.LocalId;
        GXboxSystemLink.Role = Request == XFMP_SystemLinkClientMap ? XSLR_Client :
            Request == XFMP_SystemLinkDiscovery ? XSLR_Seeking : XSLR_Host;
        GXboxSystemLink.Phase = Request == XFMP_SystemLinkHostMap || Request == XFMP_SystemLinkClientMap ? XSLP_MapSelect :
            Request == XFMP_SystemLinkReady ? XSLP_Ready : XSLP_Discovery;
        GXboxSystemLink.ReadyConfirmed = 0;
        GXboxSystemLink.Peers.Empty();

        if( Request == XFMP_SystemLinkReady )
        {
            XboxSplitReadyReset( Viewport );
            for( INT Port=0; Port<4; Port++ )
            {
                if( XboxSplitEnsureProfileForJoin(Viewport, Port, 1) )
                {
                    GXboxSplitReadySlots[Port].Joined = 1;
                    GXboxSplitReadySlots[Port].Locked = 1;
                    GXboxSplitReadySlots[Port].Team = Port;
                }
            }
            INT PeerIndex = GXboxSystemLink.Peers.AddZeroed();
            FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(PeerIndex);
            Peer.Id = 0x20000002;
            Peer.HostId = GXboxSystemLink.HostId;
            Peer.Role = XSLR_Client;
            Peer.Phase = XSLP_ReadyConfirmed;
            Peer.ReadyMask = 1;
            Peer.LockedMask = 1;
            Peer.Confirmed = 1;
            Peer.FirstSeen = appSeconds();
            Peer.LastSeen = Peer.FirstSeen;
        }

        GXboxMenu.Screen = Request == XFMP_SystemLinkHostMap || Request == XFMP_SystemLinkClientMap
            ? XMS_SystemLinkMapSelect : XMS_SystemLink;
        GXboxMenu.SplitFocus = 0;
        if( XboxFullMenuProofExtraMarkerExists("XboxProofSelectLMS.ini") )
        {
            const INT LMS = XboxFullMenuProofFindGameType( TEXT("Botpack.LastManStanding") );
            if( LMS != INDEX_NONE )
            {
                GXboxMenu.InstantGameType = LMS;
                GXboxSystemLink.GameType = LMS;
            }
        }
    }

    const INT ProofGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1 );
    GXboxLog.Write( "XMENU FULL PROOF configured request=%d screen=%s gameType=%d gameClass=%s mutatorChoice=%d mutatorCount=%d",
        Request,
        TCHAR_TO_ANSI(XboxMenuScreenName(GXboxMenu.Screen)),
        ProofGameType,
        TCHAR_TO_ANSI(*XboxMenuGameType(ProofGameType).URLValue),
        GXboxMenu.InstantMutatorChoice,
        XboxMenuMutatorCount() );
}

static void XboxMenuProofSmokeTick( UXboxViewport* Viewport )
{
    static INT SmokeStage = 0;
    static DOUBLE SmokeStartTime = 0.0;
    static DOUBLE SmokeFirstSeenTime = 0.0;
    static UBOOL bControlsProof = 0;
    static UBOOL bSettingsProof = 0;
    static UBOOL bAudioProof = 0;
    static UBOOL bVideoProof = 0;
    static UBOOL bMainProof = 0;
    static INT FullProof = XFMP_None;

    if( !Viewport || !Viewport->Actor )
        return;

    bControlsProof = XboxControlsProofSmokeEnabled();
    bSettingsProof = XboxSettingsProofSmokeEnabled();
    bAudioProof = XboxAudioSettingsProofSmokeEnabled();
    bVideoProof = XboxVideoSettingsProofSmokeEnabled();
    bMainProof = XboxMainMenuProofSmokeEnabled();
    FullProof = XboxFullMenuProofRequested();
    if( !bControlsProof && !bSettingsProof && !bAudioProof && !bVideoProof && !bMainProof && FullProof == XFMP_None )
        return;

    if( SmokeStage == 0 )
    {
        if( SmokeFirstSeenTime <= 0.0 )
        {
            SmokeFirstSeenTime = appSeconds();
            GXboxLog.Write( "XMENU PROOF waiting for stable game frame" );
            return;
        }
        if( (appSeconds() - SmokeFirstSeenTime) < 1.0 )
            return;

        if( FullProof != XFMP_None )
        {
            XboxConfigureFullMenuProof( Viewport, FullProof );
        }
        else
        {
            XboxMenuOpen( Viewport );
            if( bMainProof )
            {
                GXboxMenu.Screen = XMS_Main;
                GXboxMenu.MainFocus = 0;
                GXboxLog.Write( "XMENU PROOF opened Main menu screen" );
            }
            else if( bControlsProof )
            {
                GXboxMenu.Screen = XMS_Controls;
                GXboxMenu.ControlsFocus = XCR_Preset;
                GXboxLog.Write( "XMENU PROOF opened Controls screen" );
            }
            else if( bAudioProof )
            {
                GXboxMenu.Screen = XMS_SettingsAudio;
                GXboxMenu.SettingsFocus = XAR_MusicVolume;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU PROOF opened Audio settings screen" );
            }
            else if( bVideoProof )
            {
                UXboxClient* Client = XboxMenuGetClient( Viewport );
                if( Client )
                {
                    Client->Brightness = 0.60f;
                    Client->DisplayContrast = 1.10f;
                    Client->DisplayGamma = 1.25f;
                    Client->SafeAreaSize = 92;
                    Client->SafeAreaX = 18;
                    Client->SafeAreaY = -12;
                    XboxRenderSetDisplayCalibration( Client->Brightness, Client->DisplayContrast, Client->DisplayGamma );
                }
                GXboxMenu.Screen = XMS_SettingsVideo;
                GXboxMenu.SettingsFocus = XVR_Brightness;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU PROOF opened Video settings screen brightness=0.60 contrast=1.10 gamma=1.25 safeAreaSize=92 safeAreaX=18 safeAreaY=-12" );
            }
            else
            {
                GXboxMenu.Screen = XMS_Settings;
                GXboxMenu.SettingsFocus = XSH_Controls;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU PROOF opened Settings hub screen" );
            }
        }
        SmokeStartTime = appSeconds();
        SmokeStage = 1;
        GXboxLog.Flush();
    }
    else if( SmokeStage == 1 && (appSeconds() - SmokeStartTime) > 4.0 )
    {
        SmokeStartTime = appSeconds();
        const INT ProofGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1 );
        GXboxLog.Write( "XMENU PROOF holding screen=%s gameType=%d gameClass=%s mutatorChoice=%d mutatorCount=%d",
            TCHAR_TO_ANSI(XboxMenuScreenName(GXboxMenu.Screen)),
            ProofGameType,
            TCHAR_TO_ANSI(*XboxMenuGameType(ProofGameType).URLValue),
            GXboxMenu.InstantMutatorChoice,
            XboxMenuMutatorCount() );
        GXboxLog.Flush();
    }
}

static INT XboxSystemLinkSmokeReadyLocalSlots( UXboxViewport* Viewport )
{
    XboxSplitReadyEnsure();

    INT DesiredSlots = XboxSystemLinkFourPlayerStressEnabled() ? 4 : 1;
    for( INT i=0; i<4; i++ )
    {
        FXboxSplitReadySlot& Slot = GXboxSplitReadySlots[i];
        if( i >= DesiredSlots )
        {
            Slot.Joined = 0;
            Slot.Locked = 0;
            Slot.Profile = -1;
            Slot.Focus = 0;
            appMemzero( &GXboxSplitProfileControls[i], sizeof(GXboxSplitProfileControls[i]) );
            continue;
        }

        if( !XboxSplitEnsureProfileForJoin(Viewport, i, 1) )
            continue;
        Slot.Joined = 1;
        Slot.Locked = 1;
        Slot.Focus = 0;
    }

    return XboxSplitReadyJoinedCount();
}

static void XboxSystemLinkSmokeTick( UXboxViewport* Viewport )
{
    static INT SmokeStage = 0;
    static DOUBLE SmokeStartTime = 0.0;
    static DOUBLE SmokeLastStatusTime = 0.0;
    static INT LifecycleGameplayJoins = 0;
    static DOUBLE LifecycleBackOutDelay = 0.0;

    if( !XboxSystemLinkSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    DOUBLE Now = appSeconds();
    UBOOL bLifecycle = XboxSystemLinkLifecycleSmokeEnabled();
    ULevel* CurrentLevel = Viewport->Actor->GetLevel();

    if( bLifecycle && SmokeStage == -1 )
    {
        if( Now - SmokeStartTime >= 2.0 )
        {
            UXboxClient* Client = (UXboxClient*)Viewport->GetOuter();
            UGameEngine* GameEngine = Client ? Cast<UGameEngine>(Client->Engine) : NULL;
            UBOOL bHadPending = GameEngine && GameEngine->GPendingLevel;
            if( bHadPending )
                GameEngine->CancelPending();
            XboxSystemLinkAbortTravelCleanup( "lifecycle failed join back-out" );
            XboxMenuOpen( Viewport );
            XboxSplitReadyReset( Viewport );
            GXboxMenu.Screen = XMS_SystemLink;
            GXboxMenu.MainFocus = 2;
            GXboxMenu.SplitFocus = 0;
            XboxSystemLinkStart( Viewport );
            SmokeStartTime = Now;
            SmokeLastStatusTime = 0.0;
            SmokeStage = 1;
            GXboxLog.Write( "XSL LIFECYCLE failed join cancelled pending=%d; entered first lobby", bHadPending ? 1 : 0 );
        }
        return;
    }

    if( bLifecycle && SmokeStage >= 2 && SmokeStage <= 4 && CurrentLevel && !XboxIsFrontendLevel(CurrentLevel)
    &&  CurrentLevel->GetLevelInfo()
    &&  (CurrentLevel->GetLevelInfo()->NetMode == NM_ListenServer || CurrentLevel->GetLevelInfo()->NetMode == NM_Client)
    &&  Viewport->Actor->PlayerReplicationInfo
    &&  !Viewport->Actor->PlayerReplicationInfo->bIsSpectator )
    {
        LifecycleGameplayJoins++;
        SmokeStartTime = Now;
        if( LifecycleGameplayJoins >= 2 )
        {
            const TCHAR* LifecyclePlayerName = Viewport->Actor->PlayerReplicationInfo
                ? *Viewport->Actor->PlayerReplicationInfo->PlayerName
                : TEXT("none");
            SmokeStage = 8;
            GXboxLog.Write( "XSL LIFECYCLE PASS second gameplay join map=%s net=%d player=%s",
                TCHAR_TO_ANSI(*CurrentLevel->URL.Map),
                (INT)CurrentLevel->GetLevelInfo()->NetMode,
                TCHAR_TO_ANSI(LifecyclePlayerName) );
        }
        else
        {
            SmokeStage = 5;
            LifecycleBackOutDelay = CurrentLevel->GetLevelInfo()->NetMode == NM_ListenServer ? 8.0 : 6.0;
            GXboxLog.Write( "XSL LIFECYCLE first gameplay join map=%s net=%d; holding %.1fs before back-out",
                TCHAR_TO_ANSI(*CurrentLevel->URL.Map),
                (INT)CurrentLevel->GetLevelInfo()->NetMode,
                LifecycleBackOutDelay );
        }
        return;
    }

    if( bLifecycle && SmokeStage == 5 )
    {
        if( Now - SmokeStartTime >= LifecycleBackOutDelay )
        {
            XboxMenuReturnToFrontend( Viewport );
            SmokeStartTime = Now;
            SmokeStage = 6;
            GXboxLog.Write( "XSL LIFECYCLE gameplay back-out queued after first join" );
        }
        return;
    }

    if( bLifecycle && SmokeStage == 6 )
    {
        if( CurrentLevel && XboxIsFrontendLevel(CurrentLevel) )
        {
            SmokeStartTime = Now;
            SmokeStage = 7;
            GXboxLog.Write( "XSL LIFECYCLE frontend restored after first join sessionRegistered=%d launch=0x%08X ack=0x%08X pending=%d peers=%d",
                GXboxSystemLink.SessionRegistered ? 1 : 0,
                GXboxSystemLink.LaunchId,
                GXboxSystemLink.LaunchAckId,
                GXboxSystemLink.PendingTravel ? 1 : 0,
                GXboxSystemLink.Peers.Num() );
        }
        return;
    }

    if( bLifecycle && SmokeStage == 7 )
    {
        if( Now - SmokeStartTime >= 2.0 )
        {
            GXboxLog.Write( "XSL LIFECYCLE pre-second-lobby stale sessionRegistered=%d launch=0x%08X ack=0x%08X pending=%d peers=%d started=%d socket=%d",
                GXboxSystemLink.SessionRegistered ? 1 : 0,
                GXboxSystemLink.LaunchId,
                GXboxSystemLink.LaunchAckId,
                GXboxSystemLink.PendingTravel ? 1 : 0,
                GXboxSystemLink.Peers.Num(),
                GXboxSystemLink.Started ? 1 : 0,
                (INT)GXboxSystemLink.Socket );
            XboxMenuOpen( Viewport );
            XboxSplitReadyReset( Viewport );
            GXboxMenu.Screen = XMS_SystemLink;
            GXboxMenu.MainFocus = 2;
            GXboxMenu.SplitFocus = 0;
            XboxSystemLinkStart( Viewport );
            SmokeStartTime = Now;
            SmokeLastStatusTime = 0.0;
            SmokeStage = 1;
            GXboxLog.Write( "XSL LIFECYCLE entered second lobby sessionRegistered=%d launch=0x%08X ack=0x%08X pending=%d peers=%d",
                GXboxSystemLink.SessionRegistered ? 1 : 0,
                GXboxSystemLink.LaunchId,
                GXboxSystemLink.LaunchAckId,
                GXboxSystemLink.PendingTravel ? 1 : 0,
                GXboxSystemLink.Peers.Num() );
        }
        return;
    }

    if( bLifecycle && SmokeStage == 8 )
        return;

    if( SmokeStage == 0 )
    {
        if( bLifecycle )
        {
            UXboxClient* Client = (UXboxClient*)Viewport->GetOuter();
            if( Client && Client->Engine )
            {
                const TCHAR* FailedURL = TEXT("192.0.2.1:7777?LAN?Name=LifecycleProbe?Class=Botpack.TMale2?Team=255");
                Client->Engine->SetClientTravel( Viewport, FailedURL, 0, TRAVEL_Absolute );
                SmokeStartTime = Now;
                SmokeLastStatusTime = 0.0;
                SmokeStage = -1;
                GXboxLog.Write( "XSL LIFECYCLE queued deliberate failed join url=%s", TCHAR_TO_ANSI(FailedURL) );
                return;
            }
        }
        XboxMenuOpen( Viewport );
        XboxSplitReadyReset( Viewport );
        GXboxMenu.Screen = XMS_SystemLink;
        GXboxMenu.MainFocus = 2;
        GXboxMenu.SplitFocus = 0;
        XboxSystemLinkStart( Viewport );
        SmokeStartTime = Now;
        SmokeLastStatusTime = 0.0;
        SmokeStage = 1;
        GXboxLog.Write( "XSL SMOKE opened System Link lobby" );
        return;
    }

    if( !GXboxMenu.Active )
        return;

    if( GXboxMenu.Screen != XMS_SystemLink && GXboxMenu.Screen != XMS_SystemLinkMapSelect )
        return;

    if( SmokeStage == 1 )
    {
        if( GXboxSystemLink.HostId && XboxSystemLinkGroupMachineCount() >= 2 )
        {
            INT SmokeSlots = XboxSystemLinkSmokeReadyLocalSlots( Viewport );
            XboxSystemLinkMarkLocalReadyChanged();
            GXboxSystemLink.ReadyConfirmed = 1;
            GXboxSystemLink.Phase = XSLP_ReadyConfirmed;
            XboxSystemLinkSendProbe();
            SmokeStartTime = Now;
            SmokeStage = 2;
            GXboxLog.Write( "XSL SMOKE local slots joined/locked/confirmed role=%s host=0x%08X machines=%d slots=%d stress4p=%d readyMask=0x%X lockedMask=0x%X",
                TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
                GXboxSystemLink.HostId,
                XboxSystemLinkGroupMachineCount(),
                SmokeSlots,
                XboxSystemLinkFourPlayerStressEnabled() ? 1 : 0,
                XboxSystemLinkReadyMask(),
                XboxSystemLinkLockedMask() );
        }
        else if( Now - SmokeLastStatusTime >= 2.0 )
        {
            SmokeLastStatusTime = Now;
            GXboxLog.Write( "XSL SMOKE waiting for peer role=%s host=0x%08X peers=%d machines=%d",
                TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
                GXboxSystemLink.HostId,
                GXboxSystemLink.Peers.Num(),
                XboxSystemLinkGroupMachineCount() );
        }
        return;
    }

    if( SmokeStage == 2 )
    {
        if( GXboxSystemLink.Role == XSLR_Host && GXboxMenu.Screen == XMS_SystemLinkMapSelect )
        {
            SmokeStartTime = Now;
            SmokeStage = 3;
            GXboxLog.Write( "XSL SMOKE host reached map select machines=%d confirmed=%d",
                XboxSystemLinkGroupMachineCount(), XboxSystemLinkConfirmedMachineCount() );
        }
        else if( Now - SmokeLastStatusTime >= 2.0 )
        {
            SmokeLastStatusTime = Now;
            GXboxLog.Write( "XSL SMOKE waiting for all ready role=%s phase=%s screen=%d confirmed=%d/%d",
                TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
                TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
                (INT)GXboxMenu.Screen,
                XboxSystemLinkConfirmedMachineCount(),
                XboxSystemLinkGroupMachineCount() );
        }
        return;
    }

    if( SmokeStage == 3 )
    {
        if( GXboxSystemLink.Role == XSLR_Host && GXboxMenu.Screen == XMS_SystemLinkMapSelect && Now - SmokeStartTime >= GXboxSystemLinkSmokeMapSelectDelaySeconds )
        {
            INT SmokeGameType = 0;
            INT SmokeMapIndex = XboxMenuFindMapIndexByFile( SmokeGameType, TEXT("DM-Fractal.unr") );
            if( SmokeMapIndex != INDEX_NONE )
            {
                GXboxMenu.InstantGameType = SmokeGameType;
                GXboxMenu.InstantMap[SmokeGameType] = SmokeMapIndex;
                GXboxSystemLink.GameType = SmokeGameType;
                GXboxSystemLink.MapIndex = SmokeMapIndex;
                const FXboxDiscoveredOption& SmokeMap = XboxMenuMap( SmokeGameType, SmokeMapIndex );
                GXboxLog.Write( "XSL SMOKE lightweight map selected %s index=%d game=%d",
                    TCHAR_TO_ANSI(*SmokeMap.URLValue), SmokeMapIndex, SmokeGameType );
            }
            else
            {
                INT CurrentGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, 63 );
                GXboxLog.Write( "XSL SMOKE lightweight map missing; using current map game=%d map=%d",
                    CurrentGameType,
                    GXboxMenu.InstantMap[CurrentGameType] );
            }
            GXboxMenu.SplitFocus = 4;
            if( XboxSystemLinkScheduleHostLaunch() )
            {
                SmokeStartTime = Now;
                SmokeStage = 4;
                GXboxLog.Write( "XSL SMOKE host scheduled launch" );
            }
            else if( Now - SmokeLastStatusTime >= 2.0 )
            {
                SmokeLastStatusTime = Now;
                GXboxLog.Write( "XSL SMOKE host launch retry role=%s phase=%s confirmed=%d/%d",
                    TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
                    TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
                    XboxSystemLinkConfirmedMachineCount(),
                    XboxSystemLinkGroupMachineCount() );
            }
        }
        return;
    }

    if( SmokeStage == 4 && Now - SmokeLastStatusTime >= 2.0 )
    {
        SmokeLastStatusTime = Now;
        GXboxLog.Write( "XSL SMOKE waiting for launch travel role=%s phase=%s pending=%d ack=0x%08X launch=0x%08X",
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
            TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
            GXboxSystemLink.PendingTravel ? 1 : 0,
            GXboxSystemLink.LaunchAckId,
            GXboxSystemLink.LaunchId );
    }
}

static UClass* XboxMenuLoadGameClass( const TCHAR* GameClassName )
{
    if( !GameClassName || !GameClassName[0] )
        return NULL;
    return UObject::StaticLoadClass( AGameInfo::StaticClass(), NULL, GameClassName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
}

static UBOOL XboxMenuGameIsChildOf( const TCHAR* GameClassName, const TCHAR* ParentClassName )
{
    UClass* GameClass = XboxMenuLoadGameClass( GameClassName );
    UClass* ParentClass = XboxMenuLoadGameClass( ParentClassName );
    return GameClass && ParentClass && GameClass->IsChildOf( ParentClass );
}

static UBOOL XboxMenuIsFixedCoreGameClass( const TCHAR* GameClassName )
{
    return GameClassName
        && (appStricmp(GameClassName, TEXT("Botpack.DeathMatchPlus")) == 0
        ||  appStricmp(GameClassName, TEXT("Botpack.LastManStanding")) == 0
        ||  appStricmp(GameClassName, TEXT("Botpack.CTFGame")) == 0
        ||  appStricmp(GameClassName, TEXT("Botpack.Domination")) == 0
        ||  appStricmp(GameClassName, TEXT("Botpack.Assault")) == 0);
}

static UBOOL XboxMenuGameUsesObjectiveRules( const TCHAR* GameClassName )
{
    if( GameClassName && appStricmp(GameClassName, TEXT("Botpack.Assault")) == 0 )
        return 1;
    if( XboxMenuIsFixedCoreGameClass(GameClassName) )
        return 0;
    return XboxMenuGameIsChildOf( GameClassName, TEXT("Botpack.Assault") );
}

static UBOOL XboxMenuGameUsesLives( const TCHAR* GameClassName )
{
    if( GameClassName && appStricmp(GameClassName, TEXT("Botpack.LastManStanding")) == 0 )
        return 1;
    if( XboxMenuIsFixedCoreGameClass(GameClassName) )
        return 0;
    return XboxMenuGameIsChildOf( GameClassName, TEXT("Botpack.LastManStanding") );
}

static UBOOL XboxMenuGameUsesTeamScore( const TCHAR* GameClassName )
{
    if( XboxMenuGameUsesObjectiveRules( GameClassName ) )
        return 0;
    if( GameClassName
    && (appStricmp(GameClassName, TEXT("Botpack.CTFGame")) == 0
    ||  appStricmp(GameClassName, TEXT("Botpack.Domination")) == 0) )
        return 1;
    if( XboxMenuIsFixedCoreGameClass(GameClassName) )
        return 0;
    return XboxMenuGameIsChildOf( GameClassName, TEXT("Botpack.TeamGamePlus") );
}

static void XboxMenuApplyMatchRuleDefaults( const TCHAR* GameClassName, INT ScoreLimit, INT MinPlayers )
{
    ScoreLimit = Max<INT>( 0, ScoreLimit );
    MinPlayers = Max<INT>( 0, MinPlayers );
    UBOOL bObjective = XboxMenuGameUsesObjectiveRules( GameClassName );
    UBOOL bTeamScore = XboxMenuGameUsesTeamScore( GameClassName );

    XboxSetClassDefaultPropertyInt( GameClassName, TEXT("InitialBots"), 0 );
    XboxSetClassDefaultPropertyInt( GameClassName, TEXT("MinPlayers"), MinPlayers );

    if( bObjective )
    {
        XboxSetClassDefaultPropertyInt( GameClassName, TEXT("FragLimit"), 0 );
        XboxSetClassDefaultPropertyInt( GameClassName, TEXT("GoalTeamScore"), 0 );
    }
    else if( bTeamScore )
    {
        XboxSetClassDefaultPropertyInt( GameClassName, TEXT("FragLimit"), 0 );
        XboxSetClassDefaultPropertyInt( GameClassName, TEXT("GoalTeamScore"), ScoreLimit );
    }
    else
    {
        XboxSetClassDefaultPropertyInt( GameClassName, TEXT("FragLimit"), ScoreLimit );
        XboxSetClassDefaultPropertyInt( GameClassName, TEXT("GoalTeamScore"), 0 );
    }
}

static void XboxMenuBuildMatchRuleOptions( const TCHAR* GameClassName, INT ScoreLimit, INT TimeLimit, INT MinPlayers, INT Skill, TCHAR* Out )
{
    ScoreLimit = Max<INT>( 0, ScoreLimit );
    TimeLimit = Max<INT>( 0, TimeLimit );
    MinPlayers = Max<INT>( 0, MinPlayers );
    Skill = Max<INT>( 0, Skill );

    // LastManStanding.InitGame deliberately disables timed play. Keep that
    // invariant in the travel URL as well as in the post-travel rule handoff.
    if( XboxMenuGameUsesLives(GameClassName) )
        TimeLimit = 0;

    if( XboxMenuGameUsesObjectiveRules( GameClassName ) )
    {
        appSprintf
        (
            Out,
            TEXT("FragLimit=0?GoalTeamScore=0?TimeLimit=%i?MinPlayers=%i?Difficulty=%i"),
            TimeLimit,
            MinPlayers,
            Skill
        );
    }
    else if( XboxMenuGameUsesTeamScore( GameClassName ) )
    {
        appSprintf
        (
            Out,
            TEXT("FragLimit=0?GoalTeamScore=%i?TimeLimit=%i?MinPlayers=%i?Difficulty=%i"),
            ScoreLimit,
            TimeLimit,
            MinPlayers,
            Skill
        );
    }
    else
    {
        appSprintf
        (
            Out,
            TEXT("FragLimit=%i?TimeLimit=%i?MinPlayers=%i?Difficulty=%i"),
            ScoreLimit,
            TimeLimit,
            MinPlayers,
            Skill
        );
    }
}

static void XboxMenuQueuePendingMatchRules( const TCHAR* GameClassName, INT ScoreLimit, INT TimeLimit, INT MinPlayers, INT Skill, INT DesiredBots )
{
    GXboxPendingMatchRules.Active = 1;
    GXboxPendingMatchRules.AppliedLevel = NULL;
    appStrncpy( GXboxPendingMatchRules.GameClass, GameClassName ? GameClassName : TEXT(""), ARRAY_COUNT(GXboxPendingMatchRules.GameClass) );
    GXboxPendingMatchRules.GameClass[ARRAY_COUNT(GXboxPendingMatchRules.GameClass)-1] = 0;
    GXboxPendingMatchRules.ScoreLimit = Max<INT>( 0, ScoreLimit );
    GXboxPendingMatchRules.TimeLimit = XboxMenuGameUsesLives(GameClassName) ? 0 : Max<INT>( 0, TimeLimit );
    GXboxPendingMatchRules.MinPlayers = Max<INT>( 0, MinPlayers );
    GXboxPendingMatchRules.Skill = Max<INT>( 0, Skill );
    GXboxPendingMatchRules.DesiredBots = Max<INT>( 0, DesiredBots );
    GXboxPendingMatchRules.ObjectiveRules = XboxMenuGameUsesObjectiveRules( GameClassName );
    GXboxPendingMatchRules.TeamScore = XboxMenuGameUsesTeamScore( GameClassName );
}

static void XboxMenuAdjustPendingBots( ULevel* Level, UObject* Game, INT DesiredBots )
{
    if( !Level || !Level->GetLevelInfo() || !Game )
        return;

    UClass* BotClass = UObject::StaticLoadClass( APawn::StaticClass(), NULL, TEXT("Botpack.Bot"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
    INT NumBots = XboxGetObjectPropertyInt( Game, TEXT("NumBots"), 0 );
    INT Removed = 0;
    if( BotClass )
    {
        for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn && NumBots > DesiredBots; )
        {
            APawn* Next = Pawn->nextPawn;
            if( Pawn->IsA(BotClass) )
            {
                Level->DestroyActor( Pawn );
                Removed++;
                NumBots--;
            }
            Pawn = Next;
        }
    }
    if( Removed )
    {
        XboxSetObjectPropertyInt( Game, TEXT("NumBots"), NumBots );
        GXboxLog.Write( "XMENU pending rules trimmed bots desired=%d removed=%d remaining=%d", DesiredBots, Removed, NumBots );
    }

    UFunction* AddBot = Game->FindFunction( FName(TEXT("AddBot"), FNAME_Find) );
    if( !AddBot )
        return;

    for( INT Safety=0; Safety<16; Safety++ )
    {
        NumBots = XboxGetObjectPropertyInt( Game, TEXT("NumBots"), 0 );
        if( NumBots >= DesiredBots )
            break;

        BYTE Parms[64];
        appMemzero( Parms, sizeof(Parms) );
        Game->ProcessEvent( AddBot, Parms );
    }
}

static void XboxMenuApplyPendingMatchRules( UXboxViewport* Viewport )
{
    if( !GXboxPendingMatchRules.Active || !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    if( !Level || XboxIsFrontendLevel(Level) || GXboxPendingMatchRules.AppliedLevel == Level )
        return;

    ALevelInfo* Info = Level->GetLevelInfo();
    AGameInfo* GameInfo = Info ? Cast<AGameInfo>(Info->Game) : NULL;
    UObject* Game = GameInfo;
    if( !Game )
        return;

    INT FragLimit = GXboxPendingMatchRules.ScoreLimit;
    INT GoalTeamScore = 0;
    if( GXboxPendingMatchRules.ObjectiveRules )
    {
        FragLimit = 0;
        GoalTeamScore = 0;
    }
    else if( GXboxPendingMatchRules.TeamScore )
    {
        FragLimit = 0;
        GoalTeamScore = GXboxPendingMatchRules.ScoreLimit;
    }

    XboxSetObjectPropertyInt( Game, TEXT("FragLimit"), FragLimit );
    XboxSetObjectPropertyInt( Game, TEXT("GoalTeamScore"), GoalTeamScore );
    XboxSetObjectPropertyInt( Game, TEXT("TimeLimit"), GXboxPendingMatchRules.TimeLimit );
    XboxSetObjectPropertyInt( Game, TEXT("MinPlayers"), GXboxPendingMatchRules.MinPlayers );
    XboxSetObjectPropertyInt( Game, TEXT("InitialBots"), GXboxPendingMatchRules.DesiredBots );
    XboxSetObjectPropertyInt( Game, TEXT("Difficulty"), GXboxPendingMatchRules.Skill );

    INT RemainingTime = GXboxPendingMatchRules.TimeLimit > 0 ? GXboxPendingMatchRules.TimeLimit * 60 : 0;
    XboxSetObjectPropertyInt( Game, TEXT("RemainingTime"), RemainingTime );
    XboxSetObjectPropertyInt( Game, TEXT("RemainingBots"), 0 );

    UObject* GRI = GameInfo->GameReplicationInfo;
    if( GRI )
    {
        XboxSetObjectPropertyInt( GRI, TEXT("FragLimit"), FragLimit );
        XboxSetObjectPropertyInt( GRI, TEXT("GoalTeamScore"), GoalTeamScore );
        XboxSetObjectPropertyInt( GRI, TEXT("TimeLimit"), GXboxPendingMatchRules.TimeLimit );
        XboxSetObjectPropertyInt( GRI, TEXT("RemainingTime"), RemainingTime );
        XboxSetObjectPropertyInt( GRI, TEXT("RemainingMinute"), RemainingTime );
    }

    INT BotsBefore = XboxGetObjectPropertyInt( Game, TEXT("NumBots"), 0 );
    XboxMenuAdjustPendingBots( Level, Game, GXboxPendingMatchRules.DesiredBots );
    INT BotsAfter = XboxGetObjectPropertyInt( Game, TEXT("NumBots"), 0 );
    XboxSetObjectPropertyInt( Game, TEXT("RemainingBots"), Max<INT>( 0, GXboxPendingMatchRules.DesiredBots - BotsAfter ) );

    GXboxPendingMatchRules.AppliedLevel = Level;
    GXboxPendingMatchRules.Active = 0;
    GXboxLog.Write( "XMENU pending rules applied map=%s game=%s queuedGame=%s teamScore=%d objective=%d frag=%d goalTeam=%d time=%d minPlayers=%d skill=%d desiredBots=%d botsBefore=%d botsAfter=%d",
        Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
        Game->GetClass() ? TCHAR_TO_ANSI(Game->GetClass()->GetName()) : "None",
        TCHAR_TO_ANSI(GXboxPendingMatchRules.GameClass),
        GXboxPendingMatchRules.TeamScore ? 1 : 0,
        GXboxPendingMatchRules.ObjectiveRules ? 1 : 0,
        FragLimit,
        GoalTeamScore,
        GXboxPendingMatchRules.TimeLimit,
        GXboxPendingMatchRules.MinPlayers,
        GXboxPendingMatchRules.Skill,
        GXboxPendingMatchRules.DesiredBots,
        BotsBefore,
        BotsAfter );
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
    TCHAR RuleURL[256];
    TCHAR URL[1024];
    UBOOL bTeamScore = XboxMenuGameUsesTeamScore( *Game.URLValue );
    UBOOL bObjective = XboxMenuGameUsesObjectiveRules( *Game.URLValue );
    INT MinPlayers = Bots + 1;
    XboxMenuBuildMutatorURL( MutatorURL, ARRAY_COUNT(MutatorURL) );
    XboxMenuBuildPlayerURL( PlayerURL, ARRAY_COUNT(PlayerURL) );
    XboxMenuApplyMatchRuleDefaults( *Game.URLValue, FragLimit, MinPlayers );
    XboxMenuBuildMatchRuleOptions( *Game.URLValue, FragLimit, TimeLimit, MinPlayers, Skill, RuleURL );
    XboxMenuQueuePendingMatchRules( *Game.URLValue, FragLimit, TimeLimit, MinPlayers, Skill, Bots );
    appSprintf
    (
        URL,
        TEXT("%s?Game=%s?%s%s%s%s"),
        *Map.URLValue,
        *Game.URLValue,
        RuleURL,
        PlayerURL,
        MutatorURL[0] ? TEXT("?Mutator=") : TEXT(""),
        MutatorURL[0] ? MutatorURL : TEXT("")
    );

    XboxMenuClose( Viewport );
    XboxSplitResetRuntime( Client, "InstantAction" );
    GXboxLog.Write( "XMENU Begin Match travel teamScore=%d objective=%d score=%d bots=%d minPlayers=%d: %s",
        bTeamScore ? 1 : 0,
        bObjective ? 1 : 0,
        FragLimit,
        Bots,
        MinPlayers,
        TCHAR_TO_ANSI(URL) );
    Client->Engine->SetClientTravel( Viewport, URL, 0, TRAVEL_Absolute );
}

static void XboxMenuStartSplitScreen( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    XboxSplitResetRuntime( Client, "StartSplitScreen" );
    XboxSplitReadyReset( Viewport, 0 );
    XboxSplitReadyActivatePrimary( Viewport );
    GXboxMenu.Screen = XMS_SplitReady;
    GXboxMenu.SplitFocus = 0;
    GXboxLog.Write( "XMENU screen: Splitscreen Ready" );
}

static void XboxMenuStartSplitMatch( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;
    if( !XboxSplitReadyCanBegin() )
    {
        GXboxLog.Write( "XSPLIT begin blocked joined=%d canBegin=0", XboxSplitReadyJoinedCount() );
        return;
    }

    INT GameTypeCount = XboxMenuGameTypeCount();
    GXboxMenu.InstantGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, GameTypeCount-1 );
    INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
    if( MapCount <= 0 )
    {
        const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
        GXboxLog.Write( "XSPLIT begin blocked: no maps for game=%s prefix=%s",
            TCHAR_TO_ANSI(*Game.URLValue), TCHAR_TO_ANSI(*Game.MapPrefix) );
        return;
    }

    INT MapIndex = Clamp<INT>( GXboxMenu.InstantMap[GXboxMenu.InstantGameType], 0, MapCount-1 );
    const FXboxDiscoveredOption& Map = XboxMenuMap( GXboxMenu.InstantGameType, MapIndex );
    const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
    INT FragLimit = GXboxFragLimits[Clamp<INT>(GXboxMenu.InstantFragLimit, 0, ARRAY_COUNT(GXboxFragLimits)-1)];
    INT TimeLimit = GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
    INT Skill = Clamp<INT>( GXboxMenu.InstantSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );

    XboxMenuApplyMatchRuleDefaults( *Game.URLValue, FragLimit, 0 );

    XboxSplitResetRuntime( Client, "SplitReadyLaunch" );
    GXboxSplitUseReadySlots = 1;
    GXboxSplitPending = 1;

    TCHAR PlayerURL[512];
    TCHAR RuleURL[256];
    TCHAR URL[1024];
    XboxSplitBuildPlayerURLForSlot( 0, PlayerURL, ARRAY_COUNT(PlayerURL), !GXboxSplitReadySlots[0].Joined );
    XboxMenuBuildMatchRuleOptions( *Game.URLValue, FragLimit, TimeLimit, 0, Skill, RuleURL );
    XboxMenuQueuePendingMatchRules( *Game.URLValue, FragLimit, TimeLimit, 0, Skill, 0 );
    appSprintf
    (
        URL,
        TEXT("%s?Game=%s?%s?MaxPlayers=4%s"),
        *Map.URLValue,
        *Game.URLValue,
        RuleURL,
        PlayerURL
    );

    XboxMenuClose( Viewport );
    XboxSplitReadyReleaseControllers();
    GXboxLog.Write( "XSPLIT ready travel joined=%d teamScore=%d objective=%d score=%d map=%s game=%s url=%s",
        XboxSplitReadyJoinedCount(),
        XboxMenuGameUsesTeamScore( *Game.URLValue ) ? 1 : 0,
        XboxMenuGameUsesObjectiveRules( *Game.URLValue ) ? 1 : 0,
        FragLimit,
        TCHAR_TO_ANSI(*Map.URLValue),
        TCHAR_TO_ANSI(*Game.URLValue),
        TCHAR_TO_ANSI(URL) );
    Client->Engine->SetClientTravel( Viewport, URL, 0, TRAVEL_Absolute );
}

static void XboxMenuReturnToFrontend( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    XboxMenuReleaseFrontendTransientAssets( "return frontend", 1 );
    XboxMenuReleaseMatchPause( Viewport );
    XboxSplitResetRuntime( Client, "ReturnToFrontend" );
    GXboxPendingMatchRules.Active = 0;
    GXboxPendingMatchRules.AppliedLevel = NULL;
    XboxMenuEnsureConsoleClass( Viewport, TEXT("Engine.Console"), "ReturnToFrontend" );
    GXboxMenu.Active = 0;
    GXboxMenu.Screen = XMS_Main;
    GXboxMenu.MainFocus = 0;
    GXboxMenu.PauseFocus = 0;
    GXboxPauseReturnConfirm = 0;
    GXboxPauseReturnConfirmFocus = 1;
    GXboxFrontendMenuOpenPending = 1;

    if( Client->Engine->Audio )
    {
        Client->Engine->Audio->Exec( TEXT("XAUDIOSETMENUMODE 0") );
        Client->Engine->Audio->Exec( TEXT("XAUDIOSTOPFX") );
    }

    const TCHAR* FrontendURL = TEXT("CityIntro.unr?Game=Engine.GameInfo?Name=Player?Class=Engine.Spectator?Team=255");
    UViewport* TravelViewport = Client->Viewports.Num() > 0 ? Client->Viewports(0) : Viewport;
    GXboxLog.Write( "XMENU return to frontend queued: viewport=0x%08X first=0x%08X url=%s",
        (DWORD)Viewport,
        (DWORD)TravelViewport,
        TCHAR_TO_ANSI(FrontendURL) );
    Client->Engine->SetClientTravel( TravelViewport, FrontendURL, 0, TRAVEL_Absolute );
}

static const TCHAR* XboxMenuScreenName( EXboxMenuScreen Screen )
{
    switch( Screen )
    {
        case XMS_Pause: return TEXT("Pause");
        case XMS_Main: return TEXT("Main");
        case XMS_InstantAction: return TEXT("InstantAction");
        case XMS_Mutators: return TEXT("Mutators");
        case XMS_SystemLink: return TEXT("SystemLink");
        case XMS_SystemLinkMapSelect: return TEXT("SystemLinkMapSelect");
        case XMS_Tournament: return TEXT("Tournament");
        case XMS_TournamentPostMatch: return TEXT("TournamentPostMatch");
        case XMS_SplitReady: return TEXT("SplitReady");
        case XMS_SplitMapSelect: return TEXT("SplitMapSelect");
        case XMS_ProfileSelect: return TEXT("ProfileSelect");
        case XMS_PlayerSetup: return TEXT("PlayerSetup");
        case XMS_ProfileName: return TEXT("ProfileName");
        case XMS_Controls: return TEXT("Controls");
        case XMS_Settings: return TEXT("Settings");
        case XMS_SettingsAudio: return TEXT("SettingsAudio");
        case XMS_SettingsVideo: return TEXT("SettingsVideo");
        case XMS_ComingSoon: return TEXT("ComingSoon");
    }
    return TEXT("Unknown");
}

static void XboxSoakLogState( UXboxViewport* Viewport, const char* Tag )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    const TCHAR* MapName = (Level && Level->URL.Map.Len()) ? *Level->URL.Map : TEXT("");
    const TCHAR* UrlText = (Level && Level->URL.Map.Len()) ? *Level->URL.String() : TEXT("");
    const TCHAR* TravelText = (Viewport && Viewport->TravelURL.Len()) ? *Viewport->TravelURL : TEXT("");
    INT ViewportCount = 0;
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( Client )
        ViewportCount = Client->Viewports.Num();

    GXboxLog.Write( "XSOAK state tag=%s map=%s url=%s travel=%s menu=%d screen=%s split=%d pending=%d activeMask=0x%X players=%d viewports=%d player=0x%08X class=%s state=%s ready=%d showMenu=%d availKB=%u",
        Tag ? Tag : "",
        TCHAR_TO_ANSI(MapName),
        TCHAR_TO_ANSI(UrlText),
        TCHAR_TO_ANSI(TravelText),
        GXboxMenu.Active ? 1 : 0,
        TCHAR_TO_ANSI(XboxMenuScreenName(GXboxMenu.Screen)),
        GXboxSplitActive ? 1 : 0,
        GXboxSplitPending ? 1 : 0,
        GXboxSplitActiveMask,
        GXboxSplitActivePlayerCount,
        ViewportCount,
        (DWORD)Player,
        Player && Player->GetClass() ? TCHAR_TO_ANSI(Player->GetClass()->GetName()) : "None",
        TCHAR_TO_ANSI(XboxPlayerStateName(Player)),
        Player && Player->bReadyToPlay ? 1 : 0,
        Player && Player->bShowMenu ? 1 : 0,
        (unsigned)XboxMenuAvailPhysKB() );
    XboxMenuLogResourceBuckets( Tag ? Tag : "soak" );
}

static UBOOL XboxSoakSelectMap( const TCHAR* MapFile )
{
    INT GameTypeCount = XboxMenuGameTypeCount();
    for( INT GameType=0; GameType<GameTypeCount; GameType++ )
    {
        INT MapIndex = XboxMenuFindMapIndexByFile( GameType, MapFile );
        if( MapIndex != INDEX_NONE )
        {
            GXboxMenu.InstantGameType = GameType;
            GXboxMenu.InstantMap[GameType] = MapIndex;
            XboxMenuLoadMapsForGameType( GameType );
            const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
            const FXboxDiscoveredOption& Map = XboxMenuMap( GameType, MapIndex );
            GXboxLog.Write( "XSOAK selected map file=%s game=%s map=%s gameType=%d mapIndex=%d",
                TCHAR_TO_ANSI(MapFile),
                TCHAR_TO_ANSI(*Game.URLValue),
                TCHAR_TO_ANSI(*Map.URLValue),
                GameType,
                MapIndex );
            return 1;
        }
    }

    GXboxLog.Write( "XSOAK selected map missing file=%s gameTypes=%d", TCHAR_TO_ANSI(MapFile), GameTypeCount );
    return 0;
}

static void XboxSoakConfigureMatchDefaults()
{
    GXboxMenu.InstantBots = 1;
    GXboxMenu.InstantSkill = 1;
    GXboxMenu.InstantFragLimit = 1;
    GXboxMenu.InstantTimeLimit = 1;
    GXboxMenu.InstantMutatorChoice = 0;
    for( INT i=0; i<ARRAY_COUNT(GXboxMenu.InstantMutatorMask); i++ )
        GXboxMenu.InstantMutatorMask[i] = 0;
}

static void XboxSoakConfigureFourSplitSlots( UXboxViewport* Viewport )
{
    XboxSplitReadyReset( Viewport );
    XboxSplitReadyEnsure();

    for( INT i=0; i<4; i++ )
    {
        if( !XboxSplitEnsureProfileForJoin(Viewport, i, 1) )
            continue;
        GXboxSplitReadySlots[i].Joined = 1;
        GXboxSplitReadySlots[i].Locked = 1;
        GXboxSplitReadySlots[i].Focus = 0;
        const FXboxPlayerClassOption& Player = XboxSplitReadyPlayerClass( i );
        GXboxLog.Write( "XSOAK split slot=%d profile=%d name=%s character=%d label=%s team=%d",
            i,
            GXboxSplitReadySlots[i].Profile,
            TCHAR_TO_ANSI(GXboxProfiles[GXboxSplitReadySlots[i].Profile].Name),
            GXboxSplitReadySlots[i].Character,
            TCHAR_TO_ANSI(*Player.Label),
            GXboxSplitReadySlots[i].Team );
    }
}

static void XboxSoakForceStartMatch( UXboxViewport* Viewport, const char* Reason )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UObject* Game = (Level && Level->GetLevelInfo()) ? Level->GetLevelInfo()->Game : NULL;
    if( !Player || !Game )
        return;

    Player->bReadyToPlay = 1;
    Player->bShowMenu = 0;
    Player->bSpecialMenu = 0;
    XboxSetObjectPropertyText( Game, TEXT("bRequireReady"), TEXT("False") );
    XboxSetObjectPropertyInt( Game, TEXT("CountDown"), 0 );
    UFunction* StartMatch = Game->FindFunction( FName(TEXT("StartMatch"), FNAME_Find) );
    if( StartMatch )
    {
        Game->ProcessEvent( StartMatch, NULL );
        GXboxLog.Write( "XSOAK force start reason=%s map=%s state=%s class=%s game=%s",
            Reason ? Reason : "",
            Level && Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
            TCHAR_TO_ANSI(XboxPlayerStateName(Player)),
            Player->GetClass() ? TCHAR_TO_ANSI(Player->GetClass()->GetName()) : "None",
            Game->GetClass() ? TCHAR_TO_ANSI(Game->GetClass()->GetName()) : "None" );
    }
}

static void XboxSoakStartInstantMap( UXboxViewport* Viewport, const TCHAR* MapFile )
{
    XboxSoakConfigureMatchDefaults();
    if( XboxSoakSelectMap( MapFile ) )
        XboxMenuStartInstantAction( Viewport );
}

static void XboxInstantRulesProofLogGame( UXboxViewport* Viewport, const char* Tag )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UObject* Game = (Level && Level->GetLevelInfo()) ? Level->GetLevelInfo()->Game : NULL;
    const TCHAR* MapName = (Level && Level->URL.Map.Len()) ? *Level->URL.Map : TEXT("");
    const TCHAR* UrlText = (Level && Level->URL.Map.Len()) ? *Level->URL.String() : TEXT("");

    if( !Game )
    {
        GXboxLog.Write( "XINSTANT RULES proof tag=%s map=%s url=%s game=None",
            Tag ? Tag : "",
            TCHAR_TO_ANSI(MapName),
            TCHAR_TO_ANSI(UrlText) );
        return;
    }

    GXboxLog.Write( "XINSTANT RULES proof tag=%s map=%s url=%s game=%s frag=%d goalTeam=%d minPlayers=%d initialBots=%d numBots=%d numPlayers=%d remainingBots=%d timeLimit=%d difficulty=%d ready=%d",
        Tag ? Tag : "",
        TCHAR_TO_ANSI(MapName),
        TCHAR_TO_ANSI(UrlText),
        Game->GetClass() ? TCHAR_TO_ANSI(Game->GetClass()->GetName()) : "None",
        XboxGetObjectPropertyInt( Game, TEXT("FragLimit"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("GoalTeamScore"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("MinPlayers"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("InitialBots"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("NumBots"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("NumPlayers"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("RemainingBots"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("TimeLimit"), -1 ),
        XboxGetObjectPropertyInt( Game, TEXT("Difficulty"), -1 ),
        Player && Player->bReadyToPlay ? 1 : 0 );
}

static const TCHAR* XboxInstantRulesProofRequestedPrefix()
{
    static INT DMCached = -1;
    static INT CTFCached = -1;
    static INT DOMCached = -1;
    static INT ASCached = -1;
    static INT JBCached = -1;
    static INT LMSCached = -1;
    static INT GenericCached = -1;

    if( XboxSmokeMarkerExists( "XboxInstantMenuProof_LMS.ini", LMSCached ) )
        return TEXT("LMS");
    if( XboxSmokeMarkerExists( "XboxInstantMenuProof_DM.ini", DMCached ) )
        return TEXT("DM");
    if( XboxSmokeMarkerExists( "XboxInstantMenuProof_CTF.ini", CTFCached ) )
        return TEXT("CTF");
    if( XboxSmokeMarkerExists( "XboxInstantMenuProof_DOM.ini", DOMCached ) )
        return TEXT("DOM");
    if( XboxSmokeMarkerExists( "XboxInstantMenuProof_AS.ini", ASCached ) )
        return TEXT("AS");
    if( XboxSmokeMarkerExists( "XboxInstantMenuProof_JB.ini", JBCached ) )
        return TEXT("JB");
    if( XboxSmokeMarkerExists( "XboxInstantMenuProofSmoke.ini", GenericCached ) )
        return TEXT("DM");
    return NULL;
}

static INT XboxInstantRulesProofFindGameTypeByPrefix( const TCHAR* Prefix )
{
    INT GameTypeCount = XboxMenuGameTypeCount();
    if( !Prefix || !Prefix[0] )
        return GameTypeCount > 0 ? 0 : INDEX_NONE;

    for( INT i=0; i<GameTypeCount; i++ )
    {
        const FXboxDiscoveredOption& Game = XboxMenuGameType( i );
        if( appStricmp( Prefix, TEXT("LMS") ) == 0
        &&  appStricmp( *Game.URLValue, TEXT("Botpack.LastManStanding") ) == 0 )
            return i;
        if( appStricmp( *Game.MapPrefix, Prefix ) == 0 )
            return i;
    }
    if( appStricmp( Prefix, TEXT("LMS") ) == 0 )
        return INDEX_NONE;
    return GameTypeCount > 0 ? 0 : INDEX_NONE;
}

struct FXboxInstantRulesProofProfile
{
    INT BotIndex;
    INT SkillIndex;
    INT ScoreIndex;
    INT TimeIndex;
    const TCHAR* PreferredMap;
};

static FXboxInstantRulesProofProfile XboxInstantRulesProofProfileForGame( const FXboxDiscoveredOption& Game )
{
    FXboxInstantRulesProofProfile Profile;
    Profile.BotIndex = 2;
    Profile.SkillIndex = 2;
    Profile.ScoreIndex = 1;
    Profile.TimeIndex = 1;
    Profile.PreferredMap = TEXT("");

    if( appStricmp( *Game.URLValue, TEXT("Botpack.LastManStanding") ) == 0 )
    {
        Profile.BotIndex = 2;
        Profile.SkillIndex = 2;
        Profile.ScoreIndex = 1;
        Profile.TimeIndex = 0;
        Profile.PreferredMap = TEXT("DM-Deck16][.unr");
    }
    else if( appStricmp( *Game.MapPrefix, TEXT("DM") ) == 0 )
    {
        Profile.BotIndex = 2;
        Profile.SkillIndex = 2;
        Profile.ScoreIndex = 1;
        Profile.TimeIndex = 1;
        Profile.PreferredMap = TEXT("DM-Deck16][.unr");
    }
    else if( appStricmp( *Game.MapPrefix, TEXT("CTF") ) == 0 )
    {
        Profile.BotIndex = 3;
        Profile.SkillIndex = 3;
        Profile.ScoreIndex = 1;
        Profile.TimeIndex = 1;
        Profile.PreferredMap = TEXT("CTF-Face][.unr");
    }
    else if( appStricmp( *Game.MapPrefix, TEXT("DOM") ) == 0 )
    {
        Profile.BotIndex = 2;
        Profile.SkillIndex = 0;
        Profile.ScoreIndex = 1;
        Profile.TimeIndex = 1;
        Profile.PreferredMap = TEXT("DOM-Sesmar.unr");
    }
    else if( appStricmp( *Game.MapPrefix, TEXT("AS") ) == 0 )
    {
        Profile.BotIndex = 1;
        Profile.SkillIndex = 1;
        Profile.ScoreIndex = 1;
        Profile.TimeIndex = 1;
        Profile.PreferredMap = TEXT("AS-Guardia.unr");
    }
    else if( appStricmp( *Game.MapPrefix, TEXT("JB") ) == 0 )
    {
        Profile.BotIndex = 2;
        Profile.SkillIndex = 2;
        Profile.ScoreIndex = 1;
        Profile.TimeIndex = 1;
        Profile.PreferredMap = TEXT("JB-Pharynx.unr");
    }

    Profile.BotIndex = Clamp<INT>( Profile.BotIndex, 0, ARRAY_COUNT(GXboxBotCounts)-1 );
    Profile.SkillIndex = Clamp<INT>( Profile.SkillIndex, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );
    Profile.ScoreIndex = Clamp<INT>( Profile.ScoreIndex, 0, ARRAY_COUNT(GXboxFragLimits)-1 );
    Profile.TimeIndex = Clamp<INT>( Profile.TimeIndex, 0, ARRAY_COUNT(GXboxTimeLimits)-1 );
    return Profile;
}

static INT XboxInstantRulesProofCurrentMenuIndex( INT Row )
{
    switch( Row )
    {
        case 0: return GXboxMenu.InstantGameType;
        case 1: return GXboxMenu.InstantMap[Clamp<INT>(GXboxMenu.InstantGameType, 0, 63)];
        case 2: return GXboxMenu.InstantBots;
        case 3: return GXboxMenu.InstantSkill;
        case 4: return GXboxMenu.InstantFragLimit;
        case 5: return GXboxMenu.InstantTimeLimit;
    }
    return 0;
}

static INT XboxInstantRulesProofMenuRowCount( INT Row )
{
    switch( Row )
    {
        case 0: return XboxMenuGameTypeCount();
        case 1: return XboxInstantMapList( Clamp<INT>(GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1) );
        case 2: return ARRAY_COUNT(GXboxBotCounts);
        case 3: return ARRAY_COUNT(GXboxSkillLabels);
        case 4: return ARRAY_COUNT(GXboxFragLimits);
        case 5: return ARRAY_COUNT(GXboxTimeLimits);
    }
    return 0;
}

static void XboxInstantRulesProofAdjustMenuRowToIndex( INT Row, INT TargetIndex )
{
    INT Count = XboxInstantRulesProofMenuRowCount( Row );
    if( Count <= 0 )
        return;

    GXboxMenu.Screen = XMS_InstantAction;
    GXboxMenu.InstantFocus = Row;
    TargetIndex = Clamp<INT>( TargetIndex, 0, Count-1 );
    INT CurrentIndex = Clamp<INT>( XboxInstantRulesProofCurrentMenuIndex(Row), 0, Count-1 );
    INT Forward = (TargetIndex - CurrentIndex + Count) % Count;
    INT Backward = (CurrentIndex - TargetIndex + Count) % Count;
    INT Steps = Min<INT>( Forward, Backward );
    INT Delta = (Forward <= Backward) ? 1 : -1;

    for( INT i=0; i<Steps; i++ )
        XboxMenuAdjustInstantAction( Delta );

    GXboxLog.Write( "XINSTANT MENU adjusted row=%d target=%d final=%d count=%d steps=%d delta=%d",
        Row,
        TargetIndex,
        XboxInstantRulesProofCurrentMenuIndex(Row),
        Count,
        Steps,
        Steps ? Delta : 0 );
}

static UBOOL XboxInstantRulesProofPrepareBeforeSelection( INT GameType )
{
    INT GameTypeCount = XboxMenuGameTypeCount();
    if( GameType < 0 || GameType >= GameTypeCount )
        return 0;

    XboxSoakConfigureMatchDefaults();
    GXboxMenu.Active = 1;
    GXboxMenu.Screen = XMS_InstantAction;
    GXboxMenu.InstantFocus = 0;
    GXboxMenu.PausedMatch = 0;

    XboxInstantRulesProofAdjustMenuRowToIndex( 0, GameType );

    const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
    FXboxInstantRulesProofProfile Profile = XboxInstantRulesProofProfileForGame( Game );
    INT MapCount = XboxInstantMapList( GameType );
    if( MapCount <= 0 )
        return 0;

    INT MapIndex = INDEX_NONE;
    if( Profile.PreferredMap && Profile.PreferredMap[0] )
        MapIndex = XboxMenuFindMapIndexByFile( GameType, Profile.PreferredMap );
    if( MapIndex == INDEX_NONE )
        MapIndex = 0;

    XboxInstantRulesProofAdjustMenuRowToIndex( 1, MapIndex );
    XboxInstantRulesProofAdjustMenuRowToIndex( 2, 0 );
    XboxInstantRulesProofAdjustMenuRowToIndex( 3, 0 );
    XboxInstantRulesProofAdjustMenuRowToIndex( 4, 0 );
    XboxInstantRulesProofAdjustMenuRowToIndex( 5, 0 );

    GXboxMenu.InstantFocus = 2;

    const FXboxDiscoveredOption& SelectedMap = XboxMenuMap( GameType, GXboxMenu.InstantMap[GameType] );
    UBOOL bObjective = XboxMenuGameUsesObjectiveRules( *Game.URLValue );
    UBOOL bTeamScore = XboxMenuGameUsesTeamScore( *Game.URLValue );
    GXboxLog.Write( "XINSTANT MENU BEFORE index=%d game=%s prefix=%s map=%s mapIndex=%d botsIndex=%d bots=%d skillIndex=%d skillLabel=%s scoreIndex=%d score=%d timeIndex=%d time=%d minPlayers=%d mode=%s focus=%d active=%d",
        GameType,
        TCHAR_TO_ANSI(*Game.URLValue),
        TCHAR_TO_ANSI(*Game.MapPrefix),
        TCHAR_TO_ANSI(*SelectedMap.URLValue),
        GXboxMenu.InstantMap[GameType],
        GXboxMenu.InstantBots,
        GXboxBotCounts[Clamp<INT>(GXboxMenu.InstantBots, 0, ARRAY_COUNT(GXboxBotCounts)-1)],
        GXboxMenu.InstantSkill,
        TCHAR_TO_ANSI(GXboxSkillLabels[Clamp<INT>(GXboxMenu.InstantSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1)]),
        GXboxMenu.InstantFragLimit,
        GXboxFragLimits[Clamp<INT>(GXboxMenu.InstantFragLimit, 0, ARRAY_COUNT(GXboxFragLimits)-1)],
        GXboxMenu.InstantTimeLimit,
        GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)],
        GXboxBotCounts[Clamp<INT>(GXboxMenu.InstantBots, 0, ARRAY_COUNT(GXboxBotCounts)-1)] + 1,
        bObjective ? "objective" : (bTeamScore ? "teamScore" : "frag"),
        GXboxMenu.InstantFocus,
        GXboxMenu.Active ? 1 : 0 );
    return 1;
}

static UBOOL XboxInstantRulesProofPrepareMenuSelection( INT GameType )
{
    INT GameTypeCount = XboxMenuGameTypeCount();
    if( GameType < 0 || GameType >= GameTypeCount )
        return 0;

    GXboxMenu.Active = 1;
    GXboxMenu.Screen = XMS_InstantAction;
    GXboxMenu.InstantFocus = 0;
    GXboxMenu.PausedMatch = 0;

    XboxInstantRulesProofAdjustMenuRowToIndex( 0, GameType );

    const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
    FXboxInstantRulesProofProfile Profile = XboxInstantRulesProofProfileForGame( Game );
    INT MapCount = XboxInstantMapList( GameType );
    if( MapCount <= 0 )
        return 0;

    INT MapIndex = INDEX_NONE;
    if( Profile.PreferredMap && Profile.PreferredMap[0] )
        MapIndex = XboxMenuFindMapIndexByFile( GameType, Profile.PreferredMap );
    if( MapIndex == INDEX_NONE )
        MapIndex = 0;

    XboxInstantRulesProofAdjustMenuRowToIndex( 1, MapIndex );
    XboxInstantRulesProofAdjustMenuRowToIndex( 2, Profile.BotIndex );
    XboxInstantRulesProofAdjustMenuRowToIndex( 3, Profile.SkillIndex );
    XboxInstantRulesProofAdjustMenuRowToIndex( 4, Profile.ScoreIndex );
    XboxInstantRulesProofAdjustMenuRowToIndex( 5, Profile.TimeIndex );

    GXboxMenu.InstantFocus = 7;

    const FXboxDiscoveredOption& SelectedMap = XboxMenuMap( GameType, GXboxMenu.InstantMap[GameType] );
    INT DesiredBots = GXboxBotCounts[Clamp<INT>(GXboxMenu.InstantBots, 0, ARRAY_COUNT(GXboxBotCounts)-1)];
    INT ScoreLimit = GXboxFragLimits[Clamp<INT>(GXboxMenu.InstantFragLimit, 0, ARRAY_COUNT(GXboxFragLimits)-1)];
    INT TimeLimit = GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
    INT Skill = Clamp<INT>( GXboxMenu.InstantSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1 );
    UBOOL bObjective = XboxMenuGameUsesObjectiveRules( *Game.URLValue );
    UBOOL bTeamScore = XboxMenuGameUsesTeamScore( *Game.URLValue );

    GXboxLog.Write( "XINSTANT MENU AFTER index=%d game=%s prefix=%s map=%s mapIndex=%d botsIndex=%d bots=%d skillIndex=%d skillLabel=%s scoreIndex=%d score=%d timeIndex=%d time=%d minPlayers=%d mode=%s focus=%d active=%d",
        GameType,
        TCHAR_TO_ANSI(*Game.URLValue),
        TCHAR_TO_ANSI(*Game.MapPrefix),
        TCHAR_TO_ANSI(*SelectedMap.URLValue),
        GXboxMenu.InstantMap[GameType],
        GXboxMenu.InstantBots,
        DesiredBots,
        Skill,
        TCHAR_TO_ANSI(GXboxSkillLabels[Skill]),
        GXboxMenu.InstantFragLimit,
        ScoreLimit,
        GXboxMenu.InstantTimeLimit,
        TimeLimit,
        DesiredBots + 1,
        bObjective ? "objective" : (bTeamScore ? "teamScore" : "frag"),
        GXboxMenu.InstantFocus,
        GXboxMenu.Active ? 1 : 0 );
    return 1;
}

static UBOOL XboxInstantRulesProofEvaluate( UXboxViewport* Viewport, INT GameType )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UObject* GameObject = (Level && Level->GetLevelInfo()) ? Level->GetLevelInfo()->Game : NULL;
    if( !GameObject )
        return 0;

    const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
    FXboxInstantRulesProofProfile Profile = XboxInstantRulesProofProfileForGame( Game );
    INT SelectedMapIndex = Clamp<INT>( GXboxMenu.InstantMap[GameType], 0, Max<INT>(1, XboxInstantMapList(GameType))-1 );
    const FXboxDiscoveredOption& SelectedMap = XboxMenuMap( GameType, SelectedMapIndex );
    INT ScoreLimit = GXboxFragLimits[Profile.ScoreIndex];
    INT TimeLimit = GXboxTimeLimits[Profile.TimeIndex];
    INT DesiredBots = GXboxBotCounts[Profile.BotIndex];
    INT MinPlayers = DesiredBots + 1;
    INT Skill = Profile.SkillIndex;
    UBOOL bObjective = XboxMenuGameUsesObjectiveRules( *Game.URLValue );
    UBOOL bTeamScore = XboxMenuGameUsesTeamScore( *Game.URLValue );
    UBOOL bLives = XboxMenuGameUsesLives( *Game.URLValue );
    UClass* ExpectedGameClass = XboxMenuLoadGameClass( *Game.URLValue );
    INT ExpectedFrag = bTeamScore || bObjective ? 0 : ScoreLimit;
    INT ExpectedGoal = bObjective ? 0 : (bTeamScore ? ScoreLimit : -9999);
    INT Frag = XboxGetObjectPropertyInt( GameObject, TEXT("FragLimit"), -9999 );
    INT Goal = XboxGetObjectPropertyInt( GameObject, TEXT("GoalTeamScore"), -9999 );
    INT ActualMinPlayers = XboxGetObjectPropertyInt( GameObject, TEXT("MinPlayers"), -9999 );
    INT InitialBots = XboxGetObjectPropertyInt( GameObject, TEXT("InitialBots"), -9999 );
    INT NumBots = XboxGetObjectPropertyInt( GameObject, TEXT("NumBots"), -9999 );
    INT ActualTime = XboxGetObjectPropertyInt( GameObject, TEXT("TimeLimit"), -9999 );
    INT ActualSkill = XboxGetObjectPropertyInt( GameObject, TEXT("Difficulty"), -9999 );
    INT ActualLives = XboxGetObjectPropertyInt( GameObject, TEXT("Lives"), -9999 );
    FString ScoreBoardType;
    XboxGetObjectPropertyString( GameObject, TEXT("ScoreBoardType"), ScoreBoardType );
    const TCHAR* UrlSkillText = Level ? Level->URL.GetOption( TEXT("Difficulty="), NULL ) : NULL;
    INT UrlSkill = UrlSkillText ? appAtoi( UrlSkillText ) : -9999;
    UBOOL bMapMatches = Level && Level->URL.Map.Len() && appStricmp( *Level->URL.Map, *SelectedMap.URLValue ) == 0;
    UBOOL bPass =
        bMapMatches &&
        ExpectedGameClass && GameObject->IsA(ExpectedGameClass) &&
        Frag == ExpectedFrag &&
        ( ExpectedGoal == -9999 || Goal == ExpectedGoal ) &&
        ActualMinPlayers == MinPlayers &&
        InitialBots == DesiredBots &&
        NumBots == DesiredBots &&
        ActualTime == (bLives ? 0 : TimeLimit) &&
        (!bLives || ActualLives == ScoreLimit) &&
        ActualSkill == Skill &&
        UrlSkill == Skill;

    GXboxLog.Write( "XINSTANT MENU LIVE index=%d status=%s game=%s prefix=%s selectedMap=%s liveMap=%s mapMatch=%d liveGame=%s classMatch=%d mode=%s selectedScore=%d frag=%d expectedFrag=%d lives=%d expectedLives=%d scoreboard=%s goalTeam=%d expectedGoal=%d selectedBots=%d minPlayers=%d expectedMin=%d initialBots=%d expectedInitialBots=%d numBots=%d desiredBots=%d selectedSkill=%d gameDifficulty=%d urlDifficulty=%d selectedTime=%d timeLimit=%d expectedTime=%d",
        GameType,
        bPass ? "PASS" : "FAIL",
        TCHAR_TO_ANSI(*Game.URLValue),
        TCHAR_TO_ANSI(*Game.MapPrefix),
        TCHAR_TO_ANSI(*SelectedMap.URLValue),
        Level && Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
        bMapMatches ? 1 : 0,
        GameObject->GetClass() ? TCHAR_TO_ANSI(GameObject->GetClass()->GetName()) : "None",
        ExpectedGameClass && GameObject->IsA(ExpectedGameClass) ? 1 : 0,
        bLives ? "lives" : (bObjective ? "objective" : (bTeamScore ? "teamScore" : "frag")),
        ScoreLimit,
        Frag,
        ExpectedFrag,
        ActualLives,
        bLives ? ScoreLimit : -9999,
        ScoreBoardType.Len() ? TCHAR_TO_ANSI(*ScoreBoardType) : "",
        Goal,
        ExpectedGoal,
        DesiredBots,
        ActualMinPlayers,
        MinPlayers,
        InitialBots,
        DesiredBots,
        NumBots,
        DesiredBots,
        Skill,
        ActualSkill,
        UrlSkill,
        TimeLimit,
        ActualTime,
        bLives ? 0 : TimeLimit );
    return bPass;
}

static void XboxInstantRulesProofBuildScoreboard( ULevel* Level, TCHAR* Out, INT OutCount )
{
    if( !Out || OutCount <= 0 )
        return;

    Out[0] = 0;
    if( !Level || !Level->GetLevelInfo() )
        return;

    for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; Pawn = Pawn->nextPawn )
    {
        APlayerReplicationInfo* PRI = Pawn->PlayerReplicationInfo;
        if( !PRI || PRI->bIsSpectator )
            continue;

        TCHAR Entry[96];
        const TCHAR* Name = PRI->PlayerName.Len() ? *PRI->PlayerName : TEXT("Player");
        appSprintf( Entry, TEXT("%s%s:T%d:%s:S%.1f:D%.1f"),
            Out[0] ? TEXT(";") : TEXT(""),
            Name,
            PRI->Team,
            PRI->bIsABot ? TEXT("bot") : TEXT("human"),
            PRI->Score,
            PRI->Deaths );
        appStrncat( Out, Entry, OutCount );
    }
}

static UBOOL XboxInstantRulesProofIsGameEnded( UXboxViewport* Viewport )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    AGameInfo* Game = (Level && Level->GetLevelInfo()) ? Cast<AGameInfo>(Level->GetLevelInfo()->Game) : NULL;
    if( Game && Game->bGameEnded )
        return 1;

    const TCHAR* StateName = XboxPlayerStateName( Player );
    if( StateName && appStricmp( StateName, TEXT("GameEnded") ) == 0 )
        return 1;

    FString bGameEnded;
    if( XboxGetObjectPropertyString( Game, TEXT("bGameEnded"), bGameEnded )
    && (appStricmp( *bGameEnded, TEXT("True") ) == 0 || appStricmp( *bGameEnded, TEXT("1") ) == 0) )
        return 1;

    return 0;
}

static void XboxInstantRulesProofDriveGameplay( UXboxViewport* Viewport, DOUBLE Now, UBOOL& FireDown, DOUBLE& FireDownTime, DOUBLE& LastFirePulse, UBOOL& MovementLogged )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    if( !Client || !Client->Engine || !Player || GXboxMenu.Active )
        return;

    const TCHAR* StateName = XboxPlayerStateName( Player );
    if( Player->Health > 0 && !Player->bHidden && appStricmp( StateName, TEXT("PlayerWalking") ) == 0 )
    {
        FLOAT MoveScale = Client ? Max<FLOAT>( Client->ScaleXYZ, 100.0f ) : 100.0f;
        FLOAT LookScale = Client ? Max<FLOAT>( Client->ScaleRUV, 100.0f ) : 100.0f;
        FLOAT StrafeSign = appSin( Now * 1.4 ) >= 0.0 ? 1.0f : -1.0f;
        Player->aForward += MoveScale;
        Player->aStrafe  += MoveScale * 0.45f * StrafeSign;
        Player->aTurn    += LookScale * 2.2f;

        if( !MovementLogged )
        {
            MovementLogged = 1;
            GXboxLog.Write( "XINSTANT MENU gameplay drive active state=%s", TCHAR_TO_ANSI(StateName) );
        }

        if( !FireDown && (Now - LastFirePulse) > 0.45 )
        {
            Client->Engine->InputEvent( Viewport, IK_LeftMouse, IST_Press, 0.0f );
            FireDown = 1;
            FireDownTime = Now;
            LastFirePulse = Now;
        }
    }

    if( FireDown && (Now - FireDownTime) > 0.12 )
    {
        Client->Engine->InputEvent( Viewport, IK_LeftMouse, IST_Release, 0.0f );
        FireDown = 0;
    }
}

static UBOOL XboxInstantRulesProofLogEnd( UXboxViewport* Viewport, INT GameType, UBOOL bLivePass, const char* Reason )
{
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    AGameInfo* GameInfo = (Level && Level->GetLevelInfo()) ? Cast<AGameInfo>(Level->GetLevelInfo()->Game) : NULL;
    UObject* GameObject = GameInfo;
    AGameReplicationInfo* GRI = GameInfo ? GameInfo->GameReplicationInfo : NULL;
    if( !GameObject )
        return 0;

    const FXboxDiscoveredOption& Game = XboxMenuGameType( GameType );
    FXboxInstantRulesProofProfile Profile = XboxInstantRulesProofProfileForGame( Game );
    INT SelectedMapIndex = Clamp<INT>( GXboxMenu.InstantMap[GameType], 0, Max<INT>(1, XboxInstantMapList(GameType))-1 );
    const FXboxDiscoveredOption& SelectedMap = XboxMenuMap( GameType, SelectedMapIndex );
    INT ScoreLimit = GXboxFragLimits[Profile.ScoreIndex];
    INT TimeLimit = GXboxTimeLimits[Profile.TimeIndex];
    INT DesiredBots = GXboxBotCounts[Profile.BotIndex];
    TCHAR Scores[512];
    XboxInstantRulesProofBuildScoreboard( Level, Scores, ARRAY_COUNT(Scores) );

    FString EndedText;
    XboxGetObjectPropertyString( GameObject, TEXT("bGameEnded"), EndedText );
    UBOOL bEnded = XboxInstantRulesProofIsGameEnded( Viewport );
    UBOOL bPass = bLivePass && bEnded;

    GXboxLog.Write( "XINSTANT MENU END index=%d status=%s reason=%s game=%s prefix=%s selectedMap=%s liveMap=%s ended=%d bGameEnded=%s state=%s selectedScore=%d frag=%d goalTeam=%d selectedBots=%d numBots=%d selectedSkill=%d gameDifficulty=%d selectedTime=%d timeLimit=%d remainingTime=%d elapsedTime=%d scores=%s",
        GameType,
        bPass ? "PASS" : "FAIL",
        Reason ? Reason : "",
        TCHAR_TO_ANSI(*Game.URLValue),
        TCHAR_TO_ANSI(*Game.MapPrefix),
        TCHAR_TO_ANSI(*SelectedMap.URLValue),
        Level && Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
        bEnded ? 1 : 0,
        EndedText.Len() ? TCHAR_TO_ANSI(*EndedText) : "",
        TCHAR_TO_ANSI(XboxPlayerStateName(Player)),
        ScoreLimit,
        XboxGetObjectPropertyInt( GameObject, TEXT("FragLimit"), -9999 ),
        XboxGetObjectPropertyInt( GameObject, TEXT("GoalTeamScore"), -9999 ),
        DesiredBots,
        XboxGetObjectPropertyInt( GameObject, TEXT("NumBots"), -9999 ),
        Profile.SkillIndex,
        XboxGetObjectPropertyInt( GameObject, TEXT("Difficulty"), -9999 ),
        TimeLimit,
        XboxGetObjectPropertyInt( GameObject, TEXT("TimeLimit"), -9999 ),
        GRI ? GRI->RemainingTime : -9999,
        GRI ? GRI->ElapsedTime : -9999,
        TCHAR_TO_ANSI(Scores) );
    return bPass;
}

static void XboxInstantRulesProofSmokeTick( UXboxViewport* Viewport )
{
    static INT RulesStage = 0;
    static INT GameTypeIndex = INDEX_NONE;
    static DOUBLE StageStartTime = 0.0;
    static DOUBLE LastLogTime = 0.0;
    static UBOOL ReadySent = 0;
    static UBOOL LivePass = 0;
    static UBOOL FireDown = 0;
    static DOUBLE FireDownTime = 0.0;
    static DOUBLE LastFirePulse = 0.0;
    static UBOOL MovementLogged = 0;
    static UBOOL CompleteLogged = 0;

    if( !XboxInstantRulesProofSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UBOOL bFrontend = XboxIsFrontendLevel( Level );
    UBOOL bLivesProof = GameTypeIndex >= 0 && GameTypeIndex < XboxMenuGameTypeCount()
        && appStricmp( *XboxMenuGameType(GameTypeIndex).URLValue, TEXT("Botpack.LastManStanding") ) == 0;
    DOUBLE Now = appSeconds();
    FLOAT StageElapsed = StageStartTime > 0.0 ? (FLOAT)(Now - StageStartTime) : 0.0f;

    if( RulesStage == 0 )
    {
        const TCHAR* Prefix = XboxInstantRulesProofRequestedPrefix();
        GameTypeIndex = XboxInstantRulesProofFindGameTypeByPrefix( Prefix );
        StageStartTime = Now;
        LastLogTime = 0.0;
        ReadySent = 0;
        LivePass = 0;
        FireDown = 0;
        FireDownTime = 0.0;
        LastFirePulse = 0.0;
        MovementLogged = 0;
        CompleteLogged = 0;
        RulesStage = 1;
        GXboxLog.Write( "XINSTANT MENU START targetPrefix=%s targetIndex=%d proof=single-gametype-menu-driven",
            Prefix ? TCHAR_TO_ANSI(Prefix) : "",
            GameTypeIndex );
        XboxSoakLogState( Viewport, "instant-menu-start" );
        return;
    }

    if( Now - LastLogTime >= 3.0 )
    {
        LastLogTime = Now;
        XboxSoakLogState( Viewport, "instant-menu-heartbeat" );
        XboxInstantRulesProofLogGame( Viewport, "heartbeat" );
    }

    if( RulesStage == 1 && bFrontend && StageElapsed >= 1.0f )
    {
        if( GameTypeIndex == INDEX_NONE )
        {
            GXboxLog.Write( "XINSTANT MENU COMPLETE status=FAIL reason=no-target" );
            CompleteLogged = 1;
            RulesStage = 99;
            return;
        }

        if( XboxInstantRulesProofPrepareBeforeSelection( GameTypeIndex ) )
        {
            StageStartTime = Now;
            RulesStage = 2;
        }
        else
        {
            const FXboxDiscoveredOption& Game = XboxMenuGameType( GameTypeIndex );
            GXboxLog.Write( "XINSTANT MENU COMPLETE index=%d status=FAIL game=%s prefix=%s reason=no-map-before",
                GameTypeIndex,
                TCHAR_TO_ANSI(*Game.URLValue),
                TCHAR_TO_ANSI(*Game.MapPrefix) );
            CompleteLogged = 1;
            StageStartTime = Now;
            RulesStage = 99;
        }
        return;
    }

    if( RulesStage == 2 && bFrontend && StageElapsed >= 12.0f )
    {
        if( XboxInstantRulesProofPrepareMenuSelection( GameTypeIndex ) )
        {
            StageStartTime = Now;
            RulesStage = 3;
        }
        else
        {
            const FXboxDiscoveredOption& Game = XboxMenuGameType( GameTypeIndex );
            GXboxLog.Write( "XINSTANT MENU COMPLETE index=%d status=FAIL game=%s prefix=%s reason=no-map-after",
                GameTypeIndex,
                TCHAR_TO_ANSI(*Game.URLValue),
                TCHAR_TO_ANSI(*Game.MapPrefix) );
            CompleteLogged = 1;
            StageStartTime = Now;
            RulesStage = 99;
        }
        return;
    }

    if( RulesStage == 3 && bFrontend && StageElapsed >= 12.0f )
    {
        GXboxMenu.Active = 1;
        GXboxMenu.Screen = XMS_InstantAction;
        GXboxMenu.InstantFocus = 7;
        GXboxLog.Write( "XINSTANT MENU LAUNCH index=%d via=activate focus=%d", GameTypeIndex, GXboxMenu.InstantFocus );
        XboxMenuActivate( Viewport );
        StageStartTime = Now;
        ReadySent = 0;
        LivePass = 0;
        MovementLogged = 0;
        FireDown = 0;
        FireDownTime = 0.0;
        LastFirePulse = 0.0;
        RulesStage = 4;
        return;
    }

    if( RulesStage == 4 && !bFrontend )
    {
        if( bLivesProof )
            Player->bShowScores = 1;
        if( !ReadySent && StageElapsed >= 3.0f && Player && !Player->bReadyToPlay )
        {
            XboxSoakForceStartMatch( Viewport, "instant-menu-ready" );
            ReadySent = 1;
        }
        XboxInstantRulesProofDriveGameplay( Viewport, Now, FireDown, FireDownTime, LastFirePulse, MovementLogged );
        if( StageElapsed >= 8.0f )
        {
            XboxInstantRulesProofLogGame( Viewport, "loaded" );
            LivePass = XboxInstantRulesProofEvaluate( Viewport, GameTypeIndex );
            StageStartTime = Now;
            RulesStage = 5;
        }
        return;
    }

    if( RulesStage == 5 && !bFrontend )
    {
        if( bLivesProof )
            Player->bShowScores = 1;
        XboxInstantRulesProofDriveGameplay( Viewport, Now, FireDown, FireDownTime, LastFirePulse, MovementLogged );
        if( XboxInstantRulesProofIsGameEnded( Viewport ) )
        {
            XboxInstantRulesProofLogGame( Viewport, "ended" );
            UBOOL bEndPass = XboxInstantRulesProofLogEnd( Viewport, GameTypeIndex, LivePass, "game-ended" );
            GXboxLog.Write( "XINSTANT MENU COMPLETE index=%d status=%s livePass=%d endPass=%d",
                GameTypeIndex,
                (LivePass && bEndPass) ? "PASS" : "FAIL",
                LivePass ? 1 : 0,
                bEndPass ? 1 : 0 );
            CompleteLogged = 1;
            StageStartTime = Now;
            RulesStage = 99;
        }
        else if( StageElapsed >= 390.0f )
        {
            XboxInstantRulesProofLogGame( Viewport, "end-timeout" );
            XboxInstantRulesProofLogEnd( Viewport, GameTypeIndex, LivePass, "timeout-waiting-for-end" );
            GXboxLog.Write( "XINSTANT MENU COMPLETE index=%d status=FAIL livePass=%d endPass=0 reason=timeout-waiting-for-end",
                GameTypeIndex,
                LivePass ? 1 : 0 );
            CompleteLogged = 1;
            StageStartTime = Now;
            RulesStage = 99;
        }
        return;
    }

    if( RulesStage == 99 && !CompleteLogged )
    {
        GXboxLog.Write( "XINSTANT MENU COMPLETE index=%d status=FAIL reason=unexpected-complete", GameTypeIndex );
        CompleteLogged = 1;
    }
    else if( RulesStage == 99 && StageElapsed >= 20.0f )
    {
        if( !bFrontend )
        {
            XboxMenuReturnToFrontend( Viewport );
            StageStartTime = Now;
        }
    }
}

static void XboxSoakStartSplitMap( UXboxViewport* Viewport, const TCHAR* MapFile )
{
    XboxSoakConfigureMatchDefaults();
    if( !XboxSoakSelectMap( MapFile ) )
        return;
    XboxSoakConfigureFourSplitSlots( Viewport );
    XboxMenuStartSplitMatch( Viewport );
}

static void XboxIssueMapSmokeTick( UXboxViewport* Viewport )
{
    static INT IssueStage = 0;
    static INT IssueMapIndex = 0;
    static DOUBLE StageStartTime = 0.0;
    static DOUBLE LastLogTime = 0.0;
    static const TCHAR* IssueMaps[] =
    {
        TEXT("DM-Pantheon.unr"),
        TEXT("DM-Halberd.unr"),
        TEXT("DM-Hood.unr"),
        TEXT("DM-CanyonFear.unr")
    };

    if( !XboxIssueMapSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UBOOL bFrontend = XboxIsFrontendLevel( Level );
    DOUBLE Now = appSeconds();
    FLOAT StageElapsed = StageStartTime > 0.0 ? (FLOAT)(Now - StageStartTime) : 0.0f;

    if( IssueStage == 0 )
    {
        StageStartTime = Now;
        LastLogTime = 0.0;
        IssueMapIndex = 0;
        IssueStage = 1;
        GXboxLog.Write( "XISSUE START maps=DM-Pantheon,DM-Halberd,DM-Hood,DM-CanyonFear" );
        XboxSoakLogState( Viewport, "issue-start" );
        return;
    }

    if( Now - LastLogTime >= 4.0 )
    {
        LastLogTime = Now;
        XboxSoakLogState( Viewport, "issue-heartbeat" );
    }

    if( IssueMapIndex >= ARRAY_COUNT(IssueMaps) )
    {
        if( IssueStage != 9 )
        {
            GXboxLog.Write( "XISSUE DONE maps=%d", IssueMapIndex );
            XboxSoakLogState( Viewport, "issue-done" );
            GXboxLog.Flush();
            IssueStage = 9;
        }
        return;
    }

    if( IssueStage == 1 && bFrontend && StageElapsed >= 1.5f )
    {
        const TCHAR* MapFile = IssueMaps[IssueMapIndex];
        GXboxLog.Write( "XISSUE travel mapIndex=%d map=%s", IssueMapIndex, TCHAR_TO_ANSI(MapFile) );
        XboxSoakStartInstantMap( Viewport, MapFile );
        StageStartTime = Now;
        IssueStage = 2;
        return;
    }

    if( IssueStage == 2 )
    {
        if( !bFrontend && StageElapsed >= 3.0f && Player && !Player->bReadyToPlay )
            XboxSoakForceStartMatch( Viewport, "issue-ready" );
        if( StageElapsed >= 9.0f )
        {
            XboxSoakLogState( Viewport, "issue-map-hold-complete" );
            XboxMenuReturnToFrontend( Viewport );
            IssueMapIndex++;
            StageStartTime = Now;
            IssueStage = 3;
        }
        return;
    }

    if( IssueStage == 3 && bFrontend && StageElapsed >= 1.5f )
    {
        StageStartTime = Now;
        IssueStage = 1;
        return;
    }
}

static void XboxSafeAreaProofSmokeTick( UXboxViewport* Viewport )
{
    static INT SafeStage = 0;
    static DOUBLE StageStartTime = 0.0;
    static DOUBLE LastLogTime = 0.0;
    static UBOOL ReadySent = 0;

    if( !XboxSafeAreaProofSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UBOOL bFrontend = XboxIsFrontendLevel( Level );
    DOUBLE Now = appSeconds();
    FLOAT StageElapsed = StageStartTime > 0.0 ? (FLOAT)(Now - StageStartTime) : 0.0f;
    UXboxClient* Client = Cast<UXboxClient>( Viewport->GetOuter() );
    if( Client )
    {
        Client->SafeAreaSize = 92;
        Client->SafeAreaX = 0;
        Client->SafeAreaY = 0;
    }

    if( SafeStage == 0 )
    {
        StageStartTime = Now;
        LastLogTime = 0.0;
        ReadySent = 0;
        SafeStage = 1;
        GXboxLog.Write( "XSAFEAREA START safeAreaSize=92 safeAreaX=0 safeAreaY=0" );
        XboxSoakLogState( Viewport, "safearea-start" );
        return;
    }

    if( Now - LastLogTime >= 3.0 )
    {
        LastLogTime = Now;
        XboxSoakLogState( Viewport, "safearea-heartbeat" );
    }

    if( SafeStage == 1 && bFrontend && StageElapsed >= 1.0f )
    {
        XboxSoakStartInstantMap( Viewport, TEXT("DM-Deck16][.unr") );
        StageStartTime = Now;
        ReadySent = 0;
        SafeStage = 2;
        GXboxLog.Write( "XSAFEAREA travel map=DM-Deck16][.unr" );
        return;
    }

    if( SafeStage == 2 && !bFrontend )
    {
        if( !ReadySent && StageElapsed >= 3.0f && Player && !Player->bReadyToPlay )
        {
            XboxSoakForceStartMatch( Viewport, "safearea-ready" );
            ReadySent = 1;
        }
        if( StageElapsed >= 7.0f )
        {
            XboxSoakLogState( Viewport, "safearea-proof-hold" );
            SafeStage = 3;
        }
    }
}

static void XboxSoakSmokeTick( UXboxViewport* Viewport )
{
    static INT SoakStage = 0;
    static DOUBLE StageStartTime = 0.0;
    static DWORD StageStartTick = 0;
    static DOUBLE LastLogTime = 0.0;
    static DOUBLE LastFrontendRetryTime = 0.0;
    static UBOOL TournamentReadySent = 0;
    static INT CompletedMapLegs = 0;
    static const TCHAR* MapLegs[] =
    {
        TEXT("DM-Fractal.unr"),
        TEXT("CTF-Face.unr"),
        TEXT("AS-HiSpeed.unr"),
        TEXT("DM-Deck16][.unr")
    };

    if( !XboxSoakSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    DOUBLE Now = appSeconds();
    DWORD TickNow = GetTickCount();
    FLOAT StageElapsed = StageStartTick ? (FLOAT)(TickNow - StageStartTick) / 1000.0f : 0.0f;
    FLOAT AppStageElapsed = StageStartTime > 0.0 ? (FLOAT)(Now - StageStartTime) : 0.0f;
    if( AppStageElapsed > StageElapsed )
        StageElapsed = AppStageElapsed;
    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player ? Player->GetLevel() : NULL;
    UBOOL bFrontend = XboxIsFrontendLevel( Level );

    if( SoakStage == 0 )
    {
        StageStartTime = Now;
        StageStartTick = TickNow;
        LastLogTime = 0.0;
        LastFrontendRetryTime = 0.0;
        TournamentReadySent = 0;
        CompletedMapLegs = 0;
        SoakStage = 1;
        GXboxLog.Write( "XSOAK START plan=menus,tournament,dm,ctf,assault,dm,4p-split" );
        XboxSoakLogState( Viewport, "start" );
        return;
    }

    if( Now - LastLogTime >= 5.0 )
    {
        char HeartbeatTag[96];
        appSprintf( HeartbeatTag, "heartbeat stage=%d elapsed=%.1f app=%.1f legs=%d",
            SoakStage, StageElapsed, AppStageElapsed, CompletedMapLegs );
        LastLogTime = Now;
        XboxSoakLogState( Viewport, HeartbeatTag );
    }

    if( SoakStage == 1 && bFrontend && StageElapsed >= 2.0f )
    {
        XboxMenuOpen( Viewport );
        GXboxMenu.Screen = XMS_Main;
        GXboxMenu.MainFocus = 0;
        StageStartTime = Now;
        StageStartTick = TickNow;
        SoakStage = 2;
        XboxSoakLogState( Viewport, "menu-main" );
        return;
    }

    if( SoakStage == 2 && StageElapsed >= 1.0f )
    {
        GXboxMenu.Screen = XMS_InstantAction;
        GXboxMenu.InstantFocus = 0;
        XboxSoakConfigureMatchDefaults();
        XboxSoakSelectMap( TEXT("DM-Fractal.unr") );
        StageStartTime = Now;
        StageStartTick = TickNow;
        SoakStage = 3;
        XboxSoakLogState( Viewport, "menu-instant" );
        return;
    }

    if( SoakStage == 3 && StageElapsed >= 1.0f )
    {
        GXboxMenu.Screen = XMS_PlayerSetup;
        GXboxMenu.PlayerFocus = 0;
        XboxProfileOpen( Viewport, 0 );
        XboxMenuLoadPlayerClasses();
        INT CharacterCount = Max<INT>( GXboxPlayerClasses.Num(), 1 );
        GXboxMenu.PlayerClass = XboxMenuWrapInt( GXboxMenu.PlayerClass, 3, CharacterCount );
        StageStartTime = Now;
        StageStartTick = TickNow;
        SoakStage = 4;
        XboxSoakLogState( Viewport, "menu-player-setup" );
        return;
    }

    if( SoakStage == 4 && StageElapsed >= 1.0f )
    {
        XboxMenuStartTournament( Viewport );
        StageStartTime = Now;
        StageStartTick = TickNow;
        SoakStage = 5;
        XboxSoakLogState( Viewport, "menu-tournament" );
        return;
    }

    if( SoakStage == 5 && StageElapsed >= 1.5f )
    {
        GXboxMenu.TournamentFocus = 2;
        XboxMenuStartTournamentMatch( Viewport );
        StageStartTime = Now;
        StageStartTick = TickNow;
        TournamentReadySent = 0;
        SoakStage = 6;
        GXboxLog.Write( "XSOAK travel tournament" );
        return;
    }

    if( SoakStage == 6 && !bFrontend )
    {
        if( !TournamentReadySent && StageElapsed >= 4.0f )
        {
            XboxTournamentHandleReadyInput( Viewport, 1, 0 );
            TournamentReadySent = 1;
            XboxSoakLogState( Viewport, "tournament-ready" );
        }
        if( StageElapsed >= 10.0f )
        {
            XboxSoakLogState( Viewport, "tournament-return" );
            XboxMenuReturnToFrontend( Viewport );
            StageStartTime = Now;
            StageStartTick = TickNow;
            SoakStage = 7;
        }
        return;
    }

    if( SoakStage == 7 && bFrontend && StageElapsed >= 2.0f )
    {
        XboxSoakLogState( Viewport, "frontend-after-tournament" );
        LastFrontendRetryTime = 0.0;
        StageStartTime = Now;
        StageStartTick = TickNow;
        SoakStage = 8;
        return;
    }

    if( SoakStage == 7 && !bFrontend && StageElapsed >= 4.0f )
    {
        if( LastFrontendRetryTime <= 0.0 || (Now - LastFrontendRetryTime) >= 4.0 )
        {
            XboxSoakLogState( Viewport, "frontend-return-retry" );
            XboxMenuReturnToFrontend( Viewport );
            LastFrontendRetryTime = Now;
        }
        if( StageElapsed >= 24.0f )
        {
            XboxSoakLogState( Viewport, "frontend-return-timeout" );
            GXboxLog.Write( "XSOAK FAIL frontend return timeout stage=%d legs=%d elapsed=%.1f map=%s",
                SoakStage,
                CompletedMapLegs,
                StageElapsed,
                Level && Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "" );
            GXboxLog.Flush();
            SoakStage = 12;
        }
        return;
    }

    if( SoakStage == 8 && bFrontend )
    {
        if( CompletedMapLegs < ARRAY_COUNT(MapLegs) )
        {
            const TCHAR* MapFile = MapLegs[CompletedMapLegs];
            GXboxLog.Write( "XSOAK travel instant leg=%d map=%s", CompletedMapLegs, TCHAR_TO_ANSI(MapFile) );
            XboxSoakStartInstantMap( Viewport, MapFile );
            StageStartTime = Now;
            StageStartTick = TickNow;
            SoakStage = 9;
        }
        else
        {
            StageStartTime = Now;
            StageStartTick = TickNow;
            SoakStage = 10;
        }
        return;
    }

    if( SoakStage == 9 )
    {
        if( !bFrontend && StageElapsed >= 3.0f && Player && !Player->bReadyToPlay )
            XboxSoakForceStartMatch( Viewport, "instant-ready" );
        if( StageElapsed >= 8.0f )
        {
            XboxSoakLogState( Viewport, "instant-map-return" );
            CompletedMapLegs++;
            XboxMenuReturnToFrontend( Viewport );
            StageStartTime = Now;
            StageStartTick = TickNow;
            SoakStage = 7;
        }
        return;
    }

    if( SoakStage == 10 && bFrontend && StageElapsed >= 2.0f )
    {
        GXboxLog.Write( "XSOAK travel split map=DM-Deck16][.unr" );
        XboxSoakStartSplitMap( Viewport, TEXT("DM-Deck16][.unr") );
        StageStartTime = Now;
        StageStartTick = TickNow;
        SoakStage = 11;
        return;
    }

    if( SoakStage == 11 )
    {
        if( GXboxSplitSmokeFinished )
        {
            XboxSoakLogState( Viewport, "split-finished" );
            GXboxLog.Write( "XSOAK DONE mapLegs=%d splitActive=%d activeMask=0x%X availKB=%u",
                CompletedMapLegs,
                GXboxSplitActive ? 1 : 0,
                GXboxSplitActiveMask,
                (unsigned)XboxMenuAvailPhysKB() );
            GXboxLog.Flush();
            SoakStage = 12;
        }
        else if( StageElapsed >= 30.0f )
        {
            XboxSoakLogState( Viewport, "split-timeout" );
            GXboxLog.Write( "XSOAK FAIL split timeout active=%d pending=%d finished=%d mask=0x%X",
                GXboxSplitActive ? 1 : 0,
                GXboxSplitPending ? 1 : 0,
                GXboxSplitSmokeFinished ? 1 : 0,
                GXboxSplitActiveMask );
            GXboxLog.Flush();
            SoakStage = 12;
        }
        return;
    }
}

static void XboxMenuMove( INT Delta );
static void XboxMenuAdjustInstantAction( INT Delta );
static void XboxMenuAdjustTournament( UXboxViewport* Viewport, INT Delta );
static void XboxMenuAdjustSplitMapSelect( INT Delta );
static void XboxMenuAdjustPlayerSetup( UXboxViewport* Viewport, INT Delta );
static void XboxMenuAdjustControls( UXboxViewport* Viewport, INT Delta );
static void XboxMenuAdjustAudioSettings( UXboxViewport* Viewport, INT Delta );
static void XboxMenuAdjustVideoSettings( UXboxViewport* Viewport, INT Delta );

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
                GXboxPauseReturnConfirm = 1;
                GXboxPauseReturnConfirmFocus = 1;
                GXboxLog.Write( "XMENU pause return confirmation opened default=NO" );
                break;
            case 2:
                GXboxMenu.Screen = XMS_Settings;
                GXboxMenu.SettingsFocus = XSH_Controls;
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
                XboxMenuStartTournament( Viewport );
                break;
            case 1:
                GXboxMenu.Screen = XMS_InstantAction;
                GXboxMenu.InstantFocus = 0;
                GXboxLog.Write( "XMENU screen: Instant Action" );
                break;
            case 2:
                XboxSplitReadyReset( Viewport );
                XboxSplitReadyActivatePrimary( Viewport );
                GXboxMenu.Screen = XMS_SystemLink;
                XboxSystemLinkStart( Viewport );
                GXboxLog.Write( "XMENU screen: System Link alpha" );
                break;
            case 3:
                XboxMenuStartSplitScreen( Viewport );
                break;
            case 4:
                GXboxMenu.Screen = XMS_PlayerSetup;
                GXboxMenu.PlayerFocus = 0;
                XboxProfileOpen( Viewport, 0 );
                GXboxLog.Write( "XMENU screen: Player Setup" );
                break;
            case 5:
                GXboxMenu.Screen = XMS_Settings;
                GXboxMenu.SettingsFocus = XSH_Controls;
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
    else if( GXboxMenu.Screen == XMS_Tournament )
    {
        if( GXboxMenu.TournamentFocus == 2 )
            XboxMenuStartTournamentMatch( Viewport );
        else
            XboxMenuMove( 1 );
    }
    else if( GXboxMenu.Screen == XMS_TournamentPostMatch )
    {
        XboxMenuReleaseMapPreviewTexture();
        XboxTournamentClampSelection( Viewport );
        GXboxMenu.Screen = XMS_Tournament;
        GXboxMenu.TournamentFocus = (GXboxTournamentPostMatch.Valid && !GXboxTournamentPostMatch.Advanced) ? 2 : 0;
        GXboxLog.Write( "XMENU Tournament post-match continue" );
    }
    else if( GXboxMenu.Screen == XMS_SystemLink )
    {
        GXboxLog.Write( "XSL activate role=%s phase=%s host=0x%08X peers=%d machines=%d confirmed=%d",
            TCHAR_TO_ANSI(XboxSystemLinkRoleText(GXboxSystemLink.Role)),
            TCHAR_TO_ANSI(XboxSystemLinkPhaseText(GXboxSystemLink.Phase)),
            GXboxSystemLink.HostId,
            GXboxSystemLink.Peers.Num(),
            XboxSystemLinkGroupMachineCount(),
            XboxSystemLinkConfirmedMachineCount() );
    }
    else if( GXboxMenu.Screen == XMS_SystemLinkMapSelect )
    {
        if( GXboxSystemLink.Role == XSLR_Host && GXboxMenu.SplitFocus == 4 )
            XboxSystemLinkScheduleHostLaunch();
        else if( GXboxSystemLink.Role == XSLR_Host )
            XboxMenuMove( 1 );
    }
    else if( GXboxMenu.Screen == XMS_SplitMapSelect )
    {
        if( GXboxMenu.SplitFocus == 4 )
            XboxMenuStartSplitMatch( Viewport );
        else
            XboxMenuMove( 1 );
    }
    else if( GXboxMenu.Screen == XMS_ProfileSelect )
    {
        INT ProfileIndex = XboxProfileIndexForGateRow( GXboxProfileGateFocus );
        if( ProfileIndex >= 0 )
            XboxProfileLoadFrontend( Viewport, ProfileIndex );
        else
            XboxProfileBeginCreate( XPNM_StartupCreate, -1, XMS_ProfileSelect );
    }
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
    {
        XboxMenuAdjustPlayerSetup( Viewport, 1 );
    }
    else if( GXboxMenu.Screen == XMS_Controls )
    {
        XboxMenuAdjustControls( Viewport, 1 );
    }
    else if( GXboxMenu.Screen == XMS_Settings )
    {
        switch( GXboxMenu.SettingsFocus )
        {
            case XSH_Controls:
                XboxProfileLoadControlsForContext( Viewport );
                GXboxMenu.Screen = XMS_Controls;
                GXboxMenu.ControlsFocus = XCR_Preset;
                GXboxLog.Write( "XMENU screen: Settings > Controls" );
                break;
            case XSH_Audio:
                GXboxMenu.Screen = XMS_SettingsAudio;
                GXboxMenu.SettingsFocus = XAR_MusicVolume;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU screen: Settings > Audio" );
                break;
            case XSH_Video:
                GXboxMenu.Screen = XMS_SettingsVideo;
                GXboxMenu.SettingsFocus = XVR_Brightness;
                XboxMenuLoadSettings();
                GXboxLog.Write( "XMENU screen: Settings > Video" );
                break;
        }
    }
    else if( GXboxMenu.Screen == XMS_SettingsAudio )
    {
        XboxMenuAdjustAudioSettings( Viewport, 1 );
    }
    else if( GXboxMenu.Screen == XMS_SettingsVideo )
    {
        XboxMenuAdjustVideoSettings( Viewport, 1 );
    }
    else if( GXboxMenu.Screen == XMS_ComingSoon )
    {
        return;
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
        {
            const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
            if( !XboxMenuGameUsesLives(*Game.URLValue) )
                GXboxMenu.InstantTimeLimit = XboxMenuWrap( GXboxMenu.InstantTimeLimit, Delta, ARRAY_COUNT(GXboxTimeLimits) );
            break;
        }
    }

    GXboxLog.Write( "XMENU instant adjust row=%d delta=%d", GXboxMenu.InstantFocus, Delta );
}

static void XboxMenuAdjustTournament( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_Tournament || Delta == 0 )
        return;

    XboxTournamentClampSelection( Viewport );

    switch( GXboxMenu.TournamentFocus )
    {
        case 0:
            GXboxMenu.TournamentLadder = XboxMenuWrapInt( GXboxMenu.TournamentLadder, Delta, ARRAY_COUNT(GXboxTournamentLadders) );
            XboxMenuReleaseMapPreviewTexture();
            break;
        case 2:
            GXboxMenu.TournamentSkill = XboxMenuWrapInt( GXboxMenu.TournamentSkill, Delta, ARRAY_COUNT(GXboxSkillLabels) );
            break;
    }

    XboxTournamentClampSelection( Viewport );
    GXboxLog.Write( "XMENU tournament adjust row=%d ladder=%d match=%d skill=%d delta=%d",
        GXboxMenu.TournamentFocus, GXboxMenu.TournamentLadder, GXboxMenu.TournamentMatch, GXboxMenu.TournamentSkill, Delta );
}

static void XboxMenuAdjustSplitMapSelect( INT Delta )
{
    if( (GXboxMenu.Screen != XMS_SplitMapSelect && GXboxMenu.Screen != XMS_SystemLinkMapSelect) || Delta == 0 )
        return;

    switch( GXboxMenu.SplitFocus )
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
            GXboxMenu.InstantFragLimit = XboxMenuWrap( GXboxMenu.InstantFragLimit, Delta, ARRAY_COUNT(GXboxFragLimits) );
            break;
        case 3:
        {
            const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
            if( !XboxMenuGameUsesLives(*Game.URLValue) )
                GXboxMenu.InstantTimeLimit = XboxMenuWrap( GXboxMenu.InstantTimeLimit, Delta, ARRAY_COUNT(GXboxTimeLimits) );
            break;
        }
    }

    GXboxLog.Write( "%s map adjust row=%d delta=%d",
        GXboxMenu.Screen == XMS_SystemLinkMapSelect ? "XSL" : "XMENU split",
        GXboxMenu.SplitFocus, Delta );
}

static void XboxMenuAdjustPlayerSetup( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_PlayerSetup || Delta == 0 )
        return;

    XboxMenuLoadPlayerState();

    switch( GXboxMenu.PlayerFocus )
    {
        case XBOX_PLAYER_ROW_CHARACTER:
        {
            char OldPortraitName[64];
            appMemzero( OldPortraitName, sizeof(OldPortraitName) );
            const char* CurrentPortraitName = XboxMenuCurrentPlayerPortraitName();
            if( CurrentPortraitName && CurrentPortraitName[0] )
            {
                appStrncpy( OldPortraitName, CurrentPortraitName, ARRAY_COUNT(OldPortraitName) );
                OldPortraitName[ARRAY_COUNT(OldPortraitName)-1] = 0;
            }
            GXboxMenu.PlayerClass = XboxMenuWrapInt( GXboxMenu.PlayerClass, Delta, GXboxPlayerClasses.Num() );
            GXboxPlayerSkinsClass = -1;
            GXboxPlayerVoicesClass = -1;
            GXboxMenu.PlayerSkin = 0;
            GXboxMenu.PlayerFace = 0;
            GXboxMenu.PlayerVoice = 0;
            GXboxMenu.PlayerTeam = GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultTeam;
            XboxMenuLoadPlayerSkins( GXboxMenu.PlayerClass );
            XboxMenuLoadPlayerFaces( GXboxMenu.PlayerClass, GXboxMenu.PlayerSkin );
            XboxMenuLoadPlayerVoices( GXboxMenu.PlayerClass );
            if( GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultVoice.Len() )
                GXboxMenu.PlayerVoice = XboxMenuFindURLValue( GXboxPlayerVoices, GXboxPlayerClasses(GXboxMenu.PlayerClass).DefaultVoice );
            if( OldPortraitName[0] )
                XboxRenderReleaseMenuTexture( OldPortraitName );
            GXboxLog.Write( "XMENU player character selected index=%d label=%s url=%s skin=%s face=%s portrait=%s",
                GXboxMenu.PlayerClass,
                TCHAR_TO_ANSI(*GXboxPlayerClasses(GXboxMenu.PlayerClass).Label),
                TCHAR_TO_ANSI(*GXboxPlayerClasses(GXboxMenu.PlayerClass).URLValue),
                TCHAR_TO_ANSI(*GXboxPlayerSkins(GXboxMenu.PlayerSkin).URLValue),
                GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue.Len() ? TCHAR_TO_ANSI(*GXboxPlayerFaces(GXboxMenu.PlayerFace).URLValue) : "",
                GXboxPlayerClasses(GXboxMenu.PlayerClass).PortraitName );
            GXboxLog.Flush();
            break;
        }
        case XBOX_PLAYER_ROW_TEAM:
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

    XboxProfileSaveActive( Viewport );
    GXboxLog.Write( "XMENU player adjust row=%d delta=%d", GXboxMenu.PlayerFocus, Delta );
}

static void XboxMenuAdjustControls( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_Controls || Delta == 0 )
        return;

    UXboxClient* Client = XboxMenuGetClient( Viewport );
    if( !Client )
        return;

    if( GXboxMenu.ControlsFocus == XCR_LookSensitivity )
    {
        Client->ScaleRUV = Clamp<FLOAT>( Client->ScaleRUV + Delta * 5.0f, 25.0f, 200.0f );
        if( !GXboxSplitActive )
            Client->SaveConfig();
    }
    else if( GXboxMenu.ControlsFocus == XCR_MoveSensitivity )
    {
        Client->ScaleXYZ = Clamp<FLOAT>( Client->ScaleXYZ + Delta * 5.0f, 25.0f, 200.0f );
        if( !GXboxSplitActive )
            Client->SaveConfig();
    }
    else if( GXboxMenu.ControlsFocus == XCR_InvertY )
    {
        Client->InvertVertical = !Client->InvertVertical;
        if( !GXboxSplitActive )
            Client->SaveConfig();
    }
    else if( GXboxMenu.ControlsFocus == XCR_DeadZone )
    {
        Client->DeadZone = Clamp<FLOAT>( Client->DeadZone + Delta * 0.05f, 0.05f, 0.40f );
        if( !GXboxSplitActive )
            Client->SaveConfig();
    }
    else if( GXboxMenu.ControlsFocus == XCR_Preset )
    {
        INT Preset = Client->ControlPreset;
        if( Preset < 0 )
            Preset = 0;
        XboxControlApplyPreset( Client, Preset + Delta );
    }
    else if( GXboxMenu.ControlsFocus == XCR_StickLayout )
    {
        Client->StickLayout = XboxMenuWrapInt( XboxStickLayoutClamp(Client->StickLayout), Delta, XSL_LegacySouthpaw + 1 );
        XboxControlMarkCustom( Client );
        if( !GXboxSplitActive )
            Client->SaveConfig();
        GXboxLog.Write( "XMENU controls stickLayout=%d label=%s", Client->StickLayout, TCHAR_TO_ANSI(XboxStickLayoutLabel(Client->StickLayout)) );
    }
    else if( GXboxMenu.ControlsFocus == XCR_WeaponHand )
    {
        XboxMenuSetWeaponHand( Viewport ? Viewport->Actor : NULL, XboxMenuWrapInt( XboxMenuWeaponHandIndex(Viewport ? Viewport->Actor : NULL), Delta, ARRAY_COUNT(GXboxWeaponHands) ) );
    }
    else if( GXboxMenu.ControlsFocus == XCR_AutoSwitch )
    {
        APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
        if( Player )
        {
            Player->bNeverAutoSwitch = !Player->bNeverAutoSwitch;
            Player->bNeverSwitchOnPickup = Player->bNeverAutoSwitch;
            if( !GXboxSplitActive )
                Player->SaveConfig();
        }
    }
    else
    {
        INT Button = Clamp<INT>( GXboxMenu.ControlsFocus - XCR_FirstButton, 0, XCB_Count-1 );
        INT Action = XboxControlButtonAction( Client, Button );
        Action = XboxMenuWrapInt( Action, Delta, ARRAY_COUNT(GXboxControlActions) );
        XboxControlSetButtonAction( Client, Button, Action );
        XboxControlMarkCustom( Client );
        if( !GXboxSplitActive )
            Client->SaveConfig();
        GXboxLog.Write( "XMENU controls button=%d action=%d label=%s",
            Button, Action, TCHAR_TO_ANSI(GXboxControlActions[Action].Label) );
    }
    XboxProfileSaveControlsForContext( Viewport );
}

static void XboxMenuAdjustAudioSettings( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_SettingsAudio || Delta == 0 )
        return;

    XboxMenuLoadSettings();
    UXboxClient* Client = XboxMenuGetClient( Viewport );

    switch( GXboxMenu.SettingsFocus )
    {
        case XAR_MusicVolume:
            GXboxSettingsMusicVolume = Clamp<INT>( GXboxSettingsMusicVolume + Delta * 16, 0, 255 );
            if( Client && Client->Engine && Client->Engine->Audio )
            {
                TCHAR Cmd[48];
                appSprintf( Cmd, TEXT("XAUDIOSETMUSICVOLUME %i"), GXboxSettingsMusicVolume );
                Client->Engine->Audio->Exec( Cmd );
            }
            break;
        case XAR_SoundVolume:
            GXboxSettingsSoundVolume = Clamp<INT>( GXboxSettingsSoundVolume + Delta * 16, 0, 255 );
            if( Client && Client->Engine && Client->Engine->Audio )
            {
                TCHAR Cmd[48];
                appSprintf( Cmd, TEXT("XAUDIOSETSOUNDVOLUME %i"), GXboxSettingsSoundVolume );
                Client->Engine->Audio->Exec( Cmd );
            }
            break;
        case XAR_AnnouncerVolume:
        {
            GXboxSettingsAnnouncerVolume = Clamp<INT>( GXboxSettingsAnnouncerVolume + Delta, 0, 4 );
            if( Viewport && Viewport->Actor )
                XboxSetObjectPropertyInt( Viewport->Actor, TEXT("AnnouncerVolume"), GXboxSettingsAnnouncerVolume );
            UClass* TournamentPlayerClass = UObject::StaticLoadClass( APlayerPawn::StaticClass(), NULL, TEXT("Botpack.TournamentPlayer"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
            if( TournamentPlayerClass )
            {
                TCHAR NewValue[16];
                appSprintf( NewValue, TEXT("%i"), GXboxSettingsAnnouncerVolume );
                XboxSetClassDefaultPropertyText( TournamentPlayerClass, TEXT("AnnouncerVolume"), NewValue );
            }
            XboxMenuSetUserInt( TEXT("Botpack.TournamentPlayer"), TEXT("AnnouncerVolume"), GXboxSettingsAnnouncerVolume );
            break;
        }
    }

    GXboxLog.Write( "XMENU audio adjust row=%d delta=%d", GXboxMenu.SettingsFocus, Delta );
}

static void XboxMenuAdjustVideoSettings( UXboxViewport* Viewport, INT Delta )
{
    if( GXboxMenu.Screen != XMS_SettingsVideo || Delta == 0 )
        return;

    XboxMenuLoadSettings();
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;

    switch( GXboxMenu.SettingsFocus )
    {
        case XVR_Brightness:
            if( Client )
            {
                Client->Brightness = Clamp<FLOAT>( Client->Brightness + Delta * 0.05f, 0.0f, 1.0f );
                Client->SaveConfig();
            }
            break;
        case XVR_Contrast:
            if( Client )
            {
                Client->DisplayContrast = Clamp<FLOAT>( Client->DisplayContrast + Delta * 0.05f, 0.5f, 1.5f );
                Client->SaveConfig();
            }
            break;
        case XVR_Gamma:
            if( Client )
            {
                Client->DisplayGamma = Clamp<FLOAT>( Client->DisplayGamma + Delta * 0.05f, 0.5f, 2.0f );
                Client->SaveConfig();
            }
            break;
        case XVR_SafeAreaSize:
            if( Client )
            {
                Client->SafeAreaSize = Clamp<INT>( Client->SafeAreaSize + Delta, 85, 100 );
                Client->SaveConfig();
            }
            break;
        case XVR_SafeAreaX:
            if( Client )
            {
                Client->SafeAreaX = Clamp<INT>( Client->SafeAreaX + Delta * 2, -48, 48 );
                Client->SaveConfig();
            }
            break;
        case XVR_SafeAreaY:
            if( Client )
            {
                Client->SafeAreaY = Clamp<INT>( Client->SafeAreaY + Delta * 2, -36, 36 );
                Client->SaveConfig();
            }
            break;
        case XVR_Crosshair:
            if( Player && Player->myHUD )
            {
                Player->myHUD->Crosshair = XboxMenuWrapInt( Player->myHUD->Crosshair, Delta, 9 );
                Player->myHUD->SaveConfig();
            }
            break;
        case XVR_HudColor:
            XboxMenuApplyHudColor( Viewport, GXboxSettingsHudColor + Delta );
            break;
        case XVR_CrosshairColor:
            XboxMenuApplyCrosshairColor( Viewport, GXboxSettingsCrosshairColor + Delta );
            break;
        case XVR_HudOpacity:
            GXboxSettingsHudOpacity = Clamp<INT>( GXboxSettingsHudOpacity + Delta, 1, 16 );
            if( Player && Player->myHUD )
            {
                XboxSetObjectPropertyInt( Player->myHUD, TEXT("Opacity"), GXboxSettingsHudOpacity );
                TCHAR NewValue[16];
                appSprintf( NewValue, TEXT("%i"), GXboxSettingsHudOpacity );
                XboxSetClassDefaultPropertyText( Player->myHUD->GetClass(), TEXT("Opacity"), NewValue );
                Player->myHUD->SaveConfig();
            }
            XboxMenuSetUserInt( TEXT("Botpack.ChallengeHUD"), TEXT("Opacity"), GXboxSettingsHudOpacity );
            break;
        case XVR_MatureLanguage:
        {
            UBOOL bNoMature = XboxMenuGetUserBool( TEXT("Botpack.TournamentPlayer"), TEXT("bNoMatureLanguage"), 0 );
            UBOOL bNewNoMature = !bNoMature;
            const TCHAR* NewValue = bNewNoMature ? TEXT("True") : TEXT("False");
            if( Player )
                XboxSetObjectPropertyText( Player, TEXT("bNoMatureLanguage"), NewValue );
            UClass* TournamentPlayerClass = UObject::StaticLoadClass( APlayerPawn::StaticClass(), NULL, TEXT("Botpack.TournamentPlayer"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
            if( TournamentPlayerClass )
                XboxSetClassDefaultPropertyText( TournamentPlayerClass, TEXT("bNoMatureLanguage"), NewValue );
            XboxMenuSetUserBool( TEXT("Botpack.TournamentPlayer"), TEXT("bNoMatureLanguage"), bNewNoMature );
            break;
        }
    }

    if( Client )
        XboxRenderSetDisplayCalibration( Client->Brightness, Client->DisplayContrast, Client->DisplayGamma );

    GXboxLog.Write( "XMENU video adjust row=%d delta=%d brightness=%.2f contrast=%.2f gamma=%.2f",
        GXboxMenu.SettingsFocus, Delta,
        Client ? Client->Brightness : 0.5f,
        Client ? Client->DisplayContrast : 1.0f,
        Client ? Client->DisplayGamma : 1.0f );
}

static void XboxMenuMove( INT Delta )
{
    if( GXboxMenu.Screen == XMS_Pause )
    {
        GXboxMenu.PauseFocus = (GXboxMenu.PauseFocus + Delta + 3) % 3;
    }
    else if( GXboxMenu.Screen == XMS_Main )
    {
        GXboxMenu.MainFocus = (GXboxMenu.MainFocus + Delta + 6) % 6;
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
    else if( GXboxMenu.Screen == XMS_Tournament )
    {
        GXboxMenu.TournamentFocus = XboxMenuWrapInt( GXboxMenu.TournamentFocus, Delta, 3 );
        GXboxLog.Write( "XMENU tournament focus=%d", GXboxMenu.TournamentFocus );
    }
    else if( GXboxMenu.Screen == XMS_SplitMapSelect || GXboxMenu.Screen == XMS_SystemLinkMapSelect )
    {
        GXboxMenu.SplitFocus = XboxMenuWrapInt( GXboxMenu.SplitFocus, Delta, 5 );
        GXboxLog.Write( "%s map focus=%d", GXboxMenu.Screen == XMS_SystemLinkMapSelect ? "XSL" : "XMENU split", GXboxMenu.SplitFocus );
    }
    else if( GXboxMenu.Screen == XMS_ProfileSelect )
    {
        GXboxProfileGateFocus = XboxMenuWrapInt( GXboxProfileGateFocus, Delta, Max<INT>(1, XboxProfileGateRowCount()) );
        GXboxLog.Write( "XPROFILE frontend focus=%d rows=%d", GXboxProfileGateFocus, XboxProfileGateRowCount() );
    }
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
    {
        GXboxMenu.PlayerFocus = XboxMenuWrapInt( GXboxMenu.PlayerFocus, Delta, XBOX_PLAYER_ROW_COUNT );
        GXboxLog.Write( "XMENU player focus=%d", GXboxMenu.PlayerFocus );
    }
    else if( GXboxMenu.Screen == XMS_Controls )
    {
        GXboxMenu.ControlsFocus = XboxMenuWrapInt( GXboxMenu.ControlsFocus, Delta, XCR_FirstButton + XCB_Count );
        GXboxLog.Write( "XMENU controls focus=%d", GXboxMenu.ControlsFocus );
    }
    else if( GXboxMenu.Screen == XMS_Settings )
    {
        GXboxMenu.SettingsFocus = XboxMenuWrapInt( GXboxMenu.SettingsFocus, Delta, XSH_Count );
    }
    else if( GXboxMenu.Screen == XMS_SettingsAudio )
    {
        GXboxMenu.SettingsFocus = XboxMenuWrapInt( GXboxMenu.SettingsFocus, Delta, XAR_Count );
    }
    else if( GXboxMenu.Screen == XMS_SettingsVideo )
    {
        GXboxMenu.SettingsFocus = XboxMenuWrapInt( GXboxMenu.SettingsFocus, Delta, XVR_Count );
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

static UBOOL XboxControlButtonDown( const XINPUT_GAMEPAD& Pad, INT Button, BYTE Threshold )
{
    Button = Clamp<INT>( Button, 0, XCB_Count-1 );
    if( GXboxControlButtons[Button].AnalogIndex >= 0 )
        return Pad.bAnalogButtons[GXboxControlButtons[Button].AnalogIndex] > Threshold;
    return (Pad.wButtons & GXboxControlButtons[Button].DigitalMask) != 0;
}

static UBOOL XboxThumbPressed( SHORT Cur, SHORT Prev, SHORT Threshold )
{
    if( Threshold > 0 )
        return Cur > Threshold && Prev <= Threshold;
    return Cur < Threshold && Prev >= Threshold;
}

static void XboxProfileKeyboardMove( INT DeltaX, INT DeltaY )
{
    const INT Columns = 8;
    const INT Rows = ARRAY_COUNT(GXboxProfileKeyboardKeys) / Columns;
    INT Row = Clamp<INT>( GXboxProfileKeyboardFocus / Columns, 0, Rows-1 );
    INT Column = Clamp<INT>( GXboxProfileKeyboardFocus % Columns, 0, Columns-1 );
    Row = XboxMenuWrapInt( Row, DeltaY, Rows );
    Column = XboxMenuWrapInt( Column, DeltaX, Columns );
    GXboxProfileKeyboardFocus = Row * Columns + Column;
}

static void XboxProfileKeyboardAppend( TCHAR Character )
{
    INT Length = appStrlen( GXboxProfileEditName );
    if( Length >= XBOX_PROFILE_NAME_MAX )
        return;
    if( Character == ' ' && (Length == 0 || GXboxProfileEditName[Length-1] == ' ') )
        return;
    GXboxProfileEditName[Length] = Character;
    GXboxProfileEditName[Length+1] = 0;
}

static void XboxProfileKeyboardActivate( UXboxViewport* Viewport )
{
    INT Key = Clamp<INT>( GXboxProfileKeyboardFocus, 0, ARRAY_COUNT(GXboxProfileKeyboardKeys)-1 );
    if( Key < 26 )
        XboxProfileKeyboardAppend( (TCHAR)('A' + Key) );
    else if( Key < 36 )
        XboxProfileKeyboardAppend( (TCHAR)('0' + Key - 26) );
    else if( Key == 36 )
        XboxProfileKeyboardAppend( ' ' );
    else if( Key == 37 )
    {
        INT Length = appStrlen( GXboxProfileEditName );
        if( Length > 0 )
            GXboxProfileEditName[Length-1] = 0;
    }
    else if( Key == 38 )
    {
        GXboxProfileEditName[0] = 0;
    }
    else
    {
        INT Length = appStrlen( GXboxProfileEditName );
        while( Length > 0 && GXboxProfileEditName[Length-1] == ' ' )
            GXboxProfileEditName[--Length] = 0;
        XboxProfileCommitName( Viewport );
    }
}

static void XboxProfileKeyboardHandlePad( UXboxViewport* Viewport, const XINPUT_GAMEPAD& Pad, const XINPUT_GAMEPAD& PrevPad )
{
    WORD CurDigital = Pad.wButtons;
    WORD PrevDigital = PrevPad.wButtons;
    if( XboxButtonPressed(CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_UP)
    ||  XboxThumbPressed(Pad.sThumbLY, PrevPad.sThumbLY, 18000) )
        XboxProfileKeyboardMove( 0, -1 );
    if( XboxButtonPressed(CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_DOWN)
    ||  XboxThumbPressed(Pad.sThumbLY, PrevPad.sThumbLY, -18000) )
        XboxProfileKeyboardMove( 0, 1 );
    if( XboxButtonPressed(CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_LEFT)
    ||  XboxThumbPressed(Pad.sThumbLX, PrevPad.sThumbLX, -18000) )
        XboxProfileKeyboardMove( -1, 0 );
    if( XboxButtonPressed(CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_RIGHT)
    ||  XboxThumbPressed(Pad.sThumbLX, PrevPad.sThumbLX, 18000) )
        XboxProfileKeyboardMove( 1, 0 );
    if( XboxAnalogPressed(Pad, PrevPad, XINPUT_GAMEPAD_A) )
        XboxProfileKeyboardActivate( Viewport );
    if( XboxButtonPressed(CurDigital, PrevDigital, XINPUT_GAMEPAD_START) )
        XboxProfileCommitName( Viewport );
    if( XboxButtonPressed(CurDigital, PrevDigital, XINPUT_GAMEPAD_BACK)
    ||  XboxAnalogPressed(Pad, PrevPad, XINPUT_GAMEPAD_B) )
        XboxProfileCancelName();
}

static void XboxSplitReadyAdjustProfile( UXboxViewport* Viewport, INT Port, INT Delta )
{
    XboxSplitReadyEnsure();
    Port = Clamp<INT>( Port, 0, 3 );
    if( !GXboxSplitReadySlots[Port].Joined || GXboxSplitReadySlots[Port].Locked )
        return;

    const FXboxPlayerClassOption& OldPlayer = XboxSplitReadyPlayerClass( Port );
    if( OldPlayer.PortraitName[0] )
        XboxRenderReleaseMenuTexture( OldPlayer.PortraitName );

    INT CurrentProfile = GXboxSplitReadySlots[Port].Profile;
    INT StartProfile = XboxMenuWrapInt( CurrentProfile, Delta, XBOX_PROFILE_COUNT );
    INT ProfileIndex = XboxSplitFindUnusedCreatedProfile( Port, StartProfile, Delta );
    if( ProfileIndex < 0 )
        return;
    XboxSplitAssignProfile( Port, ProfileIndex, XboxMenuGetClient(Viewport) );
    XboxSystemLinkMarkLocalReadyChanged();
    GXboxLog.Write( "XSPLIT ready port=%d profile=%d name=%s",
        Port + 1, ProfileIndex + 1, TCHAR_TO_ANSI(GXboxProfiles[ProfileIndex].Name) );
}

static void XboxSplitReadyAdjustCharacter( INT Port, INT Delta )
{
    XboxSplitReadyEnsure();
    Port = Clamp<INT>( Port, 0, 3 );
    if( !GXboxSplitReadySlots[Port].Joined || GXboxSplitReadySlots[Port].Locked )
        return;

    const FXboxPlayerClassOption& OldPlayer = XboxSplitReadyPlayerClass( Port );
    if( OldPlayer.PortraitName[0] )
        XboxRenderReleaseMenuTexture( OldPlayer.PortraitName );
    GXboxSplitReadySlots[Port].Character = XboxMenuWrapInt( GXboxSplitReadySlots[Port].Character, Delta, GXboxPlayerClasses.Num() );
    const FXboxPlayerClassOption& Player = XboxSplitReadyPlayerClass( Port );
    GXboxSplitReadySlots[Port].Team = Player.DefaultTeam;
    XboxSplitSaveProfileIdentity( Port );
    XboxSystemLinkMarkLocalReadyChanged();
    GXboxLog.Write( "XSPLIT ready port=%d character=%d label=%s portrait=%s",
        Port + 1, GXboxSplitReadySlots[Port].Character, TCHAR_TO_ANSI(*Player.Label), Player.PortraitName );
}

static void XboxSplitReadyAdjustTeam( INT Port, INT Delta )
{
    XboxSplitReadyEnsure();
    Port = Clamp<INT>( Port, 0, 3 );
    if( !GXboxSplitReadySlots[Port].Joined || GXboxSplitReadySlots[Port].Locked )
        return;

    if( GXboxSplitReadySlots[Port].Team == 255 )
        GXboxSplitReadySlots[Port].Team = Delta > 0 ? 0 : 3;
    else
    {
        GXboxSplitReadySlots[Port].Team += Delta;
        if( GXboxSplitReadySlots[Port].Team > 3 )
            GXboxSplitReadySlots[Port].Team = 255;
        else if( GXboxSplitReadySlots[Port].Team < 0 )
            GXboxSplitReadySlots[Port].Team = 255;
    }
    XboxSplitSaveProfileIdentity( Port );
    XboxSystemLinkMarkLocalReadyChanged();
    GXboxLog.Write( "XSPLIT ready port=%d team=%d", Port + 1, GXboxSplitReadySlots[Port].Team );
}

static void XboxSplitReadyMove( INT Port, INT Delta )
{
    XboxSplitReadyEnsure();
    Port = Clamp<INT>( Port, 0, 3 );
    if( !GXboxSplitReadySlots[Port].Joined || GXboxSplitReadySlots[Port].Locked )
        return;
    GXboxSplitReadySlots[Port].Focus = XboxMenuWrapInt( GXboxSplitReadySlots[Port].Focus, Delta, 3 );
}

static void XboxSplitReadyHandlePad( UXboxViewport* Viewport, INT Port, const XINPUT_GAMEPAD& Pad, const XINPUT_GAMEPAD& PrevPad )
{
    XboxSplitReadyEnsure();
    if( GXboxMenu.Screen == XMS_ProfileName && GXboxProfileNameMode == XPNM_MultiplayerCreate )
    {
        if( Port == GXboxProfileNamePort )
            XboxProfileKeyboardHandlePad( Viewport, Pad, PrevPad );
        return;
    }
    WORD CurDigital  = Pad.wButtons;
    WORD PrevDigital = PrevPad.wButtons;

    if( XboxAnalogPressed(Pad, PrevPad, XINPUT_GAMEPAD_X)
    &&  !GXboxSplitReadySlots[Port].Locked )
    {
        XboxProfileBeginCreate( XPNM_MultiplayerCreate, Port, GXboxMenu.Screen );
        return;
    }

    if( XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_B )
    ||  XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_BACK ) )
    {
        if( GXboxSplitReadySlots[Port].Locked )
        {
            GXboxSplitReadySlots[Port].Locked = 0;
            XboxSystemLinkMarkLocalReadyChanged();
            GXboxLog.Write( "XSPLIT ready unlocked port=%d", Port + 1 );
        }
        else if( GXboxSplitReadySlots[Port].Joined && Port == 0 )
        {
            GXboxLog.Write( "XSPLIT fixed P1 backing out of ready screen" );
            XboxMenuBack( Viewport );
        }
        else if( GXboxSplitReadySlots[Port].Joined )
        {
            const FXboxPlayerClassOption& Player = XboxSplitReadyPlayerClass( Port );
            if( Player.PortraitName[0] )
                XboxRenderReleaseMenuTexture( Player.PortraitName );
            GXboxSplitReadySlots[Port].Joined = 0;
            GXboxSplitReadySlots[Port].Profile = -1;
            appMemzero( &GXboxSplitProfileControls[Port], sizeof(GXboxSplitProfileControls[Port]) );
            XboxSystemLinkMarkLocalReadyChanged();
            GXboxLog.Write( "XSPLIT ready leave port=%d", Port + 1 );
        }
        return;
    }

    if( XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_A ) )
    {
        if( !GXboxSplitReadySlots[Port].Joined )
        {
            if( !XboxSplitEnsureProfileForJoin(Viewport, Port) )
            {
                XboxProfileBeginCreate( XPNM_MultiplayerCreate, Port, GXboxMenu.Screen );
                GXboxLog.Write( "XSPLIT ready join port=%d requires new profile", Port + 1 );
                return;
            }
            GXboxSplitReadySlots[Port].Joined = 1;
            GXboxSplitReadySlots[Port].Locked = 0;
            GXboxSplitReadySlots[Port].Focus = 0;
            XboxSystemLinkMarkLocalReadyChanged();
            GXboxLog.Write( "XSPLIT ready join port=%d profile=%d name=%s",
                Port + 1,
                GXboxSplitReadySlots[Port].Profile + 1,
                TCHAR_TO_ANSI(GXboxProfiles[GXboxSplitReadySlots[Port].Profile].Name) );
        }
        else if( !GXboxSplitReadySlots[Port].Locked )
        {
            INT ProfileIndex = GXboxSplitReadySlots[Port].Profile;
            if( ProfileIndex < 0
            ||  ProfileIndex >= XBOX_PROFILE_COUNT
            ||  !GXboxProfiles[ProfileIndex].Created
            ||  XboxSplitProfileUsedByOther(Port, ProfileIndex) )
            {
                GXboxLog.Write( "XSPLIT ready lock blocked port=%d invalidOrDuplicateProfile=%d", Port + 1, ProfileIndex );
                return;
            }
            GXboxSplitReadySlots[Port].Locked = 1;
            XboxSystemLinkMarkLocalReadyChanged();
            GXboxLog.Write( "XSPLIT ready locked port=%d", Port + 1 );
        }
        return;
    }

    if( Port == 0 && XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_START ) )
    {
        if( GXboxMenu.Screen == XMS_SystemLink )
        {
            if( XboxSystemLinkGroupMachineCount() < 2 )
            {
                GXboxLog.Write( "XSL ready confirm blocked: waiting for another Xbox" );
            }
            else if( XboxSystemLinkLocalReadyCanConfirm() )
            {
                GXboxSystemLink.ReadyConfirmed = 1;
                GXboxSystemLink.Phase = XSLP_ReadyConfirmed;
                GXboxLog.Write( "XSL local machine confirmed ready joined=%d readyMask=0x%X lockedMask=0x%X confirmed=%d/%d",
                    XboxSplitReadyJoinedCount(),
                    XboxSystemLinkReadyMask(),
                    XboxSystemLinkLockedMask(),
                    XboxSystemLinkConfirmedMachineCount(),
                    XboxSystemLinkGroupMachineCount() );
                XboxSystemLinkSendProbe();
            }
            else
            {
                GXboxLog.Write( "XSL ready confirm blocked joined=%d localReady=0", XboxSplitReadyJoinedCount() );
            }
        }
        else if( XboxSplitReadyCanBegin() )
        {
            GXboxMenu.Screen = XMS_SplitMapSelect;
            GXboxMenu.SplitFocus = 0;
            GXboxLog.Write( "XSPLIT ready complete joined=%d -> map select", XboxSplitReadyJoinedCount() );
        }
        else
        {
            GXboxLog.Write( "XSPLIT ready start blocked joined=%d", XboxSplitReadyJoinedCount() );
        }
        return;
    }

    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_UP )
    ||  XboxThumbPressed( Pad.sThumbLY, PrevPad.sThumbLY, 18000 ) )
        XboxSplitReadyMove( Port, -1 );
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_DOWN )
    ||  XboxThumbPressed( Pad.sThumbLY, PrevPad.sThumbLY, -18000 ) )
        XboxSplitReadyMove( Port, 1 );

    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_LEFT )
    ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, -18000 ) )
    {
        if( GXboxSplitReadySlots[Port].Focus == 0 )
            XboxSplitReadyAdjustProfile( Viewport, Port, -1 );
        else if( GXboxSplitReadySlots[Port].Focus == 1 )
            XboxSplitReadyAdjustCharacter( Port, -1 );
        else
            XboxSplitReadyAdjustTeam( Port, -1 );
    }
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_RIGHT )
    ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, 18000 ) )
    {
        if( GXboxSplitReadySlots[Port].Focus == 0 )
            XboxSplitReadyAdjustProfile( Viewport, Port, 1 );
        else if( GXboxSplitReadySlots[Port].Focus == 1 )
            XboxSplitReadyAdjustCharacter( Port, 1 );
        else
            XboxSplitReadyAdjustTeam( Port, 1 );
    }
}

static void XboxSplitReadyPollControllers( UXboxViewport* Viewport, const XINPUT_GAMEPAD& Port0Pad, const XINPUT_GAMEPAD& Port0PrevPad )
{
    XboxSplitReadyHandlePad( Viewport, 0, Port0Pad, Port0PrevPad );

    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    for( INT Port=1; Port<4; Port++ )
    {
        if( !(DeviceMask & (1 << Port)) )
        {
            if( GXboxSplitReadyControllerHandles[Port] )
            {
                XInputClose( GXboxSplitReadyControllerHandles[Port] );
                GXboxSplitReadyControllerHandles[Port] = NULL;
            }
            appMemzero( &GXboxSplitReadyControllerState[Port], sizeof(GXboxSplitReadyControllerState[Port]) );
            appMemzero( &GXboxSplitReadyPrevControllerState[Port], sizeof(GXboxSplitReadyPrevControllerState[Port]) );
            continue;
        }

        if( !GXboxSplitReadyControllerHandles[Port] )
            GXboxSplitReadyControllerHandles[Port] = XboxOpenControllerOnPort( Port, DeviceMask );
        if( !GXboxSplitReadyControllerHandles[Port] )
            continue;

        GXboxSplitReadyPrevControllerState[Port] = GXboxSplitReadyControllerState[Port];
        DWORD Result = XInputGetState( GXboxSplitReadyControllerHandles[Port], &GXboxSplitReadyControllerState[Port] );
        if( Result == ERROR_SUCCESS )
        {
            XboxSplitReadyHandlePad( Viewport, Port, GXboxSplitReadyControllerState[Port].Gamepad, GXboxSplitReadyPrevControllerState[Port].Gamepad );
        }
        else
        {
            XInputClose( GXboxSplitReadyControllerHandles[Port] );
            GXboxSplitReadyControllerHandles[Port] = NULL;
            appMemzero( &GXboxSplitReadyControllerState[Port], sizeof(GXboxSplitReadyControllerState[Port]) );
            appMemzero( &GXboxSplitReadyPrevControllerState[Port], sizeof(GXboxSplitReadyPrevControllerState[Port]) );
            GXboxLog.Write( "XSPLIT ready controller lost port=%d result=0x%08X", Port + 1, Result );
        }
    }
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

    if( XboxFullMenuProofRequested() != XFMP_None )
        return 1;

    if( GXboxMenu.Screen == XMS_Pause && GXboxPauseReturnConfirm )
    {
        if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_UP )
        ||  XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_DOWN )
        ||  XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_LEFT )
        ||  XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_RIGHT )
        ||  XboxThumbPressed( Pad.sThumbLY, PrevPad.sThumbLY, 18000 )
        ||  XboxThumbPressed( Pad.sThumbLY, PrevPad.sThumbLY, -18000 )
        ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, 18000 )
        ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, -18000 ) )
        {
            GXboxPauseReturnConfirmFocus = 1 - GXboxPauseReturnConfirmFocus;
            GXboxLog.Write( "XMENU pause return confirmation focus=%s", GXboxPauseReturnConfirmFocus == 0 ? "YES" : "NO" );
            return 1;
        }
        if( XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_A ) )
        {
            if( GXboxPauseReturnConfirmFocus == 0 )
            {
                GXboxPauseReturnConfirm = 0;
                GXboxLog.Write( "XMENU pause return confirmation accepted" );
                XboxMenuReturnToFrontend( Viewport );
            }
            else
            {
                GXboxPauseReturnConfirm = 0;
                GXboxLog.Write( "XMENU pause return confirmation cancelled selection=NO" );
            }
            return 1;
        }
        if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_START )
        ||  XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_BACK )
        ||  XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_B ) )
        {
            GXboxPauseReturnConfirm = 0;
            GXboxLog.Write( "XMENU pause return confirmation cancelled input=BACK" );
            return 1;
        }
        return 1;
    }

    if( GXboxMenu.Screen == XMS_ProfileName )
    {
        if( GXboxProfileNameMode == XPNM_MultiplayerCreate )
            XboxSplitReadyPollControllers( Viewport, Pad, PrevPad );
        else
            XboxProfileKeyboardHandlePad( Viewport, Pad, PrevPad );
        return 1;
    }

    if( GXboxMenu.Screen == XMS_SplitReady )
    {
        XboxSplitReadyPollControllers( Viewport, Pad, PrevPad );
        return 1;
    }

    if( GXboxMenu.Screen == XMS_SystemLink && GXboxSystemLink.Phase == XSLP_Launching )
        return 1;

    if( GXboxMenu.Screen == XMS_SystemLink
    &&  GXboxSystemLink.HostId
    &&  XboxSystemLinkGroupMachineCount() >= 2
    &&  GXboxSystemLink.Phase != XSLP_Launching )
    {
        XboxSplitReadyPollControllers( Viewport, Pad, PrevPad );
        return 1;
    }

    if( (GXboxMenu.Screen == XMS_SplitMapSelect || GXboxMenu.Screen == XMS_SystemLinkMapSelect) && XboxViewportIndex(Viewport) != 0 )
        return 1;

    if( GXboxMenu.Screen == XMS_SystemLinkMapSelect && GXboxSystemLink.Role != XSLR_Host )
    {
        if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_BACK )
        ||  XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_B ) )
            XboxMenuBack( Viewport );
        return 1;
    }

    if( GXboxMenu.Screen == XMS_Main && XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_X ) )
    {
        XboxProfileLoadDirectory( 1 );
        GXboxProfileGateFocus = XboxProfileGateRowForIndex( GXboxActiveProfile );
        GXboxMenu.Screen = XMS_ProfileSelect;
        GXboxLog.Write( "XPROFILE optional switch opened activeSlot=%d profiles=%d", GXboxActiveProfile + 1, XboxProfileCreatedCount() );
        return 1;
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
        XboxMenuAdjustTournament( Viewport, -1 );
        XboxMenuAdjustSplitMapSelect( -1 );
        XboxMenuAdjustPlayerSetup( Viewport, -1 );
        XboxMenuAdjustControls( Viewport, -1 );
        XboxMenuAdjustAudioSettings( Viewport, -1 );
        XboxMenuAdjustVideoSettings( Viewport, -1 );
    }
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_RIGHT )
    ||  XboxThumbPressed( Pad.sThumbLX, PrevPad.sThumbLX, 18000 ) )
    {
        XboxMenuAdjustInstantAction( 1 );
        XboxMenuAdjustTournament( Viewport, 1 );
        XboxMenuAdjustSplitMapSelect( 1 );
        XboxMenuAdjustPlayerSetup( Viewport, 1 );
        XboxMenuAdjustControls( Viewport, 1 );
        XboxMenuAdjustAudioSettings( Viewport, 1 );
        XboxMenuAdjustVideoSettings( Viewport, 1 );
    }
    if( GXboxMenu.Screen == XMS_Controls && XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_Y ) )
    {
        XboxControlApplyPreset( XboxMenuGetClient(Viewport), 0 );
        XboxProfileSaveControlsForContext( Viewport );
        GXboxMenu.ControlsFocus = XCR_Preset;
        return 1;
    }
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_START ) )
    {
        if( GXboxMenu.Screen == XMS_Pause )
            XboxMenuClose( Viewport );
    }
    if( XboxAnalogPressed( Pad, PrevPad, XINPUT_GAMEPAD_A ) )
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

static void XboxMenuTextFit( UCanvas* Canvas, UFont* Font, FLOAT X, FLOAT Y, FLOAT MaxWidth, BYTE R, BYTE G, BYTE B, const TCHAR* Text )
{
    if( !Text )
        Text = TEXT("");

    TCHAR Source[256];
    appStrncpy( Source, Text, ARRAY_COUNT(Source) );
    Source[ARRAY_COUNT(Source)-1] = 0;

    TCHAR Buffer[256];
    INT SourceLen = appStrlen( Source );
    INT BestLen = SourceLen;
    for( INT TestLen=SourceLen; TestLen>=0; TestLen-- )
    {
        INT CopyLen = Min<INT>( TestLen, ARRAY_COUNT(Buffer)-1 );
        for( INT i=0; i<CopyLen; i++ )
            Buffer[i] = Source[i];
        Buffer[CopyLen] = 0;
        if( TestLen < SourceLen && CopyLen > 3 )
        {
            Buffer[CopyLen-3] = '.';
            Buffer[CopyLen-2] = '.';
            Buffer[CopyLen-1] = '.';
        }

        INT XL = 0;
        INT YL = 0;
        XboxMenuTextSize( Canvas, Font, Buffer, XL, YL );
        if( XL <= MaxWidth || TestLen == 0 )
        {
            BestLen = TestLen;
            break;
        }
    }

    INT CopyLen = Min<INT>( BestLen, ARRAY_COUNT(Buffer)-1 );
    for( INT i=0; i<CopyLen; i++ )
        Buffer[i] = Source[i];
    Buffer[CopyLen] = 0;
    if( BestLen < SourceLen && CopyLen > 3 )
    {
        Buffer[CopyLen-3] = '.';
        Buffer[CopyLen-2] = '.';
        Buffer[CopyLen-1] = '.';
    }
    XboxMenuText( Canvas, Font, X, Y, R, G, B, Buffer );
}

static FLOAT XboxMenuCenteredTextY( UCanvas* Canvas, UFont* Font, FLOAT Top, FLOAT Bottom, const TCHAR* Text )
{
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, Font, Text, XL, YL );
    return (FLOAT)(INT)(Top + ((Bottom - Top) - (FLOAT)YL) * 0.5f);
}

static void XboxMenuDrawSelectionHighlight( UCanvas* Canvas, UFont* Font, FLOAT X1, FLOAT X2, FLOAT TextY, const TCHAR* Text, FLOAT Alpha )
{
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, Font, Text, XL, YL );
    FLOAT TextCenter = TextY + (FLOAT)YL * 0.5f;
    FLOAT HighlightH = Max<FLOAT>( 28.0f, (FLOAT)YL + 12.0f );
    FLOAT Top = (FLOAT)(INT)(TextCenter - HighlightH * 0.5f);
    FLOAT Bottom = (FLOAT)(INT)(TextCenter + HighlightH * 0.5f);
    XboxMenuDrawRect( Canvas, X1, Top, X2, Bottom - 3.0f, 12, 82, 166, Alpha );
    XboxMenuDrawRect( Canvas, X1, Bottom - 3.0f, X2, Bottom, 28, 108, 205, Min<FLOAT>( 0.65f, Alpha + 0.10f ) );
}

static FLOAT XboxMenuFooterTop( UCanvas* Canvas )
{
    return Canvas ? Canvas->ClipY - 42.0f : 0.0f;
}

static FLOAT XboxMenuFooterBottom( UCanvas* Canvas )
{
    return Canvas ? Canvas->ClipY - 14.0f : 0.0f;
}

static FLOAT XboxMenuFooterContentBottom( UCanvas* Canvas )
{
    return XboxMenuFooterBottom(Canvas) - 6.0f;
}

static FLOAT XboxMenuContentRight( UCanvas* Canvas )
{
    return Canvas ? Canvas->ClipX - 18.0f : 0.0f;
}

static FLOAT XboxMenuContentBottom( UCanvas* Canvas )
{
    return XboxMenuFooterTop(Canvas) - 10.0f;
}

static FLOAT XboxMenuAboveFooterTextY( UCanvas* Canvas, UFont* Font, const TCHAR* Text, FLOAT Gap )
{
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, Font, Text, XL, YL );
    return (FLOAT)(INT)(XboxMenuFooterTop(Canvas) - Gap - (FLOAT)YL);
}

static void XboxMenuTextWrap( UCanvas* Canvas, UFont* Font, FLOAT X, FLOAT Y, FLOAT MaxWidth, INT MaxLines, FLOAT LineStep, BYTE R, BYTE G, BYTE B, const TCHAR* Text )
{
    if( !Canvas || !Font || !Text || MaxLines <= 0 )
        return;

    TCHAR Source[512];
    appStrncpy( Source, Text, ARRAY_COUNT(Source) );
    Source[ARRAY_COUNT(Source)-1] = 0;

    TCHAR* Cursor = Source;
    for( INT Line=0; Line<MaxLines && *Cursor; Line++ )
    {
        while( *Cursor == ' ' )
            Cursor++;
        if( !*Cursor )
            break;

        INT BestLen = 0;
        INT LastSpace = -1;
        TCHAR Buffer[256];
        for( INT i=0; Cursor[i] && i<ARRAY_COUNT(Buffer)-1; i++ )
        {
            if( Cursor[i] == ' ' )
                LastSpace = i;
            Buffer[i] = Cursor[i];
            Buffer[i+1] = 0;

            INT XL = 0;
            INT YL = 0;
            XboxMenuTextSize( Canvas, Font, Buffer, XL, YL );
            if( XL > MaxWidth )
                break;
            BestLen = i + 1;
        }

        if( Cursor[BestLen] && LastSpace > 0 )
            BestLen = LastSpace;
        if( BestLen <= 0 )
            BestLen = Min<INT>( appStrlen(Cursor), ARRAY_COUNT(Buffer)-1 );

        for( INT i=0; i<BestLen && i<ARRAY_COUNT(Buffer)-1; i++ )
            Buffer[i] = Cursor[i];
        Buffer[Min<INT>(BestLen, ARRAY_COUNT(Buffer)-1)] = 0;

        if( Line == MaxLines-1 && Cursor[BestLen] && BestLen > 3 )
        {
            Buffer[BestLen-3] = '.';
            Buffer[BestLen-2] = '.';
            Buffer[BestLen-1] = '.';
        }

        XboxMenuText( Canvas, Font, X, Y + Line * LineStep, R, G, B, Buffer );
        Cursor += BestLen;
    }
}

static void XboxMenuDrawButtonPrompt( UCanvas* Canvas, FLOAT X, FLOAT Y, const char* ButtonImage, const TCHAR* Label )
{
    if( !Canvas )
        return;

    UFont* PromptFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, PromptFont, Label, XL, YL );

    FLOAT IconSize = 18.0f;
    FLOAT IconY = (FLOAT)(INT)Y;
    FLOAT TextY = XboxMenuCenteredTextY( Canvas, PromptFont, IconY, IconY + IconSize, Label );
    XboxMenuDrawImage( Canvas, ButtonImage, X, IconY, IconSize, IconSize );
    XboxMenuText( Canvas, PromptFont, X + 22.0f, TextY, 135, 255, 120, Label );
}

static FLOAT XboxMenuButtonPromptWidth( UCanvas* Canvas, const TCHAR* Label )
{
    UFont* PromptFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, PromptFont, Label, XL, YL );
    return 22.0f + (FLOAT)XL;
}

static FLOAT XboxMenuNextButtonPromptX( UCanvas* Canvas, FLOAT X, const TCHAR* Label )
{
    return X + XboxMenuButtonPromptWidth(Canvas, Label) + 14.0f;
}

static FLOAT XboxMenuFooterPromptY( UCanvas* Canvas )
{
    return Canvas ? (FLOAT)(INT)(XboxMenuFooterTop(Canvas) + (XboxMenuFooterContentBottom(Canvas) - XboxMenuFooterTop(Canvas) - 18.0f) * 0.5f) : 0.0f;
}

static void XboxMenuDrawScrollPrompt( UCanvas* Canvas, FLOAT X, FLOAT Y )
{
    if( !Canvas )
        return;

    UFont* PromptFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    const TCHAR* Slash = TEXT("/");
    const TCHAR* Label = TEXT("SCROLL");
    INT SlashXL = 0;
    INT SlashYL = 0;
    INT LabelXL = 0;
    INT LabelYL = 0;
    XboxMenuTextSize( Canvas, PromptFont, Slash, SlashXL, SlashYL );
    XboxMenuTextSize( Canvas, PromptFont, Label, LabelXL, LabelYL );

    FLOAT IconSize = 18.0f;
    FLOAT IconY = (FLOAT)(INT)Y;
    FLOAT SlashY = XboxMenuCenteredTextY( Canvas, PromptFont, IconY, IconY + IconSize, Slash );
    FLOAT LabelY = XboxMenuCenteredTextY( Canvas, PromptFont, IconY, IconY + IconSize, Label );
    FLOAT SlashX = X + IconSize - 2.0f;
    FLOAT StickX = SlashX + (FLOAT)SlashXL - 1.0f;
    FLOAT LabelX = StickX + IconSize + 4.0f;

    XboxMenuDrawImage( Canvas, "button_dpad.xui", X, IconY, IconSize, IconSize );
    XboxMenuText( Canvas, PromptFont, SlashX, SlashY, 135, 255, 120, Slash );
    XboxMenuDrawImage( Canvas, "button_lmove.xui", StickX, IconY, IconSize, IconSize );
    XboxMenuText( Canvas, PromptFont, LabelX, LabelY, 135, 255, 120, Label );
}

static FLOAT XboxMenuScrollPromptWidth( UCanvas* Canvas )
{
    UFont* PromptFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    INT SlashXL = 0;
    INT SlashYL = 0;
    INT LabelXL = 0;
    INT LabelYL = 0;
    XboxMenuTextSize( Canvas, PromptFont, TEXT("/"), SlashXL, SlashYL );
    XboxMenuTextSize( Canvas, PromptFont, TEXT("SCROLL"), LabelXL, LabelYL );
    return 37.0f + (FLOAT)SlashXL + (FLOAT)LabelXL;
}

static void XboxMenuDrawFooterBand( UCanvas* Canvas )
{
    if( !Canvas )
        return;

    XboxMenuDrawRect( Canvas, 18, XboxMenuFooterTop(Canvas), Canvas->ClipX-18, XboxMenuFooterBottom(Canvas), 9, 42, 89, 0.72f );
    XboxMenuDrawRect( Canvas, 20, XboxMenuFooterBottom(Canvas)-6.0f, Canvas->ClipX-20, XboxMenuFooterBottom(Canvas)-3.0f, 31, 112, 205, 0.85f );
}

static void XboxMenuDrawFooterCommands
(
    UCanvas* Canvas,
    const char* Image1, const TCHAR* Label1,
    const char* Image2=NULL, const TCHAR* Label2=NULL,
    const char* Image3=NULL, const TCHAR* Label3=NULL
)
{
    FLOAT X = 38.0f;
    FLOAT Y = XboxMenuFooterPromptY( Canvas );

    if( Image1 && Label1 )
    {
        XboxMenuDrawButtonPrompt( Canvas, X, Y, Image1, Label1 );
        X = XboxMenuNextButtonPromptX( Canvas, X, Label1 );
    }
    if( Image2 && Label2 )
    {
        XboxMenuDrawButtonPrompt( Canvas, X, Y, Image2, Label2 );
        X = XboxMenuNextButtonPromptX( Canvas, X, Label2 );
    }
    if( Image3 && Label3 )
        XboxMenuDrawButtonPrompt( Canvas, X, Y, Image3, Label3 );
}

static void XboxMenuDrawTexture( UCanvas* Canvas, UTexture* Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL )
{
    if( !Canvas || !Texture )
        return;

    if( Canvas->Frame && XboxRenderDrawMenuUTexture( Canvas->Frame, Texture, X, Y, XL, YL, 1.0f ) )
        return;

    Canvas->DrawTile( Texture, X, Y, XL, YL, 0.0f, 0.0f, Texture->USize, Texture->VSize, NULL, Canvas->Z, FPlane(1,1,1,1), FPlane(0,0,0,0), PF_TwoSided );
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
    if( !Viewport || !Player )
        return;

    AWeapon* Weapon = XboxWeaponWheelFindWeapon( Player, SlotIndex );
    if( !XboxWeaponWheelCanSelect( Weapon ) )
        return;

    if( Player->Weapon == Weapon )
        return;

    AWeapon* Before = Player->Weapon;
    AWeapon* PendingBefore = Player->PendingWeapon;
    TCHAR Cmd[128];
    appSprintf( Cmd, TEXT("GetWeapon %s"), GXboxWeaponWheelSlots[SlotIndex].ClassName );
    UBOOL Handled = Player->ScriptConsoleExec( Cmd, *GLog, Player );

    if( GXboxWeaponWheelLogCount < 64 )
    {
        GXboxWeaponWheelLogCount++;
        GXboxLog.Write( "XWHEEL selected slot=%d weapon=%s ammo=%d handled=%d before=%s pendingBefore=%s current=%s pending=%s",
            SlotIndex,
            TCHAR_TO_ANSI(GXboxWeaponWheelSlots[SlotIndex].DisplayName),
            XboxWeaponWheelAmmoAmount(Weapon),
            Handled ? 1 : 0,
            Before ? TCHAR_TO_ANSI(Before->GetFullName()) : "(none)",
            PendingBefore ? TCHAR_TO_ANSI(PendingBefore->GetFullName()) : "(none)",
            Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
            Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)" );
    }
}

static void XboxWeaponCycle( UXboxViewport* Viewport, APlayerPawn* Player, UBOOL bForward )
{
    if( !Viewport || !Player )
        return;

    AWeapon* Before = Player->Weapon;
    AWeapon* PendingBefore = Player->PendingWeapon;
    const TCHAR* Cmd = bForward ? TEXT("NextWeapon") : TEXT("PrevWeapon");
    UBOOL Handled = Player->ScriptConsoleExec( Cmd, *GLog, Player );
    if( GXboxWeaponWheelLogCount < 64 )
    {
        GXboxWeaponWheelLogCount++;
        GXboxLog.Write( "XWHEEL tap cycle %s handled=%d player=0x%08X before=%s pendingBefore=%s current=%s pending=%s",
            bForward ? "next" : "prev",
            Handled ? 1 : 0,
            (DWORD)Player,
            Before ? TCHAR_TO_ANSI(Before->GetFullName()) : "(none)",
            PendingBefore ? TCHAR_TO_ANSI(PendingBefore->GetFullName()) : "(none)",
            Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
            Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)" );
    }
}

static UBOOL XboxWeaponCycleProofSmokeEnabled()
{
    static INT Enabled = -1;
    if( Enabled < 0 )
        Enabled = GetFileAttributesA( "D:\\XboxWeaponCycleProofSmoke.ini" ) != 0xFFFFFFFF;
    return Enabled;
}

static UBOOL XboxWeaponCycleProofSetupOnly()
{
    HANDLE File = CreateFileA( "D:\\XboxWeaponCycleProofSmoke.ini", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
        return 0;

    char Buffer[128];
    DWORD Read = 0;
    appMemzero( Buffer, sizeof(Buffer) );
    ReadFile( File, Buffer, sizeof(Buffer)-1, &Read, NULL );
    CloseHandle( File );
    return appStrstr( Buffer, "SetupOnly=1" ) != NULL;
}

static void XboxWeaponCycleProofSetStage( INT Stage, DOUBLE Now, APlayerPawn* Player )
{
    GXboxWeaponCycleProofStage = Stage;
    GXboxWeaponCycleProofStageTime = Now;
    GXboxWeaponCycleProofBefore = Player ? Player->Weapon : NULL;
    GXboxWeaponCycleProofStageLogged = 0;
}

static void XboxWeaponCycleProofCheckChanged( const char* Label, APlayerPawn* Player )
{
    UBOOL Changed = Player && Player->Weapon && Player->Weapon != GXboxWeaponCycleProofBefore;
    if( Changed )
        GXboxWeaponCycleProofPasses++;
    else
        GXboxWeaponCycleProofFailures++;

    GXboxLog.Write( "XWHEELPROOF CHECK %s result=%s before=%s current=%s pending=%s pass=%d fail=%d",
        Label,
        Changed ? "PASS" : "FAIL",
        GXboxWeaponCycleProofBefore ? TCHAR_TO_ANSI(GXboxWeaponCycleProofBefore->GetFullName()) : "(none)",
        Player && Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
        Player && Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)",
        GXboxWeaponCycleProofPasses,
        GXboxWeaponCycleProofFailures );
}

static void XboxWeaponCycleProofSmokeApply( UXboxViewport* Viewport, XINPUT_GAMEPAD& Pad )
{
    if( !XboxWeaponCycleProofSmokeEnabled() || !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    UXboxClient* Client = (UXboxClient*)Viewport->GetOuter();
    if( !Client || !Player->Level || !Player->Level->Game || Player->Health <= 0 )
        return;

    DOUBLE Now = appSeconds();
    if( GXboxWeaponCycleProofLevel != Player->Level || GXboxWeaponCycleProofPlayer != Player )
    {
        GXboxWeaponCycleProofLevel = Player->Level;
        GXboxWeaponCycleProofPlayer = Player;
        GXboxWeaponCycleProofStage = 0;
        GXboxWeaponCycleProofIteration = 0;
        GXboxWeaponCycleProofPasses = 0;
        GXboxWeaponCycleProofFailures = 0;
        GXboxWeaponCycleProofStageTime = Now;
        GXboxWeaponCycleProofBefore = NULL;
        GXboxWeaponCycleProofStageLogged = 0;
    }

    // Setup-only mode must never suppress or synthesize controller input once
    // the arsenal has been granted.
    if( GXboxWeaponCycleProofStage == 13 )
        return;

    // This marker-gated proof feeds the same physical-button state machine used by a controller.
    appMemzero( &Pad, sizeof(Pad) );

    if( GXboxWeaponCycleProofStage == 0 )
    {
        Client->ButtonActionWhite = XCA_PrevWeaponWheel;
        Client->ButtonActionBlack = XCA_NextWeaponWheel;
        Client->StickLayout = XSL_Default;
        Player->bCheatsEnabled = 1;
        Player->ScriptConsoleExec( TEXT("God"), *GLog, Player );
        UBOOL LoadedHandled = Player->ScriptConsoleExec( TEXT("Loaded"), *GLog, Player );
        UBOOL EnforcerHandled = Player->ScriptConsoleExec( TEXT("GetWeapon Botpack.Enforcer"), *GLog, Player );
        GXboxLog.Write( "XWHEELPROOF setup loaded=%d enforcer=%d current=%s pending=%s",
            LoadedHandled ? 1 : 0,
            EnforcerHandled ? 1 : 0,
            Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
            Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)" );
        if( XboxWeaponCycleProofSetupOnly() )
        {
            GXboxLog.Write( "XWHEELPROOF SETUP-ONLY COMPLETE" );
            XboxWeaponCycleProofSetStage( 13, Now, Player );
            return;
        }
        XboxWeaponCycleProofSetStage( 1, Now, Player );
        return;
    }

    DOUBLE StageSeconds = Now - GXboxWeaponCycleProofStageTime;
    switch( GXboxWeaponCycleProofStage )
    {
        case 1:
            if( StageSeconds >= 2.5 )
            {
                GXboxLog.Write( "XWHEELPROOF BASELINE current=%s pending=%s",
                    Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
                    Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)" );
                XboxWeaponCycleProofSetStage( 2, Now, Player );
            }
            break;

        case 2:
            if( StageSeconds < 0.12 )
                Pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] = 255;
            else
                XboxWeaponCycleProofSetStage( 3, Now, Player );
            break;

        case 3:
            if( StageSeconds >= 1.5 )
            {
                XboxWeaponCycleProofCheckChanged( "next", Player );
                XboxWeaponCycleProofSetStage( 4, Now, Player );
            }
            break;

        case 4:
            if( StageSeconds >= 4.0 )
                XboxWeaponCycleProofSetStage( 5, Now, Player );
            break;

        case 5:
            if( StageSeconds < 0.12 )
                Pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE] = 255;
            else
                XboxWeaponCycleProofSetStage( 6, Now, Player );
            break;

        case 6:
            if( StageSeconds >= 1.5 )
            {
                XboxWeaponCycleProofCheckChanged( "prev", Player );
                XboxWeaponCycleProofSetStage( 7, Now, Player );
            }
            break;

        case 7:
            if( StageSeconds >= 4.0 )
            {
                GXboxWeaponCycleProofIteration = 0;
                XboxWeaponCycleProofSetStage( 8, Now, Player );
            }
            break;

        case 8:
            if( StageSeconds < 0.12 )
            {
                INT Button = GXboxWeaponCycleProofIteration < 4 ? XINPUT_GAMEPAD_BLACK : XINPUT_GAMEPAD_WHITE;
                Pad.bAnalogButtons[Button] = 255;
            }
            else
                XboxWeaponCycleProofSetStage( 9, Now, Player );
            break;

        case 9:
            if( StageSeconds >= 1.25 )
            {
                const char* Label = GXboxWeaponCycleProofIteration < 4 ? "repeat-next" : "repeat-prev";
                GXboxLog.Write( "XWHEELPROOF repeat iteration=%d direction=%s",
                    GXboxWeaponCycleProofIteration + 1,
                    GXboxWeaponCycleProofIteration < 4 ? "next" : "prev" );
                XboxWeaponCycleProofCheckChanged( Label, Player );
                GXboxWeaponCycleProofIteration++;
                if( GXboxWeaponCycleProofIteration < 8 )
                    XboxWeaponCycleProofSetStage( 8, Now, Player );
                else
                {
                    Player->ScriptConsoleExec( TEXT("GetWeapon Botpack.Enforcer"), *GLog, Player );
                    XboxWeaponCycleProofSetStage( 10, Now, Player );
                }
            }
            break;

        case 10:
            if( StageSeconds >= 1.5 )
            {
                GXboxLog.Write( "XWHEELPROOF WHEEL-BASELINE current=%s pending=%s",
                    Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
                    Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)" );
                XboxWeaponCycleProofSetStage( 11, Now, Player );
            }
            break;

        case 11:
            if( !GXboxWeaponCycleProofStageLogged )
            {
                GXboxWeaponCycleProofStageLogged = 1;
                GXboxLog.Write( "XWHEELPROOF WHEEL-HOLD target=ShockRifle" );
            }
            if( StageSeconds < 4.0 )
            {
                Pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] = 255;
                Pad.sThumbRX = -30000;
                Pad.sThumbRY = -12000;
            }
            else
                XboxWeaponCycleProofSetStage( 12, Now, Player );
            break;

        case 12:
            if( StageSeconds >= 1.5 )
            {
                UBOOL IsShock = Player->Weapon && appStricmp( Player->Weapon->GetClass()->GetName(), TEXT("ShockRifle") ) == 0;
                if( IsShock )
                    GXboxWeaponCycleProofPasses++;
                else
                    GXboxWeaponCycleProofFailures++;
                GXboxLog.Write( "XWHEELPROOF CHECK wheel result=%s current=%s pending=%s pass=%d fail=%d",
                    IsShock ? "PASS" : "FAIL",
                    Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
                    Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)",
                    GXboxWeaponCycleProofPasses,
                    GXboxWeaponCycleProofFailures );
                GXboxLog.Write( "XWHEELPROOF COMPLETE result=%s pass=%d fail=%d current=%s",
                    GXboxWeaponCycleProofFailures == 0 ? "PASS" : "FAIL",
                    GXboxWeaponCycleProofPasses,
                    GXboxWeaponCycleProofFailures,
                    Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)" );
                XboxWeaponCycleProofSetStage( 13, Now, Player );
            }
            break;

        default:
            break;
    }
}

enum EXboxSplitControlsProofTest
{
    XSCT_MoveStick,
    XSCT_LookStick,
    XSCT_AJump,
    XSCT_BDuck,
    XSCT_XUse,
    XSCT_YDodge,
    XSCT_LTAltFire,
    XSCT_RTFire,
    XSCT_WhitePrevTap,
    XSCT_BlackNextTap,
    XSCT_WhiteWheel,
    XSCT_BlackWheel,
    XSCT_BackScoreboard,
    XSCT_RightThumbCenter,
    XSCT_DpadUp,
    XSCT_DpadDown,
    XSCT_DpadLeft,
    XSCT_DpadRight,
    XSCT_Start,
    XSCT_Count
};

enum EXboxSplitControlsProofPhase
{
    XSCP_Waiting,
    XSCP_Prepare,
    XSCP_Press,
    XSCP_Release,
    XSCP_StartHold,
    XSCP_StartClose,
    XSCP_StartRelease,
    XSCP_Complete
};

struct FXboxSplitControlsProofSnapshot
{
    APlayerPawn* Player;
    FVector Location;
    FVector Velocity;
    FVector Acceleration;
    FRotator ViewRotation;
    FLOAT BaseY;
    FLOAT Strafe;
    FLOAT Turn;
    FLOAT LookUp;
    BYTE Duck;
    BYTE Fire;
    BYTE AltFire;
    BYTE SnapLevel;
    BYTE DodgeDir;
    UBOOL PressedJump;
    UBOOL JustFired;
    UBOOL JustAltFired;
    UBOOL ShowScores;
    AWeapon* Weapon;
    AWeapon* PendingWeapon;
    AInventory* SelectedItem;
    UBOOL SelectedActive;
};

static ULevel* GXboxSplitControlsProofLevel = NULL;
static INT GXboxSplitControlsProofPhase = XSCP_Waiting;
static INT GXboxSplitControlsProofTest = XSCT_MoveStick;
static INT GXboxSplitControlsProofTarget = 0;
static INT GXboxSplitControlsProofPasses = 0;
static INT GXboxSplitControlsProofFailures = 0;
static DOUBLE GXboxSplitControlsProofPhaseTime = 0.0;
static UBOOL GXboxSplitControlsProofObserved = 0;
static UBOOL GXboxSplitControlsProofWheelOpened = 0;
static UBOOL GXboxSplitControlsProofIsolationFailed = 0;
static UBOOL GXboxSplitControlsProofStartHoldPassed = 0;
static UBOOL GXboxSplitControlsProofRunnerInput = 0;
static FLOAT GXboxSplitControlsProofStartLevelTime = 0.0f;
static FVector GXboxSplitControlsProofRunnerLocation;
static FXboxSplitControlsProofSnapshot GXboxSplitControlsProofSnapshots[4];
static INT GXboxSplitControlsProofSavedActions[XCB_Count];
static INT GXboxSplitControlsProofSavedPreset = 0;
static INT GXboxSplitControlsProofSavedButtonLayout = 0;
static INT GXboxSplitControlsProofSavedStickLayout = XSL_Default;
static UBOOL GXboxSplitControlsProofSavedClient = 0;

static const char* XboxSplitControlsProofTestName( INT Test )
{
    static const char* Names[XSCT_Count] =
    {
        "LEFT STICK MOVE",
        "RIGHT STICK LOOK",
        "A JUMP",
        "B DUCK",
        "X USE",
        "Y DODGE",
        "LT ALT FIRE",
        "RT FIRE",
        "WHITE PREV TAP",
        "BLACK NEXT TAP",
        "WHITE WEAPON WHEEL",
        "BLACK WEAPON WHEEL",
        "BACK SCOREBOARD",
        "RIGHT THUMB CENTER VIEW",
        "DPAD UP WEAPON",
        "DPAD DOWN WEAPON",
        "DPAD LEFT WEAPON",
        "DPAD RIGHT WEAPON",
        "START PAUSE"
    };
    return Names[Clamp<INT>(Test, 0, XSCT_Count-1)];
}

static UXboxClient* XboxSplitControlsProofClient( UXboxViewport* Viewport )
{
    return Viewport ? Cast<UXboxClient>( Viewport->GetOuter() ) : NULL;
}

static UXboxViewport* XboxSplitControlsProofViewport( UXboxClient* Client, INT Slot )
{
    if( !Client || Slot < 0 || Slot >= Client->Viewports.Num() )
        return NULL;
    return Cast<UXboxViewport>( Client->Viewports(Slot) );
}

static APlayerPawn* XboxSplitControlsProofPlayer( UXboxClient* Client, INT Slot )
{
    UXboxViewport* Viewport = XboxSplitControlsProofViewport( Client, Slot );
    return Viewport ? Viewport->Actor : NULL;
}

static UBOOL XboxSplitControlsProofReady( UXboxClient* Client, ULevel*& Level )
{
    Level = NULL;
    if( !GXboxSplitActive || !Client || Client->Viewports.Num() < 4 )
        return 0;

    for( INT i=0; i<4; i++ )
    {
        UXboxViewport* Viewport = XboxSplitControlsProofViewport( Client, i );
        if( !Viewport || Viewport->bXboxSplitDummy || !Viewport->Actor || !Viewport->Input )
            return 0;
        if( i == 0 )
            Level = Viewport->Actor->GetLevel();
        else if( Viewport->Actor->GetLevel() != Level )
            return 0;
    }
    return Level && Level->GetLevelInfo() && Level->GetLevelInfo()->Game;
}

static AInventory* XboxSplitControlsProofFindInventory( APlayerPawn* Player, const TCHAR* ClassName )
{
    for( AInventory* Item=Player ? Player->Inventory : NULL; Item; Item=Item->Inventory )
        if( Item->GetClass() && appStricmp(Item->GetClass()->GetName(), ClassName) == 0 )
            return Item;
    return NULL;
}

static AInventory* XboxSplitControlsProofEnsureJumpBoots( APlayerPawn* Player )
{
    if( !Player || !Player->XLevel )
        return NULL;

    AInventory* Boots = XboxSplitControlsProofFindInventory( Player, TEXT("UT_JumpBoots") );
    if( !Boots )
    {
        UClass* BootsClass = UObject::StaticLoadClass(
            AInventory::StaticClass(), NULL, TEXT("Botpack.UT_JumpBoots"), NULL, LOAD_NoWarn, NULL );
        if( BootsClass )
            Boots = Cast<AInventory>( Player->XLevel->SpawnActor(
                BootsClass, NAME_None, Player, NULL, Player->Location, Player->Rotation, NULL, 1 ) );
        if( Boots )
        {
            UFunction* GiveTo = Boots->FindFunction( FName(TEXT("GiveTo"), FNAME_Find) );
            if( GiveTo )
            {
                struct FGiveToParms { APawn* Other; } Parms;
                Parms.Other = Player;
                Boots->ProcessEvent( GiveTo, &Parms );
            }
        }
    }

    if( Boots )
    {
        Boots->GotoState( FName(TEXT("DeActivated"), FNAME_Find) );
        Boots->bActive = 0;
        Boots->bActivatable = 1;
        Player->SelectedItem = Boots;
    }
    return Boots;
}

static void XboxSplitControlsProofCapture( UXboxClient* Client )
{
    for( INT i=0; i<4; i++ )
    {
        APlayerPawn* Player = XboxSplitControlsProofPlayer( Client, i );
        FXboxSplitControlsProofSnapshot& Snapshot = GXboxSplitControlsProofSnapshots[i];
        appMemzero( &Snapshot, sizeof(Snapshot) );
        Snapshot.Player = Player;
        if( !Player )
            continue;
        Snapshot.Location = Player->Location;
        Snapshot.Velocity = Player->Velocity;
        Snapshot.Acceleration = Player->Acceleration;
        Snapshot.ViewRotation = Player->ViewRotation;
        Snapshot.BaseY = Player->aBaseY;
        Snapshot.Strafe = Player->aStrafe;
        Snapshot.Turn = Player->aTurn;
        Snapshot.LookUp = Player->aLookUp;
        Snapshot.Duck = Player->bDuck;
        Snapshot.Fire = Player->bFire;
        Snapshot.AltFire = Player->bAltFire;
        Snapshot.SnapLevel = Player->bSnapLevel;
        Snapshot.DodgeDir = Player->DodgeDir;
        Snapshot.PressedJump = Player->bPressedJump;
        Snapshot.JustFired = Player->bJustFired;
        Snapshot.JustAltFired = Player->bJustAltFired;
        Snapshot.ShowScores = Player->bShowScores;
        Snapshot.Weapon = Player->Weapon;
        Snapshot.PendingWeapon = Player->PendingWeapon;
        Snapshot.SelectedItem = Player->SelectedItem;
        Snapshot.SelectedActive = Player->SelectedItem ? Player->SelectedItem->bActive : 0;
    }
}

static UBOOL XboxSplitControlsProofWeaponTest( INT Test )
{
    return Test == XSCT_WhitePrevTap || Test == XSCT_BlackNextTap
        || Test == XSCT_WhiteWheel || Test == XSCT_BlackWheel
        || Test == XSCT_DpadUp || Test == XSCT_DpadDown
        || Test == XSCT_DpadLeft || Test == XSCT_DpadRight;
}

static void XboxSplitControlsProofRestoreClient( UXboxClient* Client )
{
    if( !Client || !GXboxSplitControlsProofSavedClient )
        return;
    for( INT i=0; i<XCB_Count; i++ )
        XboxControlSetButtonAction( Client, i, GXboxSplitControlsProofSavedActions[i] );
    Client->ControlPreset = GXboxSplitControlsProofSavedPreset;
    Client->ButtonLayout = GXboxSplitControlsProofSavedButtonLayout;
    Client->StickLayout = GXboxSplitControlsProofSavedStickLayout;
    for( INT Port=0; Port<4; Port++ )
    {
        INT ProfileIndex = GXboxSplitReadySlots[Port].Profile;
        if( ProfileIndex >= 0 && ProfileIndex < XBOX_PROFILE_COUNT && GXboxProfiles[ProfileIndex].Created )
            XboxSplitLoadProfileControls( Port, ProfileIndex, Client );
    }
    GXboxSplitControlsProofSavedClient = 0;
}

static UBOOL XboxSplitControlsProofProfilePersistence( UXboxClient* Client )
{
    UXboxViewport* PlayerOneViewport = XboxSplitControlsProofViewport( Client, 0 );
    UXboxViewport* PlayerTwoViewport = XboxSplitControlsProofViewport( Client, 1 );
    INT PlayerOneProfile = GXboxSplitReadySlots[0].Profile;
    INT PlayerTwoProfile = GXboxSplitReadySlots[1].Profile;
    if( !Client || !PlayerOneViewport || !PlayerTwoViewport
    ||  PlayerOneProfile < 0 || PlayerOneProfile >= XBOX_PROFILE_COUNT
    ||  PlayerTwoProfile < 0 || PlayerTwoProfile >= XBOX_PROFILE_COUNT
    ||  PlayerOneProfile == PlayerTwoProfile
    ||  !GXboxProfiles[PlayerOneProfile].Created
    ||  !GXboxProfiles[PlayerTwoProfile].Created )
    {
        GXboxLog.Write( "XPROFILE OWNERSHIP PROOF FAIL reason=invalid-profile-assignment p1=%d p2=%d", PlayerOneProfile + 1, PlayerTwoProfile + 1 );
        return 0;
    }

    TCHAR PlayerOneSection[32];
    TCHAR PlayerTwoSection[32];
    XboxProfileSectionName( PlayerOneProfile, PlayerOneSection, ARRAY_COUNT(PlayerOneSection) );
    XboxProfileSectionName( PlayerTwoProfile, PlayerTwoSection, ARRAY_COUNT(PlayerTwoSection) );

    FLOAT PlayerOneLook = XboxProfileConfigFloat( PlayerOneSection, TEXT("LookSensitivity"), 100.0f );
    FLOAT PlayerOneMove = XboxProfileConfigFloat( PlayerOneSection, TEXT("MoveSensitivity"), 100.0f );
    FLOAT PlayerOneDeadZone = XboxProfileConfigFloat( PlayerOneSection, TEXT("DeadZone"), 0.20f );
    INT PlayerOneInvert = XboxProfileConfigInt( PlayerOneSection, TEXT("InvertY"), 0 );
    INT PlayerOneStick = XboxProfileConfigInt( PlayerOneSection, TEXT("StickLayout"), XSL_Default );
    INT PlayerOneActionA = XboxProfileConfigInt( PlayerOneSection, TEXT("ButtonAction0"), XCA_Jump );
    INT PlayerOneHand = XboxProfileConfigInt( PlayerOneSection, TEXT("WeaponHand"), 0 );
    INT PlayerOneAutoSwitch = XboxProfileConfigInt( PlayerOneSection, TEXT("AutoSwitch"), 1 );

    INT OriginalPreset = XboxProfileConfigInt( PlayerTwoSection, TEXT("ControlPreset"), 0 );
    FLOAT OriginalLook = XboxProfileConfigFloat( PlayerTwoSection, TEXT("LookSensitivity"), 100.0f );
    FLOAT OriginalMove = XboxProfileConfigFloat( PlayerTwoSection, TEXT("MoveSensitivity"), 100.0f );
    FLOAT OriginalDeadZone = XboxProfileConfigFloat( PlayerTwoSection, TEXT("DeadZone"), 0.20f );
    INT OriginalInvert = XboxProfileConfigInt( PlayerTwoSection, TEXT("InvertY"), 0 );
    INT OriginalStick = XboxProfileConfigInt( PlayerTwoSection, TEXT("StickLayout"), XSL_Default );
    INT OriginalActionA = XboxProfileConfigInt( PlayerTwoSection, TEXT("ButtonAction0"), XCA_Jump );
    INT OriginalHand = XboxProfileConfigInt( PlayerTwoSection, TEXT("WeaponHand"), 0 );
    INT OriginalAutoSwitch = XboxProfileConfigInt( PlayerTwoSection, TEXT("AutoSwitch"), 1 );

    FLOAT TestLook = OriginalLook < 150.0f ? 175.0f : 35.0f;
    FLOAT TestMove = OriginalMove < 125.0f ? 150.0f : 50.0f;
    FLOAT TestDeadZone = OriginalDeadZone < 0.25f ? 0.35f : 0.10f;
    INT TestInvert = OriginalInvert ? 0 : 1;
    INT TestStick = XboxMenuWrapInt( XboxStickLayoutClamp(OriginalStick), 1, XSL_LegacySouthpaw + 1 );
    INT TestActionA = OriginalActionA == XCA_Use ? XCA_Jump : XCA_Use;
    INT TestHand = XboxMenuWrapInt( OriginalHand, 1, ARRAY_COUNT(GXboxWeaponHands) );
    INT TestAutoSwitch = OriginalAutoSwitch ? 0 : 1;

    XboxProfileLoadControlsForContext( PlayerTwoViewport );
    Client->ControlPreset = -1;
    Client->ScaleRUV = TestLook;
    Client->ScaleXYZ = TestMove;
    Client->DeadZone = TestDeadZone;
    Client->InvertVertical = TestInvert != 0;
    Client->StickLayout = TestStick;
    XboxControlSetButtonAction( Client, XCB_A, TestActionA );
    XboxMenuSetWeaponHand( PlayerTwoViewport->Actor, TestHand );
    if( PlayerTwoViewport->Actor )
    {
        PlayerTwoViewport->Actor->bNeverAutoSwitch = TestAutoSwitch == 0;
        PlayerTwoViewport->Actor->bNeverSwitchOnPickup = PlayerTwoViewport->Actor->bNeverAutoSwitch;
    }
    XboxProfileSaveControlsForContext( PlayerTwoViewport );

    UBOOL PlayerOneUnchanged =
        Abs(XboxProfileConfigFloat(PlayerOneSection, TEXT("LookSensitivity"), 0.0f) - PlayerOneLook) < 0.01f
    &&  Abs(XboxProfileConfigFloat(PlayerOneSection, TEXT("MoveSensitivity"), 0.0f) - PlayerOneMove) < 0.01f
    &&  Abs(XboxProfileConfigFloat(PlayerOneSection, TEXT("DeadZone"), 0.0f) - PlayerOneDeadZone) < 0.001f
    &&  XboxProfileConfigInt(PlayerOneSection, TEXT("InvertY"), -1) == PlayerOneInvert
    &&  XboxProfileConfigInt(PlayerOneSection, TEXT("StickLayout"), -1) == PlayerOneStick
    &&  XboxProfileConfigInt(PlayerOneSection, TEXT("ButtonAction0"), -1) == PlayerOneActionA
    &&  XboxProfileConfigInt(PlayerOneSection, TEXT("WeaponHand"), -1) == PlayerOneHand
    &&  XboxProfileConfigInt(PlayerOneSection, TEXT("AutoSwitch"), -1) == PlayerOneAutoSwitch;

    UBOOL PlayerTwoSaved =
        XboxProfileConfigInt(PlayerTwoSection, TEXT("ControlPreset"), 0) == -1
    &&  Abs(XboxProfileConfigFloat(PlayerTwoSection, TEXT("LookSensitivity"), 0.0f) - TestLook) < 0.01f
    &&  Abs(XboxProfileConfigFloat(PlayerTwoSection, TEXT("MoveSensitivity"), 0.0f) - TestMove) < 0.01f
    &&  Abs(XboxProfileConfigFloat(PlayerTwoSection, TEXT("DeadZone"), 0.0f) - TestDeadZone) < 0.001f
    &&  XboxProfileConfigInt(PlayerTwoSection, TEXT("InvertY"), -1) == TestInvert
    &&  XboxProfileConfigInt(PlayerTwoSection, TEXT("StickLayout"), -1) == TestStick
    &&  XboxProfileConfigInt(PlayerTwoSection, TEXT("ButtonAction0"), -1) == TestActionA
    &&  XboxProfileConfigInt(PlayerTwoSection, TEXT("WeaponHand"), -1) == TestHand
    &&  XboxProfileConfigInt(PlayerTwoSection, TEXT("AutoSwitch"), -1) == TestAutoSwitch;

    XboxProfileLoadControlsForContext( PlayerTwoViewport );
    UBOOL PlayerTwoReloaded =
        Client->ControlPreset == -1
    &&  Abs(Client->ScaleRUV - TestLook) < 0.01f
    &&  Abs(Client->ScaleXYZ - TestMove) < 0.01f
    &&  Abs(Client->DeadZone - TestDeadZone) < 0.001f
    &&  (Client->InvertVertical ? 1 : 0) == TestInvert
    &&  Client->StickLayout == TestStick
    &&  XboxControlButtonAction(Client, XCB_A) == TestActionA
    &&  XboxMenuWeaponHandIndex(PlayerTwoViewport->Actor) == TestHand
    &&  (PlayerTwoViewport->Actor && !PlayerTwoViewport->Actor->bNeverAutoSwitch ? 1 : 0) == TestAutoSwitch;

    XboxProfileSetInt( PlayerTwoSection, TEXT("ControlPreset"), OriginalPreset );
    XboxProfileSetFloat( PlayerTwoSection, TEXT("LookSensitivity"), OriginalLook );
    XboxProfileSetFloat( PlayerTwoSection, TEXT("MoveSensitivity"), OriginalMove );
    XboxProfileSetFloat( PlayerTwoSection, TEXT("DeadZone"), OriginalDeadZone );
    XboxProfileSetInt( PlayerTwoSection, TEXT("InvertY"), OriginalInvert );
    XboxProfileSetInt( PlayerTwoSection, TEXT("StickLayout"), OriginalStick );
    XboxProfileSetInt( PlayerTwoSection, TEXT("ButtonAction0"), OriginalActionA );
    XboxProfileSetInt( PlayerTwoSection, TEXT("WeaponHand"), OriginalHand );
    XboxProfileSetInt( PlayerTwoSection, TEXT("AutoSwitch"), OriginalAutoSwitch );
    if( GConfig )
        GConfig->Flush( 0, TEXT("User.ini") );
    XboxSplitLoadProfileControls( 1, PlayerTwoProfile, Client );
    XboxProfileApplyPlayerOptionsForPort( PlayerTwoViewport->Actor, 1 );
    XboxProfileLoadControlsForContext( PlayerOneViewport );

    UBOOL Passed = PlayerOneUnchanged && PlayerTwoSaved && PlayerTwoReloaded;
    GXboxLog.Write( "XPROFILE OWNERSHIP PROOF %s ownerViewport=2 ownerSlot=%d protectedViewport=1 protectedSlot=%d p1Unchanged=%d p2Saved=%d p2Reloaded=%d",
        Passed ? "PASS" : "FAIL",
        PlayerTwoProfile + 1,
        PlayerOneProfile + 1,
        PlayerOneUnchanged ? 1 : 0,
        PlayerTwoSaved ? 1 : 0,
        PlayerTwoReloaded ? 1 : 0 );
    return Passed;
}

static void XboxSplitControlsProofSetup( UXboxClient* Client, ULevel* Level, DOUBLE Now )
{
    GXboxSplitControlsProofPasses = 0;
    GXboxSplitControlsProofFailures = 0;
    GXboxSplitControlsProofTarget = 0;
    GXboxSplitControlsProofTest = XboxSplitControlsOnlineProofEnabled() ? XSCT_Start : XSCT_MoveStick;
    GXboxSplitControlsProofObserved = 0;
    GXboxSplitControlsProofWheelOpened = 0;
    GXboxSplitControlsProofIsolationFailed = 0;
    GXboxSplitControlsProofStartHoldPassed = 0;
    GXboxSplitControlsProofRunnerInput = 0;

    if( !GXboxSplitControlsProofSavedClient )
    {
        for( INT i=0; i<XCB_Count; i++ )
            GXboxSplitControlsProofSavedActions[i] = XboxControlButtonAction( Client, i );
        GXboxSplitControlsProofSavedPreset = Client->ControlPreset;
        GXboxSplitControlsProofSavedButtonLayout = Client->ButtonLayout;
        GXboxSplitControlsProofSavedStickLayout = Client->StickLayout;
        GXboxSplitControlsProofSavedClient = 1;
    }

    if( !XboxSplitControlsProofProfilePersistence(Client) )
        GXboxSplitControlsProofFailures++;

    const FXboxControlPreset& DefaultPreset = GXboxControlPresets[0];
    for( INT i=0; i<XCB_Count; i++ )
        XboxControlSetButtonAction( Client, i, DefaultPreset.Actions[i] );
    Client->StickLayout = XSL_Default;
    for( INT Port=0; Port<4; Port++ )
    {
        FXboxRuntimeProfileControls& Controls = GXboxSplitProfileControls[Port];
        if( !Controls.Valid )
            continue;
        Controls.LookSensitivity = 100.0f;
        Controls.MoveSensitivity = 100.0f;
        Controls.DeadZone = 0.20f;
        Controls.InvertY = 0;
        Controls.StickLayout = XSL_Default;
        for( INT Button=0; Button<XCB_Count; Button++ )
            Controls.Actions[Button] = DefaultPreset.Actions[Button];
    }

    GXboxLog.Write( "XSPLIT CONTROLS BEGIN mode=%s map=%s net=%d activeMask=0x%X viewports=%d",
        XboxSplitControlsOnlineProofEnabled() ? "ONLINE" : "LOCAL",
        TCHAR_TO_ANSI(*Level->URL.Map),
        (INT)Level->GetLevelInfo()->NetMode,
        GXboxSplitActiveMask,
        Client->Viewports.Num() );

    for( INT i=0; i<4; i++ )
    {
        UXboxViewport* Viewport = XboxSplitControlsProofViewport( Client, i );
        APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
        UBOOL Unique = Viewport && Player && Viewport->Input
            && Viewport->ControllerPort == i
            && Viewport->Input->Viewport == Viewport;
        for( INT j=0; Unique && j<i; j++ )
        {
            UXboxViewport* Other = XboxSplitControlsProofViewport( Client, j );
            if( Other == Viewport || (Other && Other->Actor == Player) || (Other && Other->Input == Viewport->Input) )
                Unique = 0;
        }

        if( Unique )
            GXboxSplitControlsProofPasses++;
        else
            GXboxSplitControlsProofFailures++;
        GXboxLog.Write( "XSPLIT OWNERSHIP %s slot=%d port=%d viewport=0x%08X actor=0x%08X input=0x%08X inputViewport=0x%08X",
            Unique ? "PASS" : "FAIL",
            i + 1,
            Viewport ? Viewport->ControllerPort : -1,
            (DWORD)Viewport,
            (DWORD)Player,
            Viewport ? (DWORD)Viewport->Input : 0,
            (Viewport && Viewport->Input) ? (DWORD)Viewport->Input->Viewport : 0 );

        if( !Player )
            continue;
        Player->bCheatsEnabled = 1;
        Player->bNeverAutoSwitch = 1;
        Player->bAutoActivate = 0;
        Player->ScriptConsoleExec( TEXT("God"), *GLog, Player );
        Player->ScriptConsoleExec( TEXT("Loaded"), *GLog, Player );
        Player->ScriptConsoleExec( TEXT("GetWeapon Botpack.Enforcer"), *GLog, Player );
        AInventory* Boots = XboxSplitControlsProofEnsureJumpBoots( Player );
        GXboxLog.Write( "XSPLIT USE SETUP slot=%d boots=0x%08X selected=0x%08X active=%d",
            i + 1, (DWORD)Boots, (DWORD)Player->SelectedItem, Boots && Boots->bActive ? 1 : 0 );
    }

    GXboxSplitControlsProofPhase = XSCP_Waiting;
    GXboxSplitControlsProofPhaseTime = Now;
}

static void XboxSplitControlsProofPrepareTest( UXboxClient* Client, DOUBLE Now )
{
    if( GXboxMenu.Active )
        XboxMenuClose( XboxSplitControlsProofViewport(Client, GXboxMenuOwnerViewport) );

    for( INT i=0; i<4; i++ )
    {
        APlayerPawn* Player = XboxSplitControlsProofPlayer( Client, i );
        if( !Player )
            continue;
        Player->bDuck = 0;
        Player->bFire = 0;
        Player->bAltFire = 0;
        Player->bSnapLevel = 0;
        Player->bPressedJump = 0;
        Player->bJustFired = 0;
        Player->bJustAltFired = 0;
        Player->bShowScores = 0;
        Player->aBaseY = 0.0f;
        Player->aStrafe = 0.0f;
        Player->aTurn = 0.0f;
        Player->aLookUp = 0.0f;
        Player->Acceleration = FVector(0,0,0);
        GXboxWeaponWheelActive[i] = 0;
    }

    APlayerPawn* Target = XboxSplitControlsProofPlayer( Client, GXboxSplitControlsProofTarget );
    if( Target )
    {
        if( XboxSplitControlsProofWeaponTest(GXboxSplitControlsProofTest) )
            Target->ScriptConsoleExec( TEXT("GetWeapon Botpack.Enforcer"), *GLog, Target );
        if( GXboxSplitControlsProofTest == XSCT_XUse )
            XboxSplitControlsProofEnsureJumpBoots( Target );
        if( GXboxSplitControlsProofTest == XSCT_YDodge )
        {
            Target->setPhysics( PHYS_Walking );
            Target->Velocity = FVector(0,0,0);
            Target->DodgeDir = DODGE_None;
        }
        if( GXboxSplitControlsProofTest == XSCT_Start )
        {
            INT Runner = (GXboxSplitControlsProofTarget + 1) & 3;
            APlayerPawn* RunnerPlayer = XboxSplitControlsProofPlayer( Client, Runner );
            if( RunnerPlayer )
                RunnerPlayer->Velocity = FVector(0,0,0);
        }
    }

    GXboxSplitControlsProofObserved = 0;
    GXboxSplitControlsProofWheelOpened = 0;
    GXboxSplitControlsProofIsolationFailed = 0;
    GXboxSplitControlsProofStartHoldPassed = 0;
    GXboxSplitControlsProofRunnerInput = 0;
    GXboxSplitControlsProofPhase = XSCP_Prepare;
    GXboxSplitControlsProofPhaseTime = Now;
    GXboxLog.Write( "XSPLIT CONTROL PREP slot=%d port=%d test=%s",
        GXboxSplitControlsProofTarget + 1,
        XboxSplitControlsProofViewport(Client, GXboxSplitControlsProofTarget)
            ? XboxSplitControlsProofViewport(Client, GXboxSplitControlsProofTarget)->ControllerPort : -1,
        XboxSplitControlsProofTestName(GXboxSplitControlsProofTest) );
}

static UBOOL XboxSplitControlsProofOtherPlayersUnchanged( UXboxClient* Client, INT Test )
{
    for( INT i=0; i<4; i++ )
    {
        if( i == GXboxSplitControlsProofTarget )
            continue;
        APlayerPawn* Player = XboxSplitControlsProofPlayer( Client, i );
        const FXboxSplitControlsProofSnapshot& Snapshot = GXboxSplitControlsProofSnapshots[i];
        if( Player != Snapshot.Player || !Player )
            return 0;

        if( Test == XSCT_MoveStick && (Player->Acceleration-Snapshot.Acceleration).SizeSquared() > 1.0f )
            return 0;
        if( Test == XSCT_LookStick
        && (Player->ViewRotation.Yaw != Snapshot.ViewRotation.Yaw || Player->ViewRotation.Pitch != Snapshot.ViewRotation.Pitch) )
            return 0;
        if( Test == XSCT_AJump && Player->bPressedJump != Snapshot.PressedJump )
            return 0;
        if( Test == XSCT_BDuck && Player->bDuck != Snapshot.Duck )
            return 0;
        if( Test == XSCT_XUse && Player->SelectedItem && Player->SelectedItem->bActive != Snapshot.SelectedActive )
            return 0;
        if( Test == XSCT_YDodge && Player->DodgeDir != Snapshot.DodgeDir )
            return 0;
        if( Test == XSCT_LTAltFire && (Player->bAltFire != Snapshot.AltFire || Player->bJustAltFired != Snapshot.JustAltFired) )
            return 0;
        if( Test == XSCT_RTFire && (Player->bFire != Snapshot.Fire || Player->bJustFired != Snapshot.JustFired) )
            return 0;
        if( XboxSplitControlsProofWeaponTest(Test)
        && (Player->Weapon != Snapshot.Weapon || Player->PendingWeapon != Snapshot.PendingWeapon) )
            return 0;
        if( Test == XSCT_BackScoreboard && Player->bShowScores != Snapshot.ShowScores )
            return 0;
        if( Test == XSCT_RightThumbCenter && Player->bSnapLevel != Snapshot.SnapLevel )
            return 0;
    }
    return 1;
}

static void XboxSplitControlsProofObserve( UXboxViewport* Viewport, const XINPUT_GAMEPAD& Pad )
{
    if( XboxSplitBenchmarkEnabled() )
        return;
    if( (!XboxSplitControlsProofEnabled() && !XboxSplitControlsOnlineProofEnabled())
    ||  !Viewport || !Viewport->Actor || GXboxSplitControlsProofPhase == XSCP_Complete )
        return;

    UXboxClient* Client = XboxSplitControlsProofClient( Viewport );
    INT Slot = XboxViewportIndex( Viewport );
    if( GXboxSplitControlsProofPhase == XSCP_StartHold
    &&  Slot == ((GXboxSplitControlsProofTarget + 1) & 3)
    &&  Abs(Viewport->Actor->aBaseY) > 10.0f )
    {
        GXboxSplitControlsProofRunnerInput = 1;
    }

    if( Slot != GXboxSplitControlsProofTarget
    || (GXboxSplitControlsProofPhase != XSCP_Press && GXboxSplitControlsProofPhase != XSCP_Release) )
        return;

    APlayerPawn* Player = Viewport->Actor;
    const FXboxSplitControlsProofSnapshot& Snapshot = GXboxSplitControlsProofSnapshots[Slot];
    INT Test = GXboxSplitControlsProofTest;
    UBOOL Expected = 0;

    switch( Test )
    {
        case XSCT_MoveStick:
            Expected = (Player->Acceleration-Snapshot.Acceleration).SizeSquared() > 25.0f
                || (Player->Velocity-Snapshot.Velocity).SizeSquared() > 25.0f
                || (Player->Location-Snapshot.Location).SizeSquared() > 4.0f;
            break;
        case XSCT_LookStick:
            Expected = Player->ViewRotation.Yaw != Snapshot.ViewRotation.Yaw
                || Player->ViewRotation.Pitch != Snapshot.ViewRotation.Pitch;
            break;
        case XSCT_AJump:
            Expected = Player->bPressedJump != Snapshot.PressedJump || Player->Velocity.Z > Snapshot.Velocity.Z + 10.0f;
            break;
        case XSCT_BDuck:
            Expected = Player->bDuck != 0;
            break;
        case XSCT_XUse:
            Expected = Player->SelectedItem && Player->SelectedItem == Snapshot.SelectedItem
                && Player->SelectedItem->bActive != Snapshot.SelectedActive;
            break;
        case XSCT_YDodge:
            Expected = Player->DodgeDir != DODGE_None || (Player->Velocity-Snapshot.Velocity).SizeSquared() > 25.0f;
            break;
        case XSCT_LTAltFire:
            Expected = Player->bAltFire != 0 || Player->bJustAltFired != Snapshot.JustAltFired;
            break;
        case XSCT_RTFire:
            Expected = Player->bFire != 0 || Player->bJustFired != Snapshot.JustFired;
            break;
        case XSCT_WhitePrevTap:
        case XSCT_BlackNextTap:
            Expected = GXboxSplitControlsProofPhase == XSCP_Release
                && (Player->Weapon != Snapshot.Weapon || Player->PendingWeapon != Snapshot.PendingWeapon);
            break;
        case XSCT_WhiteWheel:
        case XSCT_BlackWheel:
            if( GXboxSplitControlsProofPhase == XSCP_Press && GXboxWeaponWheelActive[Slot] )
            {
                UBOOL OtherWheelOpen = 0;
                for( INT i=0; i<4; i++ )
                    if( i != Slot && GXboxWeaponWheelActive[i] )
                        OtherWheelOpen = 1;
                if( !OtherWheelOpen && XboxSplitControlsProofOtherPlayersUnchanged(Client, Test) )
                    GXboxSplitControlsProofWheelOpened = 1;
                else
                    GXboxSplitControlsProofIsolationFailed = 1;
            }
            Expected = GXboxSplitControlsProofPhase == XSCP_Release
                && (Player->Weapon != Snapshot.Weapon || Player->PendingWeapon != Snapshot.PendingWeapon);
            break;
        case XSCT_BackScoreboard:
            Expected = Player->bShowScores != Snapshot.ShowScores;
            break;
        case XSCT_RightThumbCenter:
            Expected = Player->bSnapLevel != 0;
            break;
        case XSCT_DpadUp:
        case XSCT_DpadDown:
        case XSCT_DpadLeft:
        case XSCT_DpadRight:
            Expected = Player->Weapon != Snapshot.Weapon || Player->PendingWeapon != Snapshot.PendingWeapon;
            break;
        default:
            break;
    }

    if( Expected )
    {
        if( XboxSplitControlsProofOtherPlayersUnchanged(Client, Test) )
            GXboxSplitControlsProofObserved = 1;
        else
            GXboxSplitControlsProofIsolationFailed = 1;
    }
}

static void XboxSplitControlsProofLogResult( UXboxClient* Client, UBOOL Passed, const char* Detail )
{
    UXboxViewport* Viewport = XboxSplitControlsProofViewport( Client, GXboxSplitControlsProofTarget );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    if( Passed )
        GXboxSplitControlsProofPasses++;
    else
        GXboxSplitControlsProofFailures++;
    GXboxLog.Write( "XSPLIT CONTROL %s mode=%s slot=%d port=%d test=%s detail=%s actor=0x%08X weapon=%s pending=%s pass=%d fail=%d",
        Passed ? "PASS" : "FAIL",
        XboxSplitControlsOnlineProofEnabled() ? "ONLINE" : "LOCAL",
        GXboxSplitControlsProofTarget + 1,
        Viewport ? Viewport->ControllerPort : -1,
        XboxSplitControlsProofTestName(GXboxSplitControlsProofTest),
        Detail ? Detail : "",
        (DWORD)Player,
        Player && Player->Weapon ? TCHAR_TO_ANSI(Player->Weapon->GetFullName()) : "(none)",
        Player && Player->PendingWeapon ? TCHAR_TO_ANSI(Player->PendingWeapon->GetFullName()) : "(none)",
        GXboxSplitControlsProofPasses,
        GXboxSplitControlsProofFailures );
    GXboxLog.Flush();
}

static void XboxSplitControlsProofAdvance( UXboxClient* Client, DOUBLE Now )
{
    if( XboxSplitControlsOnlineProofEnabled() )
    {
        GXboxSplitControlsProofTarget++;
    }
    else
    {
        GXboxSplitControlsProofTest++;
        if( GXboxSplitControlsProofTest >= XSCT_Count )
        {
            GXboxSplitControlsProofTest = XSCT_MoveStick;
            GXboxSplitControlsProofTarget++;
        }
    }

    if( GXboxSplitControlsProofTarget >= 4 )
    {
        INT Expected = 4 + 4 * (XboxSplitControlsOnlineProofEnabled() ? 1 : XSCT_Count);
        XboxSplitControlsProofRestoreClient( Client );
        GXboxSplitControlsProofPhase = XSCP_Complete;
        GXboxSplitSmokeFinished = 1;
        GXboxLog.Write( "XSPLIT CONTROLS COMPLETE result=%s mode=%s pass=%d fail=%d expected=%d map=%s net=%d",
            GXboxSplitControlsProofFailures == 0 && GXboxSplitControlsProofPasses == Expected ? "PASS" : "FAIL",
            XboxSplitControlsOnlineProofEnabled() ? "ONLINE" : "LOCAL",
            GXboxSplitControlsProofPasses,
            GXboxSplitControlsProofFailures,
            Expected,
            GXboxSplitControlsProofLevel ? TCHAR_TO_ANSI(*GXboxSplitControlsProofLevel->URL.Map) : "",
            (GXboxSplitControlsProofLevel && GXboxSplitControlsProofLevel->GetLevelInfo())
                ? (INT)GXboxSplitControlsProofLevel->GetLevelInfo()->NetMode : -1 );
        GXboxLog.Flush();
        return;
    }

    XboxSplitControlsProofPrepareTest( Client, Now );
}

static void XboxSplitControlsProofTick( UXboxClient* Client, ULevel* Level, DOUBLE Now )
{
    if( GXboxSplitControlsProofLevel != Level )
    {
        GXboxSplitControlsProofLevel = Level;
        XboxSplitControlsProofSetup( Client, Level, Now );
        return;
    }

    DOUBLE Elapsed = Now - GXboxSplitControlsProofPhaseTime;
    if( GXboxSplitControlsProofPhase == XSCP_Waiting )
    {
        if( Elapsed >= 2.0 )
            XboxSplitControlsProofPrepareTest( Client, Now );
        return;
    }

    if( GXboxSplitControlsProofPhase == XSCP_Prepare )
    {
        DOUBLE Wait = XboxSplitControlsProofWeaponTest(GXboxSplitControlsProofTest) ? 1.2 : 0.35;
        if( Elapsed >= Wait )
        {
            XboxSplitControlsProofCapture( Client );
            GXboxSplitControlsProofPhase = XSCP_Press;
            GXboxSplitControlsProofPhaseTime = Now;
        }
        return;
    }

    if( GXboxSplitControlsProofPhase == XSCP_Press )
    {
        DOUBLE Hold = (GXboxSplitControlsProofTest == XSCT_WhiteWheel || GXboxSplitControlsProofTest == XSCT_BlackWheel) ? 2.2 : 0.18;
        if( Elapsed >= Hold )
        {
            GXboxSplitControlsProofPhase = GXboxSplitControlsProofTest == XSCT_Start ? XSCP_StartHold : XSCP_Release;
            GXboxSplitControlsProofPhaseTime = Now;
            if( GXboxSplitControlsProofTest == XSCT_Start )
            {
                INT Runner = (GXboxSplitControlsProofTarget + 1) & 3;
                APlayerPawn* RunnerPlayer = XboxSplitControlsProofPlayer( Client, Runner );
                GXboxSplitControlsProofStartLevelTime = Level->GetLevelInfo()->TimeSeconds;
                GXboxSplitControlsProofRunnerLocation = RunnerPlayer ? RunnerPlayer->Location : FVector(0,0,0);
            }
        }
        return;
    }

    if( GXboxSplitControlsProofPhase == XSCP_Release )
    {
        DOUBLE Wait = XboxSplitControlsProofWeaponTest(GXboxSplitControlsProofTest) ? 1.1 : 0.35;
        if( Elapsed >= Wait )
        {
            UBOOL Passed = GXboxSplitControlsProofObserved && !GXboxSplitControlsProofIsolationFailed;
            if( GXboxSplitControlsProofTest == XSCT_WhiteWheel || GXboxSplitControlsProofTest == XSCT_BlackWheel )
                Passed = Passed && GXboxSplitControlsProofWheelOpened;
            XboxSplitControlsProofLogResult( Client, Passed,
                GXboxSplitControlsProofIsolationFailed ? "cross-viewport state changed" :
                (!GXboxSplitControlsProofObserved ? "target outcome missing" :
                ((GXboxSplitControlsProofTest == XSCT_WhiteWheel || GXboxSplitControlsProofTest == XSCT_BlackWheel) && !GXboxSplitControlsProofWheelOpened ? "wheel did not open independently" : "target-only outcome")) );
            XboxSplitControlsProofAdvance( Client, Now );
        }
        return;
    }

    if( GXboxSplitControlsProofPhase == XSCP_StartHold && Elapsed >= 2.0 )
    {
        INT Runner = (GXboxSplitControlsProofTarget + 1) & 3;
        APlayerPawn* RunnerPlayer = XboxSplitControlsProofPlayer( Client, Runner );
        FLOAT LevelDelta = Level->GetLevelInfo()->TimeSeconds - GXboxSplitControlsProofStartLevelTime;
        FLOAT RunnerMoveSq = RunnerPlayer ? (RunnerPlayer->Location-GXboxSplitControlsProofRunnerLocation).SizeSquared() : 0.0f;
        if( XboxSplitControlsOnlineProofEnabled() )
        {
            GXboxSplitControlsProofStartHoldPassed = GXboxMenu.Active
                && GXboxMenu.Screen == XMS_Pause
                && GXboxMenuOwnerViewport == GXboxSplitControlsProofTarget
                && !GXboxMenu.PausedMatch
                && GXboxMenuGameplayContinues
                && Level->GetLevelInfo()->Pauser == TEXT("")
                && LevelDelta > 0.5f
                && RunnerMoveSq > 25.0f;
            GXboxLog.Write( "XSPLIT START HOLD mode=ONLINE slot=%d owner=%d active=%d paused=%d continues=%d pauser=%s levelDelta=%.3f runner=%d runnerInput=%d runnerMoveSq=%.1f result=%s",
                GXboxSplitControlsProofTarget + 1,
                GXboxMenuOwnerViewport + 1,
                GXboxMenu.Active ? 1 : 0,
                GXboxMenu.PausedMatch ? 1 : 0,
                GXboxMenuGameplayContinues ? 1 : 0,
                TCHAR_TO_ANSI(*Level->GetLevelInfo()->Pauser),
                LevelDelta,
                Runner + 1,
                GXboxSplitControlsProofRunnerInput ? 1 : 0,
                RunnerMoveSq,
                GXboxSplitControlsProofStartHoldPassed ? "PASS" : "FAIL" );
        }
        else
        {
            GXboxSplitControlsProofStartHoldPassed = GXboxMenu.Active
                && GXboxMenu.Screen == XMS_Pause
                && GXboxMenuOwnerViewport == GXboxSplitControlsProofTarget
                && GXboxMenu.PausedMatch
                && !GXboxMenuGameplayContinues
                && Level->GetLevelInfo()->Pauser != TEXT("")
                && !GXboxSplitControlsProofRunnerInput
                && RunnerMoveSq < 1.0f;
            GXboxLog.Write( "XSPLIT START HOLD mode=LOCAL slot=%d owner=%d active=%d paused=%d continues=%d pauser=%s levelDelta=%.3f runner=%d runnerInput=%d runnerMoveSq=%.1f result=%s",
                GXboxSplitControlsProofTarget + 1,
                GXboxMenuOwnerViewport + 1,
                GXboxMenu.Active ? 1 : 0,
                GXboxMenu.PausedMatch ? 1 : 0,
                GXboxMenuGameplayContinues ? 1 : 0,
                TCHAR_TO_ANSI(*Level->GetLevelInfo()->Pauser),
                LevelDelta,
                Runner + 1,
                GXboxSplitControlsProofRunnerInput ? 1 : 0,
                RunnerMoveSq,
                GXboxSplitControlsProofStartHoldPassed ? "PASS" : "FAIL" );
        }
        GXboxSplitControlsProofPhase = XSCP_StartClose;
        GXboxSplitControlsProofPhaseTime = Now;
        return;
    }

    if( GXboxSplitControlsProofPhase == XSCP_StartClose && Elapsed >= 0.18 )
    {
        GXboxSplitControlsProofPhase = XSCP_StartRelease;
        GXboxSplitControlsProofPhaseTime = Now;
        return;
    }

    if( GXboxSplitControlsProofPhase == XSCP_StartRelease && Elapsed >= 0.4 )
    {
        UBOOL Closed = !GXboxMenu.Active && !GXboxMenu.PausedMatch
            && Level->GetLevelInfo()->Pauser == TEXT("");
        XboxSplitControlsProofLogResult( Client, GXboxSplitControlsProofStartHoldPassed && Closed,
            GXboxSplitControlsProofStartHoldPassed ? (Closed ? "hold and resume verified" : "resume failed") : "pause hold failed" );
        XboxSplitControlsProofAdvance( Client, Now );
    }
}

// Process-local benchmark only: normal movement, weapons, bot AI and damage.
// Both comparison builds run this same workload; it never sends host input.
static void XboxSplitCombatBenchmarkApply( UXboxViewport* Viewport, UXboxClient* Client, ULevel* Level, XINPUT_GAMEPAD& Pad )
{
    static ULevel* StartedLevel = NULL;
    static DOUBLE StartTime = 0.0, LastAudit = 0.0;
    static APlayerPawn* LastPlayers[4] = { NULL, NULL, NULL, NULL };
    static FVector LastLocations[4];
    static INT LastHealth[4] = { 0, 0, 0, 0 };
    static FLOAT Travel[4] = { 0, 0, 0, 0 };
    INT Slot = XboxViewportIndex(Viewport);
    APlayerPawn* Player = Viewport->Actor;
    if( !Player || Slot < 0 || Slot >= 4 ) return;
    DOUBLE Now = appSeconds();
    UObject* Game = Level->GetLevelInfo()->Game;
    if( Slot == 0 && StartedLevel != Level && Game )
    {
        StartedLevel = Level;
        StartTime = Now;
        LastAudit = 0.0;
        appMemzero( LastPlayers, sizeof(LastPlayers) );
        appMemzero( LastHealth, sizeof(LastHealth) );
        appMemzero( Travel, sizeof(Travel) );
        XboxSetObjectPropertyInt( Game, TEXT("MaxPlayers"), 16 );
        XboxSetObjectPropertyInt( Game, TEXT("MinPlayers"), GXboxSplitActivePlayerCount + 8 );
        XboxSetObjectPropertyInt( Game, TEXT("InitialBots"), 8 );
        XboxMenuAdjustPendingBots( Level, Game, 8 );
        XboxSetObjectPropertyInt( Game, TEXT("RemainingBots"), 0 );
        GXboxLog.Write( "XCOMBAT begin players=%d requestedBots=8 actualBots=%d",
            GXboxSplitActivePlayerCount, XboxGetObjectPropertyInt(Game,TEXT("NumBots"),0) );
    }
    if( StartedLevel != Level ) return;

    APawn* Target = NULL;
    FLOAT BestDistance = 1.e30f;
    for( APawn* Bot = Level->GetLevelInfo()->PawnList; Bot; Bot = Bot->nextPawn )
        if( Bot->Health > 0 && Bot->PlayerReplicationInfo && Bot->PlayerReplicationInfo->bIsABot )
        {
            FLOAT Distance = (Bot->Location - Player->Location).SizeSquared();
            if( Distance < BestDistance ) { BestDistance = Distance; Target = Bot; }
        }
    FLOAT Elapsed = (FLOAT)(Now - StartTime);
    FLOAT Phase = Elapsed + Slot * 0.73f;
    Pad.sThumbLY = 24000;
    Pad.sThumbLX = ((INT)(Phase / 2.5f) & 1) ? 15000 : -15000;
    if( Target )
    {
        FRotator Aim = (Target->Location + FVector(0,0,Target->BaseEyeHeight * 0.5f)
            - Player->Location - FVector(0,0,Player->EyeHeight)).Rotation();
        Pad.sThumbRX = (SHORT)Clamp<INT>( ((INT)(SWORD)(Aim.Yaw - Player->ViewRotation.Yaw)) * 4, -24000, 24000 );
        Pad.sThumbRY = (SHORT)Clamp<INT>( ((INT)(SWORD)(Aim.Pitch - Player->ViewRotation.Pitch)) * 4, -16000, 16000 );
    }
    else Pad.sThumbRX = ((INT)(Phase / 3.0f) & 1) ? 10000 : -10000;
    if( Phase - (INT)Phase < 0.85f ) Pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255;
    if( Phase - (INT)(Phase / 4.0f) * 4.0f < 0.18f ) Pad.bAnalogButtons[XINPUT_GAMEPAD_A] = 255;
    if( Phase - (INT)(Phase / 10.0f) * 10.0f < 0.18f ) Pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] = 255;

    if( Slot == 0 )
    {
        INT MovedMask = 0;
        for( INT i=0; i<Client->Viewports.Num() && i<4; i++ )
        {
            APlayerPawn* Current = Client->Viewports(i)->Actor;
            if( !Current ) continue;
            if( Current == LastPlayers[i] && Current->Health > 0 && LastHealth[i] > 0 )
            {
                FLOAT Segment = (Current->Location - LastLocations[i]).Size();
                if( Segment < 512.0f ) Travel[i] += Segment;
            }
            LastPlayers[i] = Current;
            LastLocations[i] = Current->Location;
            LastHealth[i] = Current->Health;
            if( Travel[i] >= 256.0f ) MovedMask |= 1 << i;
        }
        if( Now - LastAudit >= 2.0 )
        {
            INT Living = 0, Moving = 0, Firing = 0, Projectiles = 0, Deaths = 0;
            for( APawn* Bot = Level->GetLevelInfo()->PawnList; Bot; Bot = Bot->nextPawn )
                if( Bot->Health > 0 && Bot->PlayerReplicationInfo && Bot->PlayerReplicationInfo->bIsABot )
                {
                    Living++;
                    if( Bot->Velocity.SizeSquared() > 100.0f ) Moving++;
                    if( Bot->bFire || Bot->bAltFire ) Firing++;
                }
            for( INT i=0; i<Level->Actors.Num(); i++ )
            {
                AActor* Actor = Level->Actors(i);
                if( !Actor || Actor->bDeleteMe ) continue;
                if( Cast<AProjectile>(Actor) ) Projectiles++;
                APlayerReplicationInfo* PRI = Cast<APlayerReplicationInfo>(Actor);
                if( PRI ) Deaths += (INT)PRI->Deaths;
            }
            GXboxLog.Write( "XCOMBAT elapsed=%.1f players=%d bots=%d living=%d moving=%d firing=%d projectiles=%d deaths=%d movedMask=%X travel=%.0f,%.0f,%.0f,%.0f",
                Elapsed, GXboxSplitActivePlayerCount, XboxGetObjectPropertyInt(Game,TEXT("NumBots"),0),
                Living, Moving, Firing, Projectiles, Deaths, MovedMask, Travel[0], Travel[1], Travel[2], Travel[3] );
            LastAudit = Now;
        }
    }
}

static UBOOL XboxSplitControlsProofApply( UXboxViewport* Viewport, XINPUT_GAMEPAD& Pad )
{
    if( (!XboxSplitControlsProofEnabled() && !XboxSplitControlsOnlineProofEnabled()) || !Viewport )
        return 0;

    UXboxClient* Client = XboxSplitControlsProofClient( Viewport );
    ULevel* Level = NULL;
    if( !XboxSplitControlsProofReady(Client, Level) )
        return 0;

    appMemzero( &Pad, sizeof(Pad) );
    // Keep real local players active without the controls test changing their
    // weapons, opening menus or pausing simulation during a timing run.
    if( XboxSplitBenchmarkEnabled() )
    {
        if( XboxSplitCombatBenchmarkEnabled() )
            XboxSplitCombatBenchmarkApply( Viewport, Client, Level, Pad );
        return 1;
    }
    INT Slot = XboxViewportIndex( Viewport );
    DOUBLE Now = appSeconds();
    if( Slot == 0 && GXboxSplitControlsProofPhase != XSCP_Complete )
        XboxSplitControlsProofTick( Client, Level, Now );

    if( GXboxSplitControlsProofPhase == XSCP_StartHold )
    {
        if( Slot == ((GXboxSplitControlsProofTarget + 1) & 3) )
            Pad.sThumbLY = 28000;
        return 1;
    }

    if( GXboxSplitControlsProofPhase != XSCP_Press && GXboxSplitControlsProofPhase != XSCP_StartClose )
        return 1;
    if( Slot != GXboxSplitControlsProofTarget )
        return 1;

    if( GXboxSplitControlsProofPhase == XSCP_StartClose )
    {
        Pad.wButtons = XINPUT_GAMEPAD_START;
        return 1;
    }

    switch( GXboxSplitControlsProofTest )
    {
        case XSCT_MoveStick:         Pad.sThumbLX = 23000; Pad.sThumbLY = 28000; break;
        case XSCT_LookStick:         Pad.sThumbRX = 22000; Pad.sThumbRY = -18000; break;
        case XSCT_AJump:             Pad.bAnalogButtons[XINPUT_GAMEPAD_A] = 255; break;
        case XSCT_BDuck:             Pad.bAnalogButtons[XINPUT_GAMEPAD_B] = 255; break;
        case XSCT_XUse:              Pad.bAnalogButtons[XINPUT_GAMEPAD_X] = 255; break;
        case XSCT_YDodge:            Pad.bAnalogButtons[XINPUT_GAMEPAD_Y] = 255; Pad.sThumbLX = 28000; break;
        case XSCT_LTAltFire:         Pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] = 255; break;
        case XSCT_RTFire:            Pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] = 255; break;
        case XSCT_WhitePrevTap:      Pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE] = 255; break;
        case XSCT_BlackNextTap:      Pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] = 255; break;
        case XSCT_WhiteWheel:        Pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE] = 255; Pad.sThumbRX = -30000; Pad.sThumbRY = -12000; break;
        case XSCT_BlackWheel:        Pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK] = 255; Pad.sThumbRX = -30000; Pad.sThumbRY = -12000; break;
        case XSCT_BackScoreboard:    Pad.wButtons = XINPUT_GAMEPAD_BACK; break;
        case XSCT_RightThumbCenter:  Pad.wButtons = XINPUT_GAMEPAD_RIGHT_THUMB; break;
        case XSCT_DpadUp:            Pad.wButtons = XINPUT_GAMEPAD_DPAD_UP; break;
        case XSCT_DpadDown:          Pad.wButtons = XINPUT_GAMEPAD_DPAD_DOWN; break;
        case XSCT_DpadLeft:          Pad.wButtons = XINPUT_GAMEPAD_DPAD_LEFT; break;
        case XSCT_DpadRight:         Pad.wButtons = XINPUT_GAMEPAD_DPAD_RIGHT; break;
        case XSCT_Start:             Pad.wButtons = XINPUT_GAMEPAD_START; break;
        default: break;
    }
    return 1;
}

static INT XboxWeaponWheelSlotFromStick( INT StickLayout, const XINPUT_GAMEPAD& Pad, INT CurrentSlot )
{
    const SHORT Threshold = 9000;
    FLOAT MoveX = 0.0f;
    FLOAT MoveY = 0.0f;
    FLOAT X = 0.0f;
    FLOAT Y = 0.0f;
    XboxStickLayoutAxes(
        XboxStickLayoutClamp(StickLayout),
        (FLOAT)Pad.sThumbLX,
        (FLOAT)Pad.sThumbLY,
        (FLOAT)Pad.sThumbRX,
        (FLOAT)Pad.sThumbRY,
        MoveX,
        MoveY,
        X,
        Y );
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

static BYTE XboxDodgeDirectionFromStick( INT StickLayout, const XINPUT_GAMEPAD& Pad )
{
    const SHORT Threshold = 14000;
    FLOAT LX = 0.0f;
    FLOAT LY = 0.0f;
    FLOAT LookX = 0.0f;
    FLOAT LookY = 0.0f;
    XboxStickLayoutAxes(
        XboxStickLayoutClamp(StickLayout),
        (FLOAT)Pad.sThumbLX,
        (FLOAT)Pad.sThumbLY,
        (FLOAT)Pad.sThumbRX,
        (FLOAT)Pad.sThumbRY,
        LX,
        LY,
        LookX,
        LookY );
    if( LX > -Threshold && LX < Threshold && LY > -Threshold && LY < Threshold )
        return DODGE_None;

    if( Abs<INT>((INT)LX) > Abs<INT>((INT)LY) )
        return LX < 0 ? DODGE_Left : DODGE_Right;
    return LY < 0 ? DODGE_Back : DODGE_Forward;
}

static void XboxTriggerDodge( INT StickLayout, APlayerPawn* Player, const XINPUT_GAMEPAD& Pad )
{
    if( !Player || Player->Physics != PHYS_Walking )
        return;

    BYTE DodgeMove = XboxDodgeDirectionFromStick( StickLayout, Pad );
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
        XboxWeaponWheelDrawSlice( Canvas, CX, CY, InnerR-2.0f, OuterR+2.0f, i, 10, 10, 10, bFocus ? 106 : 69 );

        BYTE SliceR = bFocus ? 176 : 148;
        BYTE SliceG = bFocus ? 176 : 148;
        BYTE SliceB = bFocus ? 176 : 148;
        BYTE SliceA = bAvailable ? (bFocus ? 130 : 98) : 53;
        XboxWeaponWheelDrawSlice( Canvas, CX, CY, InnerR, OuterR, i, SliceR, SliceG, SliceB, SliceA );

        if( bFocus )
            XboxWeaponWheelDrawSlice( Canvas, CX, CY, InnerR+5.0f, OuterR-5.0f, i, 210, 210, 210, 42 );
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

static void XboxMenuDrawPreviewCrosshair( UCanvas* Canvas, UXboxViewport* Viewport, FLOAT CX, FLOAT CY, INT Crosshair, INT ColorIndex )
{
    if( !Canvas || Crosshair < 0 || Crosshair > 8 )
        return;

    // ChallengeHUD.DrawCrossHair uses this texture array and LoadCrosshair
    // resolves the corresponding configured CrossHairs entry. Share that source
    // instead of approximating the nine shapes with unrelated rectangles.
    AHUD* HUD = Viewport && Viewport->Actor ? Viewport->Actor->myHUD : NULL;
    UTexture* Texture = Cast<UTexture>( XboxGetObjectPropertyObjectAt(HUD, TEXT("CrossHairTextures"), Crosshair) );
    if( !Texture )
    {
        FString TextureName;
        UProperty* Property = HUD ? FindField<UProperty>(HUD->GetClass(), TEXT("CrossHairs")) : NULL;
        if( Property && Crosshair < Property->ArrayDim )
        {
            TCHAR Value[1024] = TEXT("");
            Property->ExportText( Crosshair, Value, (BYTE*)HUD, (BYTE*)HUD, 0 );
            TextureName = Value;
            XboxMenuCleanExportedText( TextureName );
        }
        else
        {
            UClass* HUDClass = UObject::StaticLoadClass( AHUD::StaticClass(), NULL, TEXT("Botpack.ChallengeHUD"), NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
            XboxMenuClassDefaultStringAt( HUDClass, TEXT("CrossHairs"), Crosshair, TextureName );
        }
        if( TextureName.Len() )
            Texture = Cast<UTexture>( UObject::StaticLoadObject(UTexture::StaticClass(), NULL, *TextureName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL) );
    }
    if( !Texture )
        return;

    const FLOAT Size = 64.0f;
    FPlane Color(
        XboxMenuColorByte(ColorIndex, 0) / 255.0f,
        XboxMenuColorByte(ColorIndex, 1) / 255.0f,
        XboxMenuColorByte(ColorIndex, 2) / 255.0f, 1.0f );
    static INT CrosshairProof = -1;
    static INT LastProofCrosshair = -1;
    if( XboxSmokeMarkerExists("XboxCrosshairProof.ini", CrosshairProof) && Crosshair != LastProofCrosshair )
    {
        LastProofCrosshair = Crosshair;
        GXboxLog.Write("XCROSSHAIR PROOF index=%d texture=%s", Crosshair, TCHAR_TO_ANSI(Texture->GetPathName()));
    }
    Canvas->DrawTile( Texture, CX-Size*0.5f, CY-Size*0.5f, Size, Size,
        0, 0, 64, 64, NULL, Canvas->Z, Color, FPlane(0,0,0,0), PF_Translucent | PF_TwoSided );
}

static FLOAT XboxMenuSettingsPreviewRight( UCanvas* Canvas )
{
    return Canvas->ClipX - 26.0f;
}

static FLOAT XboxMenuSettingsPreviewLeft( UCanvas* Canvas )
{
    const FLOAT OuterRight = Canvas->ClipX - 18.0f;
    const FLOAT OuterWidth = Clamp<FLOAT>( OuterRight - 394.0f, 124.0f, 132.0f );
    return XboxMenuSettingsPreviewRight(Canvas) - (OuterWidth - 16.0f);
}

static void XboxMenuDrawSettingsPreview( UCanvas* Canvas, UFont* Font, UXboxClient* Client, UXboxViewport* Viewport, INT Crosshair )
{
    FLOAT X1 = XboxMenuSettingsPreviewLeft( Canvas );
    FLOAT X2 = XboxMenuSettingsPreviewRight( Canvas );
    FLOAT Y1 = 92.0f;
    FLOAT Y2 = 282.0f;
    BYTE HudR = XboxMenuColorByte( GXboxSettingsHudColor, 0 );
    BYTE HudG = XboxMenuColorByte( GXboxSettingsHudColor, 1 );
    BYTE HudB = XboxMenuColorByte( GXboxSettingsHudColor, 2 );
    FLOAT HudA = Clamp<FLOAT>( (FLOAT)GXboxSettingsHudOpacity / 16.0f, 0.06f, 1.0f );

    XboxMenuDrawRect( Canvas, X1-8, Y1-8, X2+8, Y2+8, 0, 0, 0, 0.62f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y2, 6, 24, 48, 0.78f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y1+3, 28, 108, 205, 0.86f );
    XboxMenuText( Canvas, Font, X1+14, Y1+14, 135, 170, 205, TEXT("PREVIEW") );

    XboxMenuDrawRect( Canvas, X1+14, Y1+42, X2-14, Y1+118, 0, 0, 0, 0.46f );
    INT SafeX = (INT)(X1 + 14.0f);
    INT SafeY = (INT)(Y1 + 42.0f);
    INT SafeW = (INT)((X2 - 14.0f) - (X1 + 14.0f));
    INT SafeH = (INT)((Y1 + 118.0f) - (Y1 + 42.0f));
    XboxViewportApplySafeArea( Client, SafeX, SafeY, SafeW, SafeH );
    XboxMenuDrawRect( Canvas, (FLOAT)SafeX, (FLOAT)SafeY, (FLOAT)(SafeX + SafeW), (FLOAT)(SafeY + 2), 38, 142, 220, 0.92f );
    XboxMenuDrawRect( Canvas, (FLOAT)SafeX, (FLOAT)(SafeY + SafeH - 2), (FLOAT)(SafeX + SafeW), (FLOAT)(SafeY + SafeH), 38, 142, 220, 0.92f );
    XboxMenuDrawRect( Canvas, (FLOAT)SafeX, (FLOAT)SafeY, (FLOAT)(SafeX + 2), (FLOAT)(SafeY + SafeH), 38, 142, 220, 0.92f );
    XboxMenuDrawRect( Canvas, (FLOAT)(SafeX + SafeW - 2), (FLOAT)SafeY, (FLOAT)(SafeX + SafeW), (FLOAT)(SafeY + SafeH), 38, 142, 220, 0.92f );
    XboxMenuDrawPreviewCrosshair( Canvas, Viewport, SafeX + SafeW * 0.5f, SafeY + SafeH * 0.5f, Crosshair, GXboxSettingsCrosshairColor );

    FLOAT StatsX1 = X1 + 12.0f;
    FLOAT StatsX2 = X2 - 12.0f;
    FLOAT StatsGap = 8.0f;
    FLOAT StatsW = (StatsX2 - StatsX1 - StatsGap) * 0.5f;
    FLOAT RightStatsX = StatsX1 + StatsW + StatsGap;
    XboxMenuDrawRect( Canvas, StatsX1, Y1+138, StatsX1+StatsW, Y1+174, HudR, HudG, HudB, HudA );
    XboxMenuDrawRect( Canvas, StatsX1+6, Y1+144, StatsX1+StatsW-6, Y1+151, 255, 255, 255, 0.22f );
    XboxMenuDrawRect( Canvas, RightStatsX, Y1+138, StatsX2, Y1+174, HudR, HudG, HudB, HudA );
    XboxMenuDrawRect( Canvas, RightStatsX+6, Y1+148, StatsX2-6, Y1+156, 255, 255, 255, 0.24f );
    XboxMenuText( Canvas, Font, StatsX1+4, Y1+154, 235, 245, 255, TEXT("100") );
    XboxMenuText( Canvas, Font, RightStatsX+4, Y1+154, 235, 245, 255, TEXT("AMMO") );
}

static void XboxMenuDrawChromeBase( UCanvas* Canvas, const TCHAR* Section )
{
    FLOAT W = Canvas->ClipX;
    FLOAT H = Canvas->ClipY;
    (void)Section;

    XboxMenuDrawRect( Canvas, 0, 0, W, H, 2, 6, 14, 0.45f );
    XboxMenuDrawRect( Canvas, 18, 14, W-18, 42, 9, 42, 89, 0.72f );
    XboxMenuDrawRect( Canvas, 20, 16, W-20, 19, 31, 112, 205, 0.85f );
    XboxMenuDrawFooterBand( Canvas );
}

static void XboxMenuDrawChromeCommands
(
    UCanvas* Canvas, const TCHAR* Section,
    const char* Image1, const TCHAR* Label1,
    const char* Image2=NULL, const TCHAR* Label2=NULL,
    const char* Image3=NULL, const TCHAR* Label3=NULL
)
{
    XboxMenuDrawChromeBase( Canvas, Section );
    XboxMenuDrawFooterCommands( Canvas, Image1, Label1, Image2, Label2, Image3, Label3 );
}

static void XboxMenuDrawChrome( UCanvas* Canvas, const TCHAR* Section, UBOOL bShowBack )
{
    if( bShowBack )
        XboxMenuDrawChromeCommands( Canvas, Section, "button_a.xui", TEXT("SELECT"), "button_b.xui", TEXT("BACK") );
    else
        XboxMenuDrawChromeCommands( Canvas, Section, "button_a.xui", TEXT("SELECT") );
}

static void XboxMenuDrawMain( UCanvas* Canvas )
{
    static const TCHAR* VersionLabel = TEXT("v1.1.9b");
    static const TCHAR* Items[] =
    {
        TEXT("TOURNAMENT"),
        TEXT("INSTANT ACTION"),
        TEXT("SYSTEM LINK"),
        TEXT("SPLITSCREEN"),
        TEXT("PLAYER SETUP"),
        TEXT("SETTINGS")
    };

    XboxMenuDrawChromeCommands( Canvas, TEXT("MAIN MENU"),
        "button_a.xui", TEXT("SELECT"),
        "button_x.xui", TEXT("LOAD PROFILE") );

    UFont* VersionFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    INT VersionXL = 0;
    INT VersionYL = 0;
    XboxMenuTextSize( Canvas, VersionFont, VersionLabel, VersionXL, VersionYL );
    FLOAT VersionX = Max<FLOAT>( 38.0f, Canvas->ClipX - 38.0f - (FLOAT)VersionXL );
    FLOAT VersionY = XboxMenuCenteredTextY
    (
        Canvas,
        VersionFont,
        XboxMenuFooterTop(Canvas),
        XboxMenuFooterContentBottom(Canvas),
        VersionLabel
    );
    XboxMenuText( Canvas, VersionFont, VersionX, VersionY, 135, 170, 205, VersionLabel );

    UFont* MainFont = Canvas->MedFont;
    if( Canvas->Frame )
    {
        XboxRenderDrawMenuTexture( Canvas->Frame, "ut_logo_256.xui", 44, 58, 270, 135, 1.0f );
    }

    FLOAT MainStep = Clamp<FLOAT>( (XboxMenuFooterTop(Canvas) - 36.0f - 188.0f) / 5.0f, 28.0f, 34.0f );
    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 188.0f + i * MainStep;
        if( i == GXboxMenu.MainFocus )
        {
            XboxMenuDrawSelectionHighlight( Canvas, MainFont, 44.0f, 304.0f, Y, Items[i], 0.35f );
            XboxMenuText( Canvas, MainFont, 62, Y, 255, 255, 255, Items[i] );
        }
        else
        {
            XboxMenuText( Canvas, MainFont, 62, Y, 135, 170, 205, Items[i] );
        }
    }
}

static void XboxMenuDrawPauseReturnConfirm( UCanvas* Canvas )
{
    if( !Canvas || !GXboxPauseReturnConfirm )
        return;

    FLOAT ContentTop = Canvas->ClipY >= 400.0f ? 54.0f : 18.0f;
    FLOAT ContentBottom = XboxMenuFooterTop(Canvas) - 8.0f;
    XboxMenuDrawRect( Canvas, 18.0f, ContentTop, Canvas->ClipX-18.0f, ContentBottom, 0, 0, 0, 0.58f );

    FLOAT DialogW = Min<FLOAT>( Canvas->ClipX - 36.0f, Canvas->ClipX >= 500.0f ? 338.0f : 276.0f );
    FLOAT DialogH = Min<FLOAT>( ContentBottom - ContentTop - 8.0f, Canvas->ClipY >= 400.0f ? 142.0f : 126.0f );
    FLOAT DialogX = (FLOAT)(INT)((Canvas->ClipX - DialogW) * 0.5f);
    FLOAT DialogY = (FLOAT)(INT)(ContentTop + ((ContentBottom - ContentTop) - DialogH) * 0.5f);
    FLOAT DialogRight = DialogX + DialogW;
    FLOAT DialogBottom = DialogY + DialogH;

    XboxMenuDrawRect( Canvas, DialogX-2.0f, DialogY-2.0f, DialogRight+2.0f, DialogBottom+2.0f, 31, 112, 205, 0.95f );
    XboxMenuDrawRect( Canvas, DialogX, DialogY, DialogRight, DialogBottom, 3, 12, 27, 0.98f );
    XboxMenuDrawRect( Canvas, DialogX, DialogY, DialogRight, DialogY+5.0f, 31, 112, 205, 0.95f );

    UFont* SmallFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    UFont* TitleFont = Canvas->ClipX >= 500.0f ? Canvas->MedFont : SmallFont;
    const TCHAR* Title = TEXT("RETURN TO MAIN MENU?");
    const TCHAR* Warning = TEXT("CURRENT MATCH WILL END");
    INT XL = 0;
    INT YL = 0;
    XboxMenuTextSize( Canvas, TitleFont, Title, XL, YL );
    XboxMenuText( Canvas, TitleFont, (FLOAT)(INT)(DialogX + (DialogW-XL)*0.5f), DialogY+18.0f, 255, 255, 255, Title );
    XboxMenuTextSize( Canvas, SmallFont, Warning, XL, YL );
    XboxMenuText( Canvas, SmallFont, (FLOAT)(INT)(DialogX + (DialogW-XL)*0.5f), DialogY+52.0f, 135, 170, 205, Warning );

    const TCHAR* Options[] = { TEXT("YES"), TEXT("NO") };
    FLOAT ButtonW = Min<FLOAT>( 96.0f, (DialogW - 58.0f) * 0.5f );
    FLOAT ButtonGap = 18.0f;
    FLOAT ButtonsW = ButtonW * 2.0f + ButtonGap;
    FLOAT ButtonX = DialogX + (DialogW - ButtonsW) * 0.5f;
    FLOAT ButtonTop = DialogBottom - 43.0f;
    for( INT i=0; i<2; i++ )
    {
        FLOAT X1 = ButtonX + i * (ButtonW + ButtonGap);
        FLOAT X2 = X1 + ButtonW;
        UBOOL bFocused = i == GXboxPauseReturnConfirmFocus;
        XboxMenuDrawRect( Canvas, X1, ButtonTop, X2, ButtonTop+30.0f, bFocused ? 12 : 7, bFocused ? 82 : 31, bFocused ? 166 : 68, bFocused ? 0.95f : 0.78f );
        XboxMenuDrawRect( Canvas, X1, ButtonTop+27.0f, X2, ButtonTop+30.0f, 28, 108, 205, bFocused ? 0.95f : 0.35f );
        XboxMenuTextSize( Canvas, SmallFont, Options[i], XL, YL );
        XboxMenuText( Canvas, SmallFont, (FLOAT)(INT)(X1 + (ButtonW-XL)*0.5f), XboxMenuCenteredTextY(Canvas, SmallFont, ButtonTop, ButtonTop+27.0f, Options[i]), bFocused ? 255 : 170, bFocused ? 255 : 200, bFocused ? 255 : 225, Options[i] );
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

    XboxMenuDrawChromeCommands( Canvas, TEXT("PAUSED"), "button_a.xui", TEXT("SELECT"), "button_b.xui", GXboxPauseReturnConfirm ? TEXT("CANCEL") : TEXT("RESUME") );
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 58, 96, 255, 255, 255, TEXT("PAUSED") );

    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 170.0f + i * 40.0f;
        if( i == GXboxMenu.PauseFocus )
        {
            XboxMenuDrawSelectionHighlight( Canvas, MenuFont, 44.0f, 304.0f, Y, Items[i], 0.35f );
            XboxMenuText( Canvas, MenuFont, 62, Y, 255, 255, 255, Items[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 62, Y, 135, 170, 205, Items[i] );
        }
    }

    if( !GXboxPauseReturnConfirm )
    {
        const TCHAR* ResumeHint = TEXT("START OR B RESUMES");
        XboxMenuText( Canvas, MenuFont, 58, XboxMenuAboveFooterTextY(Canvas, MenuFont, ResumeHint, 9.0f), 135, 170, 205, ResumeHint );
    }
    XboxMenuDrawPauseReturnConfirm( Canvas );
}

static void XboxMenuDrawSplitPause( UViewport* Viewport, UCanvas* Canvas )
{
    if( !Canvas )
        return;

    INT ViewportIndex = XboxViewportIndex( Cast<UXboxViewport>(Viewport) );
    UFont* MenuFont = Canvas->MedFont;

    const TCHAR* PauseTitle = GXboxMenu.PausedMatch ? TEXT("PAUSED") : TEXT("PAUSE");
    XboxMenuDrawRect( Canvas, 0, 0, Canvas->ClipX, Canvas->ClipY, 2, 6, 14, 0.42f );
    XboxMenuText( Canvas, MenuFont, 58, 72, 135, 255, 120, PauseTitle );

    if( ViewportIndex != GXboxMenuOwnerViewport )
        return;

    static const TCHAR* Items[] =
    {
        TEXT("RESUME"),
        TEXT("MAIN MENU"),
        TEXT("SETTINGS")
    };

    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 110.0f + i * 28.0f;
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

    XboxMenuDrawFooterBand( Canvas );
    XboxMenuDrawFooterCommands( Canvas, "button_a.xui", TEXT("SELECT"), "button_b.xui", GXboxPauseReturnConfirm ? TEXT("CANCEL") : TEXT("RESUME") );
    XboxMenuDrawPauseReturnConfirm( Canvas );
}

static void XboxMenuDrawInstantAction( UCanvas* Canvas )
{
    static const TCHAR* Labels[] =
    {
        TEXT("GAMETYPE"),
        TEXT("ARENA"),
        TEXT("BOTS"),
        TEXT("BOT SKILL"),
        TEXT("SCORE LIMIT"),
        TEXT("TIME LIMIT"),
        TEXT("MUTATORS"),
        TEXT("BEGIN MATCH")
    };

    GXboxMenu.InstantGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1 );
    INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
    const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
    UBOOL bLives = XboxMenuGameUsesLives( *Game.URLValue );
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

    INT TimeLimit = bLives ? 0 : GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
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
    const FLOAT PreviewOuterY = 92.0f;
    const FLOAT PreviewOuterRight = XboxMenuContentRight(Canvas);
    const FLOAT PreviewSize = Clamp<FLOAT>( PreviewOuterRight - 362.0f - 20.0f, 120.0f, 188.0f );
    const FLOAT PreviewOuterX = PreviewOuterRight - PreviewSize - 20.0f;
    const FLOAT PreviewInnerX = PreviewOuterX + 10.0f;
    const FLOAT PreviewInnerY = PreviewOuterY + 10.0f;
    XboxMenuDrawRect( Canvas, PreviewOuterX, PreviewOuterY, PreviewOuterRight, PreviewOuterY+PreviewSize+20.0f, 25, 34, 48, 0.88f );
    XboxMenuDrawRect( Canvas, PreviewInnerX, PreviewInnerY, PreviewInnerX+PreviewSize, PreviewInnerY+PreviewSize, 0, 0, 0, 0.88f );
    if( Preview )
    {
        FLOAT SrcW = Max<FLOAT>( 1.0f, (FLOAT)Preview->USize );
        FLOAT SrcH = Max<FLOAT>( 1.0f, (FLOAT)Preview->VSize );
        FLOAT Scale = Min<FLOAT>( PreviewSize / SrcW, PreviewSize / SrcH );
        FLOAT PW = SrcW * Scale;
        FLOAT PH = SrcH * Scale;
        XboxMenuDrawTexture( Canvas, Preview, PreviewInnerX + (PreviewSize - PW) * 0.5f, PreviewInnerY + (PreviewSize - PH) * 0.5f, PW, PH );
    }
    else
    {
        XboxMenuTextFit( Canvas, MenuFont, PreviewInnerX+14.0f, PreviewInnerY+PreviewSize*0.46f, PreviewSize-28.0f, 210, 230, 245, TEXT("NO PREVIEW") );
    }

    const TCHAR* Hint = GXboxMenu.InstantFocus == 6
        ? TEXT("A OPENS MUTATOR LIST")
        : TEXT("DPAD LEFT/RIGHT CHANGES OPTIONS");
    FLOAT HintY = XboxMenuAboveFooterTextY( Canvas, MenuFont, Hint, 8.0f );
    FLOAT RowStep = Clamp<FLOAT>( (HintY - 24.0f - 126.0f) / 7.0f, 25.0f, 30.0f );
    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        const TCHAR* RowLabel = (bLives && i == 4) ? TEXT("LIVES") : Labels[i];
        FLOAT Y = 126.0f + i * RowStep;
        if( i == GXboxMenu.InstantFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-6, 350, Y+18, 12, 82, 166, 0.55f );
            if( i < 6 && !(bLives && i == 5) )
            {
                XboxMenuText( Canvas, MenuFont, 192, Y, 180, 215, 245, TEXT("<") );
                XboxMenuText( Canvas, MenuFont, 334, Y, 180, 215, 245, TEXT(">") );
            }
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, RowLabel );
            XboxMenuTextFit( Canvas, MenuFont, 210, Y, 120.0f, 255, 255, 255, Values[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, RowLabel );
            XboxMenuTextFit( Canvas, MenuFont, 210, Y, 120.0f, 180, 205, 230, Values[i] );
        }
    }

    XboxMenuTextFit( Canvas, MenuFont, 58, HintY, XboxMenuContentRight(Canvas)-76.0f, 135, 170, 205, Hint );
}

static void XboxMenuDrawTournament( UXboxViewport* Viewport, UCanvas* Canvas )
{
    XboxTournamentClampSelection( Viewport );
    UClass* LadderClass = XboxTournamentLadderClass( GXboxMenu.TournamentLadder );
    FString Map = XboxTournamentFullMap( GXboxMenu.TournamentLadder, GXboxMenu.TournamentMatch );
    FString Title;
    FString Description;
    XboxTournamentStringAt( GXboxMenu.TournamentLadder, TEXT("MapTitle"), GXboxMenu.TournamentMatch, Title );
    XboxTournamentStringAt( GXboxMenu.TournamentLadder, TEXT("MapDescription"), GXboxMenu.TournamentMatch, Description );
    if( Title.Len() <= 0 )
        Title = Map;

    INT FirstMatch = GXboxTournamentLadders[GXboxMenu.TournamentLadder].FirstRatedMatch;
    INT MatchCount = XboxTournamentMatchCount( GXboxMenu.TournamentLadder );
    INT RatedCount = Max<INT>( 1, MatchCount - FirstMatch );
    INT DisplayRung = Clamp<INT>( GXboxMenu.TournamentMatch - FirstMatch + 1, 1, RatedCount );
    TCHAR ProgressText[64];
    appSprintf( ProgressText, TEXT("RUNG %02i OF %02i"), DisplayRung, RatedCount );

    UTexture* Preview = Map.Len() ? XboxMenuGetMapPreview( *Map ) : NULL;
    INT FragLimit = XboxMenuClassDefaultIntAt( LadderClass, TEXT("FragLimits"), GXboxMenu.TournamentMatch, 0 );
    INT GoalScore = XboxMenuClassDefaultIntAt( LadderClass, TEXT("GoalTeamScore"), GXboxMenu.TournamentMatch, 0 );
    INT TimeLimit = XboxMenuClassDefaultIntAt( LadderClass, TEXT("TimeLimits"), GXboxMenu.TournamentMatch, 0 );
    TCHAR RuleText[96];
    if( GoalScore > 0 )
    {
        appSprintf( RuleText, TEXT("GOAL SCORE %i"), GoalScore );
    }
    else if( FragLimit > 0 )
    {
        appSprintf( RuleText, TEXT("FRAG LIMIT %i"), FragLimit );
    }
    else
    {
        appStrcpy( RuleText, TEXT("STANDARD RULES") );
    }
    if( TimeLimit > 0 )
    {
        TCHAR Temp[96];
        appSprintf( Temp, TEXT("%s    %i MINUTES"), RuleText, TimeLimit );
        appStrcpy( RuleText, Temp );
    }

    const TCHAR* FooterAction = GXboxMenu.TournamentFocus == 2 ? TEXT("BEGIN") : TEXT("SELECT");
    XboxMenuDrawChromeCommands( Canvas, TEXT("TOURNAMENT"), "button_a.xui", FooterAction, "button_b.xui", TEXT("BACK") );
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    const FLOAT ContentRight = XboxMenuContentRight(Canvas);
    const FLOAT PanelBottom = XboxMenuContentBottom(Canvas);
    const FLOAT LeftPanelRight = 276.0f;
    const FLOAT RightPanelX = 288.0f;

    XboxMenuText( Canvas, MenuFont, 46, 58, 255, 255, 255, TEXT("TOURNAMENT") );

    // The ladder circuit is the primary identity and the first interactive control.
    XboxMenuDrawRect( Canvas, 42, 84, ContentRight, 128, 0, 0, 0, 0.62f );
    XboxMenuDrawRect( Canvas, 42, 84, ContentRight, 88, 31, 112, 205, 0.90f );
    if( GXboxMenu.TournamentFocus == 0 )
    {
        XboxMenuDrawRect( Canvas, 46, 90, ContentRight-4.0f, 124, 12, 82, 166, 0.58f );
        XboxMenuDrawRect( Canvas, 46, 121, ContentRight-4.0f, 124, 28, 108, 205, 0.68f );
    }
    XboxMenuText( Canvas, SmallFont, 58, 96, 140, 178, 212, TEXT("LADDER CIRCUIT") );
    XboxMenuText( Canvas, MenuFont, 166, 98, 180, 215, 245, TEXT("<") );
    const TCHAR* LadderLabel = GXboxTournamentLadders[GXboxMenu.TournamentLadder].Label;
    const FLOAT LadderLabelX = 190.0f;
    const FLOAT LadderLabelMaxWidth = 242.0f;
    const FLOAT ProgressX = ContentRight-128.0f;
    INT LadderLabelXL = 0;
    INT LadderLabelYL = 0;
    XboxMenuTextSize( Canvas, MenuFont, LadderLabel, LadderLabelXL, LadderLabelYL );
    const FLOAT LadderArrowX = Min<FLOAT>( LadderLabelX + Min<FLOAT>((FLOAT)LadderLabelXL, LadderLabelMaxWidth) + 12.0f, ProgressX - 22.0f );
    XboxMenuTextFit( Canvas, MenuFont, LadderLabelX, 98, LadderLabelMaxWidth, 255, 255, 255, LadderLabel );
    XboxMenuText( Canvas, MenuFont, LadderArrowX, 98, 180, 215, 245, TEXT(">") );
    XboxMenuTextFit( Canvas, SmallFont, ProgressX, 100, 112.0f, 135, 255, 120, ProgressText );

    // Left side: a persistent ladder path matching the post-match progression language.
    XboxMenuDrawRect( Canvas, 42, 140, LeftPanelRight, PanelBottom, 0, 0, 0, 0.70f );
    XboxMenuDrawRect( Canvas, 42, 140, LeftPanelRight, 144, 31, 112, 205, 0.82f );
    XboxMenuText( Canvas, MenuFont, 58, 154, 255, 255, 255, TEXT("LADDER RUN") );
    XboxMenuText( Canvas, SmallFont, 58, 176, 135, 170, 205, ProgressText );

    const INT VisibleRows = 7;
    INT MaxFirstVisible = Max<INT>( FirstMatch, MatchCount - VisibleRows );
    INT FirstVisible = Clamp<INT>( GXboxMenu.TournamentMatch - 2, FirstMatch, MaxFirstVisible );
    const FLOAT FirstRowY = 202.0f;
    const FLOAT RowStep = 26.0f;
    XboxMenuDrawRect( Canvas, 68, FirstRowY-4.0f, 70, FirstRowY+(VisibleRows-1)*RowStep+10.0f, 31, 112, 205, 0.32f );

    for( INT Row=0; Row<VisibleRows; Row++ )
    {
        INT MatchIndex = FirstVisible + Row;
        if( MatchIndex >= MatchCount )
            break;

        FString RowTitle;
        XboxTournamentStringAt( GXboxMenu.TournamentLadder, TEXT("MapTitle"), MatchIndex, RowTitle );
        if( RowTitle.Len() <= 0 )
            RowTitle = XboxTournamentFullMap( GXboxMenu.TournamentLadder, MatchIndex );

        UBOOL bCompleted = MatchIndex < GXboxMenu.TournamentMatch;
        UBOOL bCurrent = MatchIndex == GXboxMenu.TournamentMatch;
        UBOOL bSelected = bCurrent && GXboxMenu.TournamentFocus == 1;
        FLOAT Y = FirstRowY + Row * RowStep;

        if( bCurrent )
        {
            XboxMenuDrawRect( Canvas, 54, Y-5, LeftPanelRight-12.0f, Y+17, 12, 82, 166, bSelected ? 0.76f : 0.56f );
            XboxMenuDrawRect( Canvas, 54, Y+14, LeftPanelRight-12.0f, Y+17, 28, 108, 205, bSelected ? 0.78f : 0.62f );
        }

        BYTE MarkerR = bCompleted ? 38 : (bCurrent ? 135 : 42);
        BYTE MarkerG = bCompleted ? 142 : (bCurrent ? 255 : 58);
        BYTE MarkerB = bCompleted ? 80 : (bCurrent ? 120 : 78);
        XboxMenuDrawRect( Canvas, 64, Y+2, 74, Y+12, MarkerR, MarkerG, MarkerB, bCurrent ? 0.96f : 0.72f );

        TCHAR IndexText[16];
        appSprintf( IndexText, TEXT("%02i"), MatchIndex - FirstMatch + 1 );
        XboxMenuText( Canvas, SmallFont, 82, Y, bCurrent ? 255 : 140, bCurrent ? 255 : 178, bCurrent ? 255 : 212, IndexText );
        XboxMenuTextFit( Canvas, SmallFont, 108, Y, 102.0f, bCurrent ? 255 : (bCompleted ? 150 : 100), bCurrent ? 255 : (bCompleted ? 180 : 125), bCurrent ? 255 : (bCompleted ? 205 : 150), *RowTitle );

        const TCHAR* StateText = bCompleted ? TEXT("DONE") : (bCurrent ? TEXT("NEXT") : TEXT("LOCKED"));
        XboxMenuTextFit( Canvas, SmallFont, LeftPanelRight-58.0f, Y, 46.0f,
            bCurrent ? 135 : (bCompleted ? 90 : 70),
            bCurrent ? 255 : (bCompleted ? 170 : 90),
            bCurrent ? 120 : (bCompleted ? 120 : 110),
            StateText );
    }

    XboxMenuDrawRect( Canvas, 54, PanelBottom-42.0f, LeftPanelRight-12.0f, PanelBottom-12.0f, 5, 25, 58, 0.78f );
    XboxMenuText( Canvas, SmallFont, 64, PanelBottom-34.0f, 135, 170, 205, GXboxMenu.TournamentFocus == 1 ? TEXT("LADDER ASSIGNED") : TEXT("CURRENT POSITION") );
    XboxMenuTextFit( Canvas, SmallFont, 170, PanelBottom-34.0f, 88.0f, 135, 255, 120, ProgressText );

    // Right side: the current rung is presented as a match dossier, not a setup form.
    XboxMenuDrawRect( Canvas, RightPanelX, 140, ContentRight, PanelBottom, 0, 0, 0, 0.70f );
    XboxMenuDrawRect( Canvas, RightPanelX, 140, ContentRight, 144, 31, 112, 205, 0.82f );
    XboxMenuText( Canvas, MenuFont, RightPanelX+16.0f, 154, 255, 255, 255, TEXT("NEXT MATCH") );
    if( GXboxMenu.TournamentFocus == 1 )
    {
        XboxMenuDrawRect( Canvas, RightPanelX+10.0f, 172, ContentRight-10.0f, 304, 12, 82, 166, 0.46f );
        XboxMenuDrawRect( Canvas, RightPanelX+10.0f, 301, ContentRight-10.0f, 304, 28, 108, 205, 0.66f );
    }

    const FLOAT PreviewOuterX = RightPanelX + 16.0f;
    const FLOAT PreviewOuterY = 178.0f;
    const FLOAT PreviewSize = 106.0f;
    const FLOAT PreviewInnerX = PreviewOuterX + 8.0f;
    const FLOAT PreviewInnerY = PreviewOuterY + 8.0f;
    XboxMenuDrawRect( Canvas, PreviewOuterX, PreviewOuterY, PreviewOuterX+PreviewSize+16.0f, PreviewOuterY+PreviewSize+16.0f, 25, 34, 48, 0.90f );
    XboxMenuDrawRect( Canvas, PreviewInnerX, PreviewInnerY, PreviewInnerX+PreviewSize, PreviewInnerY+PreviewSize, 0, 0, 0, 0.90f );
    if( Preview )
    {
        FLOAT SrcW = Max<FLOAT>( 1.0f, (FLOAT)Preview->USize );
        FLOAT SrcH = Max<FLOAT>( 1.0f, (FLOAT)Preview->VSize );
        FLOAT Scale = Min<FLOAT>( PreviewSize / SrcW, PreviewSize / SrcH );
        FLOAT PW = SrcW * Scale;
        FLOAT PH = SrcH * Scale;
        XboxMenuDrawTexture( Canvas, Preview, PreviewInnerX + (PreviewSize - PW) * 0.5f, PreviewInnerY + (PreviewSize - PH) * 0.5f, PW, PH );
    }
    else
    {
        XboxMenuTextFit( Canvas, MenuFont, PreviewInnerX+14.0f, PreviewInnerY+PreviewSize*0.46f, PreviewSize-28.0f, 210, 230, 245, TEXT("NO PREVIEW") );
    }

    const FLOAT MetaX = PreviewOuterX + PreviewSize + 30.0f;
    const FLOAT MetaWidth = Max<FLOAT>( 40.0f, ContentRight - MetaX - 14.0f );
    XboxMenuText( Canvas, SmallFont, MetaX, 180, 140, 178, 212, TEXT("MATCH") );
    XboxMenuTextFit( Canvas, SmallFont, MetaX, 198, MetaWidth, 255, 255, 255, *Title );
    XboxMenuText( Canvas, SmallFont, MetaX, 222, 140, 178, 212, TEXT("ARENA") );
    XboxMenuTextFit( Canvas, SmallFont, MetaX, 240, MetaWidth, 210, 230, 245, *Map );
    XboxMenuText( Canvas, SmallFont, MetaX, 264, 140, 178, 212, TEXT("RULES") );
    XboxMenuTextFit( Canvas, SmallFont, MetaX, 282, MetaWidth, 255, 255, 255, RuleText );

    const FLOAT BriefTop = 312.0f;
    XboxMenuDrawRect( Canvas, RightPanelX+16.0f, BriefTop, ContentRight-16.0f, BriefTop+38.0f, 5, 25, 58, 0.78f );
    XboxMenuText( Canvas, SmallFont, RightPanelX+26.0f, BriefTop+5.0f, 140, 178, 212, TEXT("BRIEFING") );
    const TCHAR* BriefText = Description.Len() ? *Description : RuleText;
    XboxMenuTextWrap( Canvas, SmallFont, RightPanelX+96.0f, BriefTop+5.0f, ContentRight-RightPanelX-122.0f, 2, 15.0f, 180, 205, 230, BriefText );

    const FLOAT SkillY = 365.0f;
    if( GXboxMenu.TournamentFocus == 2 )
    {
        XboxMenuDrawRect( Canvas, RightPanelX+12.0f, SkillY-7.0f, ContentRight-12.0f, SkillY+18.0f, 12, 82, 166, 0.62f );
        XboxMenuDrawRect( Canvas, RightPanelX+12.0f, SkillY+15.0f, ContentRight-12.0f, SkillY+18.0f, 28, 108, 205, 0.72f );
    }
    XboxMenuText( Canvas, MenuFont, RightPanelX+24.0f, SkillY, GXboxMenu.TournamentFocus == 2 ? 255 : 140, GXboxMenu.TournamentFocus == 2 ? 255 : 178, GXboxMenu.TournamentFocus == 2 ? 255 : 212, TEXT("DIFFICULTY") );
    XboxMenuText( Canvas, MenuFont, RightPanelX+144.0f, SkillY, 180, 215, 245, TEXT("<") );
    XboxMenuTextFit( Canvas, MenuFont, RightPanelX+166.0f, SkillY, ContentRight-RightPanelX-208.0f, GXboxMenu.TournamentFocus == 2 ? 255 : 180, GXboxMenu.TournamentFocus == 2 ? 255 : 205, GXboxMenu.TournamentFocus == 2 ? 255 : 230, GXboxSkillLabels[Clamp<INT>(GXboxMenu.TournamentSkill, 0, ARRAY_COUNT(GXboxSkillLabels)-1)] );
    XboxMenuText( Canvas, MenuFont, ContentRight-32.0f, SkillY, 180, 215, 245, TEXT(">") );

}

static void XboxMenuDrawTournamentPostMatch( UCanvas* Canvas )
{
    XboxMenuDrawChromeCommands( Canvas, TEXT("TOURNAMENT RESULT"), "button_a.xui", TEXT("LADDER"), "button_b.xui", TEXT("BACK") );
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;

    if( !GXboxTournamentPostMatch.Valid )
    {
        XboxMenuText( Canvas, MenuFont, 46, 70, 255, 255, 255, TEXT("TOURNAMENT RESULT") );
        XboxMenuText( Canvas, MenuFont, 58, 170, 135, 170, 205, TEXT("NO TOURNAMENT RESULT IS AVAILABLE") );
        return;
    }

    INT FirstMatch = GXboxTournamentLadders[GXboxTournamentPostMatch.LadderIndex].FirstRatedMatch;
    INT MatchCount = XboxTournamentMatchCount( GXboxTournamentPostMatch.LadderIndex );
    INT RatedCount = Max<INT>( 1, MatchCount - FirstMatch );
    INT DisplayCompleted = Clamp<INT>( GXboxTournamentPostMatch.CompletedMatch - FirstMatch + 1, 1, RatedCount );
    INT DisplayNext = Clamp<INT>( GXboxTournamentPostMatch.NextMatch - FirstMatch + 1, 1, RatedCount );
    UBOOL bHasNextMatch = GXboxTournamentPostMatch.Advanced && (GXboxTournamentPostMatch.NextMatch > GXboxTournamentPostMatch.CompletedMatch);

    TCHAR RungText[64];
    TCHAR NextText[96];
    TCHAR ResultText[96];
    appSprintf( RungText, TEXT("%02i OF %02i"), DisplayCompleted, RatedCount );
    if( GXboxTournamentPostMatch.Advanced )
    {
        if( bHasNextMatch )
            appSprintf( NextText, TEXT("NEXT MATCH %02i"), DisplayNext );
        else
            appStrcpy( NextText, TEXT("LADDER COMPLETE") );
        appStrcpy( ResultText, TEXT("LADDER ADVANCED") );
    }
    else
    {
        appStrcpy( NextText, TEXT("RETRY MATCH") );
        appStrcpy( ResultText, TEXT("POSITION UNCHANGED") );
    }

    const TCHAR* OutcomeText = GXboxTournamentPostMatch.Advanced ? TEXT("VICTORY") : TEXT("DEFEAT");
    const TCHAR* ConsequenceLine1 = TEXT("");
    const TCHAR* ConsequenceLine2 = TEXT("");
    if( GXboxTournamentPostMatch.Advanced )
    {
        ConsequenceLine1 = bHasNextMatch ? TEXT("LADDER MARKER MOVED FORWARD.") : TEXT("THIS LADDER IS COMPLETE.");
        ConsequenceLine2 = bHasNextMatch ? TEXT("RETURN TO INSPECT NEXT OPPONENT.") : TEXT("RETURN FOR THE NEXT EVENT.");
    }
    else
    {
        ConsequenceLine1 = TEXT("NO ADVANCEMENT AWARDED.");
        ConsequenceLine2 = TEXT("REPLAY THIS RUNG TO ADVANCE.");
    }

    const FLOAT ContentRight = XboxMenuContentRight(Canvas);
    const FLOAT PanelBottom = XboxMenuContentBottom(Canvas);
    const FLOAT LeftPanelRight = 282.0f;
    const FLOAT RightPanelX = 294.0f;

    XboxMenuText( Canvas, MenuFont, 46, 58, 255, 255, 255, TEXT("TOURNAMENT RESULT") );
    XboxMenuDrawRect( Canvas, 42, 86, ContentRight, 128, 0, 0, 0, 0.54f );
    XboxMenuDrawRect( Canvas, 42, 86, ContentRight, 90, GXboxTournamentPostMatch.Advanced ? 38 : 150, GXboxTournamentPostMatch.Advanced ? 142 : 46, GXboxTournamentPostMatch.Advanced ? 80 : 52, 0.92f );
    XboxMenuText( Canvas, MenuFont, 58, 98, GXboxTournamentPostMatch.Advanced ? 135 : 255, GXboxTournamentPostMatch.Advanced ? 255 : 190, GXboxTournamentPostMatch.Advanced ? 120 : 90, OutcomeText );
    XboxMenuTextFit( Canvas, MenuFont, 176, 98, ContentRight-190.0f, 210, 230, 245, ResultText );
    XboxMenuTextFit( Canvas, SmallFont, 58, 116, ContentRight-76.0f, 135, 170, 205, GXboxTournamentLadders[GXboxTournamentPostMatch.LadderIndex].Label );

    XboxMenuDrawRect( Canvas, 42, 140, LeftPanelRight, PanelBottom, 0, 0, 0, 0.70f );
    XboxMenuDrawRect( Canvas, 42, 140, LeftPanelRight, 144, 31, 112, 205, 0.82f );
    XboxMenuText( Canvas, MenuFont, 58, 154, 255, 255, 255, TEXT("LADDER PROGRESS") );
    XboxMenuText( Canvas, SmallFont, 58, 176, 135, 170, 205, RungText );

    const INT VisibleRows = 6;
    INT MaxFirstVisible = Max<INT>( FirstMatch, MatchCount - VisibleRows );
    INT AnchorMatch = GXboxTournamentPostMatch.Advanced ? GXboxTournamentPostMatch.NewPosition : GXboxTournamentPostMatch.CompletedMatch;
    INT FirstVisible = Clamp<INT>( AnchorMatch - 2, FirstMatch, MaxFirstVisible );
    for( INT Row=0; Row<VisibleRows; Row++ )
    {
        INT MatchIndex = FirstVisible + Row;
        if( MatchIndex >= MatchCount )
            break;

        FString RowTitle;
        XboxTournamentStringAt( GXboxTournamentPostMatch.LadderIndex, TEXT("MapTitle"), MatchIndex, RowTitle );
        if( RowTitle.Len() <= 0 )
            RowTitle = XboxTournamentFullMap( GXboxTournamentPostMatch.LadderIndex, MatchIndex );

        UBOOL bRetryRow = !GXboxTournamentPostMatch.Advanced && MatchIndex == GXboxTournamentPostMatch.CompletedMatch;
        UBOOL bClearedRow = GXboxTournamentPostMatch.Advanced && MatchIndex == GXboxTournamentPostMatch.CompletedMatch;
        UBOOL bNextRow = bHasNextMatch && MatchIndex == GXboxTournamentPostMatch.NextMatch;
        UBOOL bDoneRow = GXboxTournamentPostMatch.Advanced && MatchIndex < GXboxTournamentPostMatch.CompletedMatch;
        UBOOL bFocusRow = bRetryRow || bClearedRow || bNextRow;

        FLOAT Y = 200.0f + Row * 26.0f;
        if( bFocusRow )
            XboxMenuDrawRect( Canvas, 54, Y-4, LeftPanelRight-14.0f, Y+17, bRetryRow ? 120 : 12, bRetryRow ? 42 : 82, bRetryRow ? 45 : 166, 0.62f );

        TCHAR IndexText[16];
        appSprintf( IndexText, TEXT("%02i"), MatchIndex - FirstMatch + 1 );
        XboxMenuText( Canvas, SmallFont, 62, Y, bFocusRow ? 255 : 140, bFocusRow ? 255 : 178, bFocusRow ? 255 : 212, IndexText );
        XboxMenuTextFit( Canvas, SmallFont, 94, Y, 106.0f, bFocusRow ? 255 : 180, bFocusRow ? 255 : 205, bFocusRow ? 255 : 230, *RowTitle );

        const TCHAR* StateText = TEXT("");
        if( bRetryRow )
            StateText = TEXT("RETRY");
        else if( bClearedRow )
            StateText = TEXT("CLEAR");
        else if( bNextRow )
            StateText = TEXT("NEXT");
        else if( bDoneRow )
            StateText = TEXT("DONE");
        XboxMenuText( Canvas, SmallFont, 216, Y, bFocusRow ? 255 : 120, bFocusRow ? 255 : 154, bFocusRow ? 255 : 190, StateText );
    }

    XboxMenuDrawRect( Canvas, RightPanelX, 140, ContentRight, PanelBottom, 0, 0, 0, 0.70f );
    XboxMenuDrawRect( Canvas, RightPanelX, 140, ContentRight, 144, 31, 112, 205, 0.82f );
    XboxMenuText( Canvas, MenuFont, RightPanelX+16.0f, 154, 255, 255, 255, TEXT("MATCH SUMMARY") );

    const FLOAT PreviewOuterX = RightPanelX + 16.0f;
    const FLOAT PreviewOuterY = 180.0f;
    const FLOAT PreviewSize = Clamp<FLOAT>( ContentRight - RightPanelX - 122.0f, 96.0f, 116.0f );
    const FLOAT PreviewInnerX = PreviewOuterX + 8.0f;
    const FLOAT PreviewInnerY = PreviewOuterY + 8.0f;
    XboxMenuDrawRect( Canvas, PreviewOuterX, PreviewOuterY, PreviewOuterX+PreviewSize+16.0f, PreviewOuterY+PreviewSize+16.0f, 25, 34, 48, 0.88f );
    XboxMenuDrawRect( Canvas, PreviewInnerX, PreviewInnerY, PreviewInnerX+PreviewSize, PreviewInnerY+PreviewSize, 0, 0, 0, 0.88f );
    UTexture* Preview = XboxMenuGetMapPreview( GXboxTournamentPostMatch.MapName );
    if( Preview )
    {
        FLOAT SrcW = Max<FLOAT>( 1.0f, (FLOAT)Preview->USize );
        FLOAT SrcH = Max<FLOAT>( 1.0f, (FLOAT)Preview->VSize );
        FLOAT Scale = Min<FLOAT>( PreviewSize / SrcW, PreviewSize / SrcH );
        FLOAT PW = SrcW * Scale;
        FLOAT PH = SrcH * Scale;
        XboxMenuDrawTexture( Canvas, Preview, PreviewInnerX + (PreviewSize - PW) * 0.5f, PreviewInnerY + (PreviewSize - PH) * 0.5f, PW, PH );
    }
    else
    {
        XboxMenuText( Canvas, SmallFont, PreviewInnerX + 25.0f, PreviewInnerY + 58.0f, 210, 230, 245, TEXT("NO PREVIEW") );
    }

    const FLOAT MetaX = PreviewOuterX + PreviewSize + 26.0f;
    const FLOAT MetaWidth = Max<FLOAT>( 40.0f, ContentRight - MetaX - 12.0f );
    XboxMenuText( Canvas, SmallFont, MetaX, 188, 140, 178, 212, TEXT("MATCH") );
    XboxMenuTextFit( Canvas, SmallFont, MetaX, 204, MetaWidth, 255, 255, 255, GXboxTournamentPostMatch.MatchTitle );
    XboxMenuText( Canvas, SmallFont, MetaX, 228, 140, 178, 212, TEXT("MAP") );
    XboxMenuTextFit( Canvas, SmallFont, MetaX, 244, MetaWidth, 210, 230, 245, GXboxTournamentPostMatch.MapName );
    XboxMenuText( Canvas, SmallFont, MetaX, 268, 140, 178, 212, TEXT("NEXT") );
    XboxMenuTextFit( Canvas, SmallFont, MetaX, 284, MetaWidth, 255, 255, 255, NextText );

    const FLOAT ConsequenceBottom = PanelBottom - 10.0f;
    const FLOAT ConsequenceTop = ConsequenceBottom - 38.0f;
    XboxMenuDrawRect( Canvas, RightPanelX+16.0f, ConsequenceTop, ContentRight-16.0f, ConsequenceBottom, 5, 25, 58, 0.78f );
    XboxMenuTextFit( Canvas, SmallFont, RightPanelX+28.0f, ConsequenceTop+6.0f, ContentRight-RightPanelX-56.0f, 135, 170, 205, ConsequenceLine1 );
    XboxMenuTextFit( Canvas, SmallFont, RightPanelX+28.0f, ConsequenceTop+22.0f, ContentRight-RightPanelX-56.0f, 135, 170, 205, ConsequenceLine2 );

}

static void XboxMenuDrawMutators( UCanvas* Canvas )
{
    XboxMenuDrawInstantAction( Canvas );

    UFont* MenuFont = Canvas->MedFont;
    const FLOAT PanelScale = 0.80f;
    const FLOAT BodyTop = 42.0f;
    const FLOAT BodyBottom = XboxMenuFooterTop(Canvas);
    const FLOAT OriginalOuterLeft = 86.0f;
    const FLOAT OriginalOuterRight = XboxMenuContentRight(Canvas);
    const FLOAT OriginalOuterTop = 54.0f;
    const FLOAT OriginalOuterBottom = BodyBottom;
    const FLOAT OuterW = (OriginalOuterRight - OriginalOuterLeft) * PanelScale;
    const FLOAT OuterH = (OriginalOuterBottom - OriginalOuterTop) * PanelScale;
    const FLOAT OuterX1 = (Canvas->ClipX - OuterW) * 0.5f;
    const FLOAT OuterY1 = BodyTop + (BodyBottom - BodyTop - OuterH) * 0.5f;
    const FLOAT OuterX2 = OuterX1 + OuterW;
    const FLOAT OuterY2 = OuterY1 + OuterH;
    const FLOAT X1 = OuterX1 + 10.0f;
    const FLOAT Y1 = OuterY1 + 10.0f;
    const FLOAT X2 = OuterX2 - 10.0f;
    const FLOAT Y2 = OuterY2 - 10.0f;
    const FLOAT ListTop = Y1 + 58.0f;
    const FLOAT RowStep = 34.0f;
    const FLOAT InternalFooterTop = Y2 - 40.0f;
    const INT MutatorCount = XboxMenuMutatorCount();
    const INT VisibleRows = Clamp<INT>(
        (INT)((InternalFooterTop - 36.0f - ListTop) / RowStep) + 1,
        1,
        5 );
    const UBOOL bCanScroll = MutatorCount > VisibleRows;
    const FLOAT ListBottom = bCanScroll ? InternalFooterTop - 8.0f : Y2 - 18.0f;
    INT Focus = Clamp<INT>( GXboxMenu.InstantMutatorChoice, 0, MutatorCount-1 );
    INT First = Focus - VisibleRows / 2;
    First = Clamp<INT>( First, 0, Max<INT>(0, MutatorCount - VisibleRows) );
    INT Last = Min<INT>( MutatorCount, First + VisibleRows );

    XboxMenuDrawRect( Canvas, OuterX1, OuterY1, OuterX2, OuterY2, 0, 0, 0, 0.88f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y2, 7, 34, 76, 0.96f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y1+4, 28, 108, 205, 0.92f );
    XboxMenuDrawRect( Canvas, X1+22, ListTop-10, X2-22, ListBottom, 2, 10, 24, 0.90f );
    XboxMenuDrawRect( Canvas, X1+22, Y1+54, X2-22, Y1+56, 31, 90, 150, 0.42f );
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
        XboxMenuTextFit( Canvas, MenuFont, X1+108, Y, X2-X1-142.0f, bFocus ? 255 : 180, bFocus ? 255 : 205, bFocus ? 255 : 230, *XboxMenuMutator(i).Label );
    }

    if( Last < MutatorCount )
        XboxMenuText( Canvas, MenuFont, X2-84, InternalFooterTop-30, 135, 190, 225, TEXT("MORE v") );

    if( bCanScroll )
    {
        XboxMenuDrawRect( Canvas, X1+22, InternalFooterTop, X2-22, Y2-10, 3, 16, 36, 0.88f );
        XboxMenuDrawRect( Canvas, X1+22, InternalFooterTop, X2-22, InternalFooterTop+2, 31, 90, 150, 0.52f );
        FLOAT ScrollX = X2 - 30.0f - XboxMenuScrollPromptWidth(Canvas);
        XboxMenuDrawScrollPrompt( Canvas, ScrollX, InternalFooterTop+6.0f );
    }

    XboxMenuDrawFooterBand( Canvas );
    XboxMenuDrawFooterCommands( Canvas, "button_a.xui", TEXT("TOGGLE"), "button_b.xui", TEXT("BACK") );
}

static void XboxMenuDrawProfileSelect( UCanvas* Canvas )
{
    XboxProfileLoadDirectory();
    INT RowCount = Max<INT>( 1, XboxProfileGateRowCount() );
    GXboxProfileGateFocus = Clamp<INT>( GXboxProfileGateFocus, 0, RowCount-1 );
    INT FocusProfile = XboxProfileIndexForGateRow( GXboxProfileGateFocus );
    const TCHAR* Action = FocusProfile >= 0 ? TEXT("LOAD") : TEXT("CREATE");
    if( GXboxSessionProfileLoaded )
        XboxMenuDrawChromeCommands( Canvas, TEXT("PROFILE"), "button_a.xui", Action, "button_b.xui", TEXT("BACK") );
    else
        XboxMenuDrawChromeCommands( Canvas, TEXT("PROFILE"), "button_a.xui", Action );

    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 46, 58, 255, 255, 255, TEXT("SELECT PROFILE") );

    const INT VisibleRows = 7;
    INT First = Clamp<INT>( GXboxProfileGateFocus - VisibleRows / 2, 0, Max<INT>(0, RowCount - VisibleRows) );
    INT Last = Min<INT>( RowCount, First + VisibleRows );
    const FLOAT ContentRight = XboxMenuContentRight(Canvas);
    const FLOAT ContentBottom = XboxMenuContentBottom(Canvas);
    const FLOAT Left = 58.0f;
    const FLOAT ListRight = Min<FLOAT>( 330.0f, ContentRight * 0.55f );
    const FLOAT PreviewLeft = ListRight + 24.0f;
    const FLOAT Top = 102.0f;
    const FLOAT RowStep = 44.0f;

    XboxMenuDrawRect( Canvas, Left-12.0f, Top-16.0f, ListRight, ContentBottom, 0, 0, 0, 0.66f );
    XboxMenuDrawRect( Canvas, Left-12.0f, Top-16.0f, ListRight, Top-12.0f, 31, 112, 205, 0.88f );
    XboxMenuDrawRect( Canvas, PreviewLeft, Top-16.0f, ContentRight, ContentBottom, 25, 34, 48, 0.72f );
    XboxMenuDrawRect( Canvas, PreviewLeft+8.0f, Top-8.0f, ContentRight-8.0f, ContentBottom-8.0f, 0, 0, 0, 0.52f );

    for( INT Row=First; Row<Last; Row++ )
    {
        FLOAT Y = Top + (Row - First) * RowStep;
        UBOOL bFocus = Row == GXboxProfileGateFocus;
        INT ProfileIndex = XboxProfileIndexForGateRow( Row );
        if( bFocus )
        {
            XboxMenuDrawRect( Canvas, Left, Y-7.0f, ListRight-12.0f, Y+21.0f, 12, 82, 166, 0.62f );
            XboxMenuDrawRect( Canvas, Left, Y+18.0f, ListRight-12.0f, Y+21.0f, 28, 108, 205, 0.72f );
        }

        if( ProfileIndex >= 0 )
            XboxMenuTextFit( Canvas, MenuFont, Left+18.0f, Y, ListRight-Left-48.0f, bFocus ? 255 : 180, bFocus ? 255 : 205, bFocus ? 255 : 230, GXboxProfiles[ProfileIndex].Name );
        else
            XboxMenuTextFit( Canvas, MenuFont, Left+18.0f, Y, ListRight-Left-48.0f, 135, 255, 120, TEXT("CREATE NEW PROFILE") );
    }

    INT PreviewPlayerClass = XboxProfilePlayerClassIndex( FocusProfile );
    if( GXboxProfilePreviewPlayerClass != PreviewPlayerClass )
    {
        XboxMenuReleaseProfilePreviewPortrait();
        GXboxProfilePreviewPlayerClass = PreviewPlayerClass;
    }
    if( PreviewPlayerClass >= 0 )
        XboxMenuDrawPlayerPortrait( Canvas, PreviewPlayerClass, PreviewLeft+8.0f, Top-8.0f, ContentRight-PreviewLeft-16.0f, ContentBottom-Top );

    if( RowCount > VisibleRows )
    {
        FLOAT ScrollX = XboxMenuContentRight(Canvas) - XboxMenuScrollPromptWidth(Canvas);
        XboxMenuDrawScrollPrompt( Canvas, ScrollX, XboxMenuFooterPromptY(Canvas) );
    }
}

static void XboxMenuDrawPlayerSetup( UXboxViewport* Viewport, UCanvas* Canvas )
{
    XboxMenuLoadPlayerState();
    XboxMenuNormalizePlayerSetupState();
    const INT Focus = Clamp<INT>( GXboxMenu.PlayerFocus, 0, XBOX_PLAYER_ROW_COUNT-1 );
    const TCHAR* TeamValue = (GXboxMenu.PlayerTeam >= 0 && GXboxMenu.PlayerTeam < ARRAY_COUNT(GXboxPlayerTeams))
        ? GXboxPlayerTeams[GXboxMenu.PlayerTeam]
        : TEXT("NONE");

    XboxMenuDrawChromeCommands( Canvas, TEXT("PLAYER SETUP"), "button_a.xui", TEXT("CHANGE"), "button_b.xui", TEXT("BACK") );
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 46, 58, 255, 255, 255, TEXT("PLAYER SETUP") );

    const FLOAT ContentRight = XboxMenuContentRight(Canvas);
    const FLOAT PreviewRight = ContentRight;
    const FLOAT PreviewLeft = Max<FLOAT>( 378.0f, PreviewRight - 226.0f );
    const FLOAT PreviewBottom = XboxMenuContentBottom(Canvas);

    XboxMenuDrawRect( Canvas, 42, 82, ContentRight, 122, 0, 0, 0, 0.62f );
    XboxMenuDrawRect( Canvas, 42, 82, ContentRight, 86, 31, 112, 205, 0.88f );
    XboxMenuText( Canvas, SmallFont, 58, 97, 140, 178, 212, TEXT("ACTIVE PROFILE") );
    XboxMenuTextFit( Canvas, MenuFont, 194, 95, ContentRight-210.0f, 255, 255, 255, GXboxProfileName );

    XboxMenuDrawRect( Canvas, PreviewLeft, 140, PreviewRight, PreviewBottom, 25, 34, 48, 0.72f );
    XboxMenuDrawRect( Canvas, PreviewLeft+10.0f, 150, PreviewRight-10.0f, PreviewBottom-10.0f, 0, 0, 0, 0.52f );
    if( !XboxMenuDrawPlayerPreviewActor( Viewport, Canvas, PreviewLeft+10.0f, 150.0f, PreviewRight-PreviewLeft-20.0f, PreviewBottom-160.0f ) )
        XboxMenuTextFit( Canvas, MenuFont, PreviewLeft+24.0f, 240, PreviewRight-PreviewLeft-48.0f, 135, 170, 205, TEXT("NO PREVIEW") );

    static const TCHAR* Labels[] = { TEXT("CHARACTER"), TEXT("TEAM COLOR") };
    const TCHAR* Values[] = { *GXboxPlayerClasses(GXboxMenu.PlayerClass).Label, TeamValue };
    for( INT Row=0; Row<XBOX_PLAYER_ROW_COUNT; Row++ )
    {
        FLOAT Y = 176.0f + Row * 54.0f;
        UBOOL bFocus = Row == Focus;
        if( bFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-7.0f, PreviewLeft-12.0f, Y+22.0f, 12, 82, 166, 0.58f );
            XboxMenuDrawRect( Canvas, 42, Y+19.0f, PreviewLeft-12.0f, Y+22.0f, 28, 108, 205, 0.68f );
        }
        XboxMenuText( Canvas, MenuFont, 58, Y, bFocus ? 255 : 140, bFocus ? 255 : 178, bFocus ? 255 : 212, Labels[Row] );
        XboxMenuText( Canvas, MenuFont, 184, Y, 180, 215, 245, TEXT("<") );
        XboxMenuTextFit( Canvas, SmallFont, 212, Y-2.0f, PreviewLeft-258.0f, bFocus ? 255 : 180, bFocus ? 255 : 205, bFocus ? 255 : 230, Values[Row] );
        XboxMenuText( Canvas, MenuFont, PreviewLeft-32.0f, Y, 180, 215, 245, TEXT(">") );
    }
}

static void XboxMenuDrawProfileName( UCanvas* Canvas )
{
    TCHAR Title[48];
    if( GXboxProfileNameMode == XPNM_MultiplayerCreate )
        appSprintf( Title, TEXT("PLAYER %i - CREATE PROFILE"), GXboxProfileNamePort + 1 );
    else
        appStrcpy( Title, TEXT("CREATE PROFILE") );
    if( GXboxProfileNameMode == XPNM_StartupCreate && XboxProfileCreatedCount() == 0 )
        XboxMenuDrawChromeCommands( Canvas, Title,
            "button_a.xui", TEXT("SELECT"),
            "button_start.xui", TEXT("DONE") );
    else
        XboxMenuDrawChromeCommands( Canvas, Title,
            "button_a.xui", TEXT("SELECT"),
            "button_b.xui", TEXT("CANCEL"),
            "button_start.xui", TEXT("DONE") );
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    const FLOAT ContentRight = XboxMenuContentRight(Canvas);
    XboxMenuTextFit( Canvas, MenuFont, 46, 58, ContentRight-46.0f, 255, 255, 255, Title );

    XboxMenuDrawRect( Canvas, 42, 84, ContentRight, 132, 0, 0, 0, 0.68f );
    XboxMenuDrawRect( Canvas, 42, 84, ContentRight, 88, 31, 112, 205, 0.88f );
    XboxMenuText( Canvas, SmallFont, 58, 102, 140, 178, 212, TEXT("NAME") );
    XboxMenuTextFit( Canvas, MenuFont, 132, 100, ContentRight-208.0f, 255, 255, 255, GXboxProfileEditName[0] ? GXboxProfileEditName : TEXT("_") );
    TCHAR CountText[32];
    appSprintf( CountText, TEXT("%02i / %02i"), appStrlen(GXboxProfileEditName), XBOX_PROFILE_NAME_MAX );
    XboxMenuTextFit( Canvas, SmallFont, ContentRight-90.0f, 102, 76.0f, 135, 255, 120, CountText );

    const INT Columns = 8;
    const FLOAT GridX = 42.0f;
    const FLOAT GridY = 152.0f;
    const FLOAT CellStepX = (ContentRight - GridX) / (FLOAT)Columns;
    const FLOAT CellW = CellStepX - 6.0f;
    const FLOAT CellH = 31.0f;
    const FLOAT CellStepY = 42.0f;
    for( INT i=0; i<ARRAY_COUNT(GXboxProfileKeyboardKeys); i++ )
    {
        INT Row = i / Columns;
        INT Column = i % Columns;
        FLOAT X = GridX + Column * CellStepX;
        FLOAT Y = GridY + Row * CellStepY;
        UBOOL bFocus = i == GXboxProfileKeyboardFocus;
        XboxMenuDrawRect( Canvas, X, Y, X+CellW, Y+CellH, bFocus ? 12 : 3, bFocus ? 82 : 16, bFocus ? 166 : 36, bFocus ? 0.78f : 0.70f );
        if( bFocus )
            XboxMenuDrawRect( Canvas, X, Y+CellH-3.0f, X+CellW, Y+CellH, 28, 108, 205, 0.82f );
        INT XL = 0;
        INT YL = 0;
        XboxMenuTextSize( Canvas, SmallFont, GXboxProfileKeyboardKeys[i], XL, YL );
        XboxMenuTextFit( Canvas, SmallFont, X+Max<FLOAT>(4.0f, (CellW-(FLOAT)XL)*0.5f), Y+8.0f, CellW-8.0f,
            bFocus ? 255 : 180, bFocus ? 255 : 205, bFocus ? 255 : 230, GXboxProfileKeyboardKeys[i] );
    }
}

static void XboxMenuDrawSplitReadySlot( UCanvas* Canvas, INT Port, FLOAT X, FLOAT Y, FLOAT W, FLOAT H )
{
    XboxSplitReadyEnsure();
    UFont* MenuFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;
    FXboxSplitReadySlot& Slot = GXboxSplitReadySlots[Port];

    XboxMenuDrawRect( Canvas, X, Y, X+W, Y+H, 25, 34, 48, Slot.Joined ? 0.72f : 0.46f );
    XboxMenuDrawRect( Canvas, X+4, Y+4, X+W-4, Y+H-4, 0, 0, 0, Slot.Joined ? 0.50f : 0.34f );
    XboxMenuDrawRect( Canvas, X, Y, X+W, Y+3, 31, 112, 205, Slot.Joined ? 0.85f : 0.40f );

    TCHAR PortLabel[8];
    appSprintf( PortLabel, TEXT("P%i"), Port + 1 );
    XboxMenuDrawRect( Canvas, X+8, Y+8, X+W-8, Y+29, 8, 36, 72, Slot.Joined ? 0.72f : 0.46f );
    XboxMenuTextFit( Canvas, MenuFont, X+16, Y+12, W-32.0f,
        Slot.Joined ? 255 : 150, Slot.Joined ? 255 : 180, Slot.Joined ? 255 : 205, PortLabel );

    if( !Slot.Joined )
    {
        XboxMenuTextFit( Canvas, MenuFont, X+14.0f, Y+H*0.45f, W-28.0f, 180, 215, 245, TEXT("PRESS A") );
        XboxMenuTextFit( Canvas, MenuFont, X+14.0f, Y+H*0.45f+18.0f, W-28.0f, 180, 215, 245, TEXT("LOAD PROFILE") );
        return;
    }

    const FXboxPlayerClassOption& Player = XboxSplitReadyPlayerClass( Port );
    INT ProfileIndex = Slot.Profile;
    const TCHAR* ProfileName = (ProfileIndex >= 0 && ProfileIndex < XBOX_PROFILE_COUNT && GXboxProfiles[ProfileIndex].Created)
        ? GXboxProfiles[ProfileIndex].Name
        : TEXT("PROFILE REQUIRED");
    const TCHAR* TeamValue = (Slot.Team >= 0 && Slot.Team < ARRAY_COUNT(GXboxPlayerTeams))
        ? GXboxPlayerTeams[Slot.Team]
        : TEXT("NONE");

    const FLOAT PortraitAreaX = X + 8.0f;
    const FLOAT PortraitAreaY = Y + 36.0f;
    const FLOAT PortraitAreaW = W - 16.0f;
    const FLOAT PortraitAreaH = Max<FLOAT>( 48.0f, H - 156.0f );
    FLOAT PortraitH = PortraitAreaH;
    FLOAT PortraitW = PortraitH * (256.0f / 512.0f);
    if( PortraitW > PortraitAreaW )
    {
        PortraitW = PortraitAreaW;
        PortraitH = PortraitW * (512.0f / 256.0f);
    }
    const FLOAT PortraitX = PortraitAreaX + (PortraitAreaW - PortraitW) * 0.5f;
    const FLOAT PortraitY = PortraitAreaY + (PortraitAreaH - PortraitH) * 0.5f;
    if( Player.PortraitName[0] )
        XboxRenderDrawMenuTexture( Canvas->Frame, Player.PortraitName, PortraitX, PortraitY, PortraitW, PortraitH, Slot.Locked ? 0.72f : 1.0f );

    FLOAT RowY = Y + H - 108.0f;
    if( !Slot.Locked && Slot.Focus == 0 )
        XboxMenuDrawRect( Canvas, X+8, RowY-5, X+W-8, RowY+17, 12, 82, 166, 0.55f );
    if( !Slot.Locked && Slot.Focus == 0 )
    {
        XboxMenuText( Canvas, MenuFont, X+12, RowY, 180, 215, 245, TEXT("<") );
        XboxMenuText( Canvas, MenuFont, X+W-20, RowY, 180, 215, 245, TEXT(">") );
    }
    XboxMenuTextFit( Canvas, MenuFont, X+28, RowY, W-56.0f, Slot.Locked ? 120 : 255, Slot.Locked ? 150 : 255, Slot.Locked ? 180 : 255, ProfileName );

    RowY += 22.0f;
    if( !Slot.Locked && Slot.Focus == 1 )
        XboxMenuDrawRect( Canvas, X+8, RowY-5, X+W-8, RowY+17, 12, 82, 166, 0.55f );
    if( !Slot.Locked && Slot.Focus == 1 )
    {
        XboxMenuText( Canvas, MenuFont, X+12, RowY, 180, 215, 245, TEXT("<") );
        XboxMenuText( Canvas, MenuFont, X+W-20, RowY, 180, 215, 245, TEXT(">") );
    }
    XboxMenuTextFit( Canvas, MenuFont, X+28, RowY, W-56.0f, Slot.Locked ? 120 : 255, Slot.Locked ? 150 : 255, Slot.Locked ? 180 : 255, *Player.Label );

    RowY += 22.0f;
    if( !Slot.Locked && Slot.Focus == 2 )
        XboxMenuDrawRect( Canvas, X+8, RowY-5, X+W-8, RowY+17, 12, 82, 166, 0.55f );
    XboxMenuText( Canvas, MenuFont, X+12, RowY, Slot.Locked ? 120 : 180, Slot.Locked ? 150 : 215, Slot.Locked ? 180 : 245, TEXT("TEAM") );
    XboxMenuTextFit( Canvas, MenuFont, X+62, RowY, W-74.0f, Slot.Locked ? 120 : 255, Slot.Locked ? 150 : 255, Slot.Locked ? 180 : 255, TeamValue );

    if( Slot.Locked )
    {
        XboxMenuDrawRect( Canvas, X+12, Y+H-32, X+W-12, Y+H-10, 18, 92, 48, 0.70f );
        XboxMenuTextFit( Canvas, MenuFont, X+22, Y+H-28, W-44.0f, 135, 255, 120, TEXT("READY") );
    }
    else
    {
        XboxMenuTextFit( Canvas, MenuFont, X+12, Y+H-28, W-24.0f, 135, 170, 205, TEXT("A LOCKS IN") );
    }
}

static void XboxMenuDrawReadyFooter( UCanvas* Canvas, UBOOL bSystemLink )
{
    XboxSplitReadyEnsure();
    FXboxSplitReadySlot& Slot = GXboxSplitReadySlots[0];
    const char* Image1 = NULL;
    const TCHAR* Label1 = NULL;
    const char* Image2 = NULL;
    const TCHAR* Label2 = NULL;
    const char* Image3 = NULL;
    const TCHAR* Label3 = NULL;

    if( !Slot.Joined )
    {
        Image1 = "button_a.xui";
        Label1 = TEXT("JOIN");
        if( XboxSplitReadyJoinedCount() == 0 )
        {
            Image2 = "button_b.xui";
            Label2 = TEXT("BACK");
        }
    }
    else if( !Slot.Locked )
    {
        Image1 = "button_a.xui";
        Label1 = TEXT("LOCK");
        Image2 = "button_b.xui";
        Label2 = TEXT("BACK");
        Image3 = "button_x.xui";
        Label3 = TEXT("NEW PROFILE");
    }
    else
    {
        Image1 = "button_b.xui";
        Label1 = TEXT("UNLOCK");
    }

    UBOOL bShowStart = bSystemLink
        ? (!GXboxSystemLink.ReadyConfirmed && XboxSystemLinkGroupMachineCount() >= 2 && XboxSystemLinkLocalReadyCanConfirm())
        : XboxSplitReadyCanBegin();
    if( bShowStart )
    {
        if( !Image2 )
        {
            Image2 = "button_start.xui";
            Label2 = bSystemLink ? TEXT("CONFIRM") : TEXT("CONTINUE");
        }
        else
        {
            Image3 = "button_start.xui";
            Label3 = bSystemLink ? TEXT("CONFIRM") : TEXT("CONTINUE");
        }
    }

    XboxMenuDrawFooterCommands( Canvas, Image1, Label1, Image2, Label2, Image3, Label3 );
}

static void XboxMenuDrawSplitReady( UCanvas* Canvas )
{
    XboxMenuDrawChromeBase( Canvas, TEXT("SPLITSCREEN") );
    XboxMenuDrawReadyFooter( Canvas, 0 );
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 46, 64, 255, 255, 255, TEXT("SPLITSCREEN READY") );

    FLOAT X = 22.0f;
    FLOAT Y = 92.0f;
    FLOAT Gap = 8.0f;
    FLOAT Right = XboxMenuContentRight(Canvas) - 4.0f;
    FLOAT W = (Right - X - Gap * 3.0f) * 0.25f;
    FLOAT H = Clamp<FLOAT>( XboxMenuFooterTop(Canvas) - Y - 44.0f, 218.0f, 264.0f );
    for( INT i=0; i<4; i++ )
        XboxMenuDrawSplitReadySlot( Canvas, i, X + i * (W + Gap), Y, W, H );

    const TCHAR* ReadyHint = XboxSplitReadyCanBegin()
        ? TEXT("PLAYER 1 START BEGINS MAP SELECTION")
        : TEXT("P1-P4 MATCH CONTROLLER PORTS; JOINED PLAYERS NEED UNIQUE PROFILES");
    FLOAT ReadyHintY = XboxMenuAboveFooterTextY( Canvas, MenuFont, ReadyHint, 7.0f );
    if( XboxSplitReadyCanBegin() )
        XboxMenuTextFit( Canvas, MenuFont, 58, ReadyHintY, XboxMenuContentRight(Canvas)-76.0f, 135, 255, 120, ReadyHint );
    else
        XboxMenuTextFit( Canvas, MenuFont, 58, ReadyHintY, XboxMenuContentRight(Canvas)-76.0f, 135, 170, 205, ReadyHint );
}

static void XboxMenuDrawSplitMapSelect( UCanvas* Canvas )
{
    static const TCHAR* Labels[] =
    {
        TEXT("GAMETYPE"),
        TEXT("ARENA"),
        TEXT("SCORE LIMIT"),
        TEXT("TIME LIMIT"),
        TEXT("BEGIN MATCH")
    };

    GXboxMenu.InstantGameType = Clamp<INT>( GXboxMenu.InstantGameType, 0, XboxMenuGameTypeCount()-1 );
    INT MapCount = XboxInstantMapList( GXboxMenu.InstantGameType );
    const FXboxDiscoveredOption& Game = XboxMenuGameType( GXboxMenu.InstantGameType );
    UBOOL bLives = XboxMenuGameUsesLives( *Game.URLValue );
    const FXboxDiscoveredOption* Map = NULL;
    if( MapCount > 0 )
    {
        INT MapIndex = Clamp<INT>( GXboxMenu.InstantMap[GXboxMenu.InstantGameType], 0, MapCount-1 );
        Map = &XboxMenuMap( GXboxMenu.InstantGameType, MapIndex );
    }
    UTexture* Preview = Map ? XboxMenuGetMapPreview( *Map->URLValue ) : NULL;

    TCHAR FragValue[16];
    TCHAR TimeValue[24];
    INT FragLimit = GXboxFragLimits[Clamp<INT>(GXboxMenu.InstantFragLimit, 0, ARRAY_COUNT(GXboxFragLimits)-1)];
    if( FragLimit > 0 )
        appSprintf( FragValue, TEXT("%i"), FragLimit );
    else
        appStrcpy( FragValue, TEXT("NONE") );

    INT TimeLimit = bLives ? 0 : GXboxTimeLimits[Clamp<INT>(GXboxMenu.InstantTimeLimit, 0, ARRAY_COUNT(GXboxTimeLimits)-1)];
    if( TimeLimit > 0 )
        appSprintf( TimeValue, TEXT("%i MINUTES"), TimeLimit );
    else
        appStrcpy( TimeValue, TEXT("NONE") );

    const TCHAR* Values[] =
    {
        *Game.Label,
        Map ? *Map->Label : TEXT("NO MAPS"),
        FragValue,
        TimeValue,
        TEXT("")
    };
    UBOOL bSystemLink = GXboxMenu.Screen == XMS_SystemLinkMapSelect;
    UBOOL bEditable = !bSystemLink || GXboxSystemLink.Role == XSLR_Host;
    if( bEditable )
        XboxMenuDrawChrome( Canvas, bSystemLink ? TEXT("SYSTEM LINK") : TEXT("SPLITSCREEN"), 1 );
    else
        XboxMenuDrawChromeCommands( Canvas, TEXT("SYSTEM LINK"), "button_b.xui", TEXT("BACK") );
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuText( Canvas, MenuFont, 46, 70, 255, 255, 255, bSystemLink ? TEXT("SYSTEM LINK MATCH") : TEXT("SPLITSCREEN MATCH") );
    const FLOAT PreviewOuterRight = XboxMenuContentRight(Canvas);
    const FLOAT PreviewSize = Clamp<FLOAT>( PreviewOuterRight - 362.0f - 20.0f, 120.0f, 188.0f );
    const FLOAT PreviewOuterX = PreviewOuterRight - PreviewSize - 20.0f;
    const FLOAT PreviewOuterY = 92.0f;
    const FLOAT PreviewInnerX = PreviewOuterX + 10.0f;
    const FLOAT PreviewInnerY = PreviewOuterY + 10.0f;
    XboxMenuDrawRect( Canvas, PreviewOuterX, PreviewOuterY, PreviewOuterRight, PreviewOuterY+PreviewSize+20.0f, 25, 34, 48, 0.88f );
    XboxMenuDrawRect( Canvas, PreviewInnerX, PreviewInnerY, PreviewInnerX+PreviewSize, PreviewInnerY+PreviewSize, 0, 0, 0, 0.88f );
    if( Preview )
    {
        FLOAT SrcW = Max<FLOAT>( 1.0f, (FLOAT)Preview->USize );
        FLOAT SrcH = Max<FLOAT>( 1.0f, (FLOAT)Preview->VSize );
        FLOAT Scale = Min<FLOAT>( PreviewSize / SrcW, PreviewSize / SrcH );
        FLOAT PW = SrcW * Scale;
        FLOAT PH = SrcH * Scale;
        XboxMenuDrawTexture( Canvas, Preview, PreviewInnerX + (PreviewSize - PW) * 0.5f, PreviewInnerY + (PreviewSize - PH) * 0.5f, PW, PH );
    }
    else
    {
        XboxMenuTextFit( Canvas, MenuFont, PreviewInnerX+14.0f, PreviewInnerY+PreviewSize*0.46f, PreviewSize-28.0f, 210, 230, 245, TEXT("NO PREVIEW") );
    }

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        const TCHAR* RowLabel = (bLives && i == 2) ? TEXT("LIVES") : Labels[i];
        const TCHAR* RowValue = Values[i];
        FLOAT Y = 164.0f + i * 36.0f;
        if( bEditable && i == GXboxMenu.SplitFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-6, 350, Y+18, 12, 82, 166, 0.55f );
            if( i < 4 && !(bLives && i == 3) )
            {
                XboxMenuText( Canvas, MenuFont, 192, Y, 180, 215, 245, TEXT("<") );
                XboxMenuText( Canvas, MenuFont, 334, Y, 180, 215, 245, TEXT(">") );
            }
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, RowLabel );
            XboxMenuTextFit( Canvas, MenuFont, 210, Y, 120.0f, 255, 255, 255, RowValue );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, RowLabel );
            XboxMenuTextFit( Canvas, MenuFont, 210, Y, 120.0f, 180, 205, 230, RowValue );
        }
    }

    const TCHAR* SetupHint = bSystemLink && !bEditable ? TEXT("WAITING FOR HOST MATCH SETUP") :
        bSystemLink ? TEXT("HOST PLAYER 1 CONTROLS MATCH SETUP") : TEXT("PLAYER 1 CONTROLS MATCH SETUP");
    XboxMenuTextFit( Canvas, MenuFont, 58, XboxMenuAboveFooterTextY(Canvas, MenuFont, SetupHint, 8.0f),
        XboxMenuContentRight(Canvas)-76.0f, 135, 170, 205, SetupHint );
}

static const TCHAR* XboxControlPresetLabel( UXboxClient* Client )
{
    if( !Client || Client->ControlPreset < 0 )
        return TEXT("CUSTOM");
    INT Preset = Clamp<INT>( Client->ControlPreset, 0, ARRAY_COUNT(GXboxControlPresets)-1 );
    return GXboxControlPresets[Preset].Label;
}

static void XboxMenuDrawControlsFacade( UXboxViewport* Viewport, UCanvas* Canvas )
{
    const FLOAT MaxSize = 214.0f;
    const FLOAT LeftEdge = 356.0f;
    const FLOAT RightEdge = Canvas->ClipX - 18.0f;
    FLOAT Size = Clamp<FLOAT>( RightEdge - LeftEdge, 160.0f, MaxSize );
    FLOAT X = RightEdge - Size;
    FLOAT Y = 235.0f - Size * 0.5f;
    XboxMenuDrawImage( Canvas, "controller_s.xui", X, Y, Size, Size, 0.96f );
}

static void XboxMenuDrawControls( UXboxViewport* Viewport, UCanvas* Canvas )
{
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont;

    XboxMenuDrawChromeCommands( Canvas, TEXT("CONTROLS"),
        "button_a.xui", TEXT("CHANGE"),
        "button_b.xui", TEXT("BACK"),
        "button_y.xui", TEXT("DEFAULT") );
    XboxMenuText( Canvas, MenuFont, 46, 58, 255, 255, 255, TEXT("CONTROLS") );

    TCHAR LookValue[32];
    TCHAR MoveValue[32];
    TCHAR DeadZoneValue[32];
    appSprintf( LookValue, TEXT("%i"), Client ? (INT)Client->ScaleRUV : 100 );
    appSprintf( MoveValue, TEXT("%i"), Client ? (INT)Client->ScaleXYZ : 100 );
    appSprintf( DeadZoneValue, TEXT("%i%%"), Client ? (INT)(Client->DeadZone * 100.0f + 0.5f) : 20 );
    INT Hand = XboxMenuWeaponHandIndex( Player );
    UBOOL AutoSwitch = Player ? !Player->bNeverAutoSwitch : 1;

    static const TCHAR* ControlLabels[] =
    {
        TEXT("LOOK SENSITIVITY"),
        TEXT("MOVE SENSITIVITY"),
        TEXT("INVERT Y"),
        TEXT("STICK DEADZONE"),
        TEXT("PRESET"),
        TEXT("STICK LAYOUT"),
        TEXT("WEAPON HAND"),
        TEXT("WEAPON AUTO-SWITCH")
    };
    const TCHAR* ControlValues[] =
    {
        LookValue,
        MoveValue,
        Client && Client->InvertVertical ? TEXT("ON") : TEXT("OFF"),
        DeadZoneValue,
        XboxControlPresetLabel(Client),
        XboxStickLayoutLabel( Client ? Client->StickLayout : XSL_Default ),
        GXboxWeaponHands[Hand],
        AutoSwitch ? TEXT("ON") : TEXT("OFF")
    };

    const FLOAT RowStartY = 80.0f;
    const FLOAT RowStepY = 21.0f;
    const INT TotalRows = XboxControlsRowCount();
    const INT VisibleRows = XboxControlsVisibleRows();
    const INT ScrollTop = XboxControlsScrollTop();
    const INT ScrollLast = Min<INT>( TotalRows, ScrollTop + VisibleRows );

    for( INT Row=ScrollTop; Row<ScrollLast; Row++ )
    {
        FLOAT Y = RowStartY + (Row - ScrollTop) * RowStepY;
        UBOOL bFocus = GXboxMenu.ControlsFocus == Row;

        if( Row < XCR_FirstButton )
        {
            UBOOL bSlider = Row == XCR_LookSensitivity || Row == XCR_MoveSensitivity || Row == XCR_DeadZone;
            FLOAT SliderValue = 0.0f;
            FLOAT SliderMin = 0.0f;
            FLOAT SliderMax = 1.0f;

            if( Row == XCR_LookSensitivity )
            {
                SliderValue = Client ? Client->ScaleRUV : 100.0f;
                SliderMin = 25.0f;
                SliderMax = 200.0f;
            }
            else if( Row == XCR_MoveSensitivity )
            {
                SliderValue = Client ? Client->ScaleXYZ : 100.0f;
                SliderMin = 25.0f;
                SliderMax = 200.0f;
            }
            else if( Row == XCR_DeadZone )
            {
                SliderValue = Client ? Client->DeadZone : 0.20f;
                SliderMin = 0.05f;
                SliderMax = 0.40f;
            }

            if( bFocus )
            {
                XboxMenuDrawRect( Canvas, 42, Y-4, 344, Y+15, 12, 82, 166, 0.55f );
                XboxMenuText( Canvas, SmallFont, 58, Y, 255, 255, 255, ControlLabels[Row] );
                XboxMenuText( Canvas, SmallFont, 190, Y, 180, 215, 245, TEXT("<") );
                XboxMenuText( Canvas, SmallFont, 332, Y, 180, 215, 245, TEXT(">") );
                if( bSlider )
                {
                    XboxMenuDrawSlider( Canvas, 204, Y+2, 58, SliderValue, SliderMin, SliderMax );
                    XboxMenuText( Canvas, SmallFont, 268, Y, 255, 255, 255, ControlValues[Row] );
                }
                else
                {
                    XboxMenuTextFit( Canvas, SmallFont, 210, Y, 116, 255, 255, 255, ControlValues[Row] );
                }
            }
            else
            {
                XboxMenuText( Canvas, SmallFont, 58, Y, 140, 178, 212, ControlLabels[Row] );
                if( bSlider )
                {
                    XboxMenuDrawSlider( Canvas, 204, Y+2, 58, SliderValue, SliderMin, SliderMax );
                    XboxMenuText( Canvas, SmallFont, 268, Y, 180, 205, 230, ControlValues[Row] );
                }
                else
                {
                    XboxMenuTextFit( Canvas, SmallFont, 210, Y, 116, 180, 205, 230, ControlValues[Row] );
                }
            }
        }
        else
        {
            INT Button = Row - XCR_FirstButton;
            INT Action = XboxControlButtonAction( Client, Button );
            if( bFocus )
            {
                XboxMenuDrawRect( Canvas, 42, Y-3, 312, Y+14, 12, 82, 166, 0.55f );
                XboxMenuDrawImage( Canvas, GXboxControlButtons[Button].Image, 58, Y-3, 16, 16 );
                XboxMenuText( Canvas, SmallFont, 92, Y, 180, 215, 245, TEXT("<") );
                XboxMenuTextFit( Canvas, SmallFont, 116, Y, 150, 255, 255, 255, GXboxControlActions[Action].Label );
                XboxMenuText( Canvas, SmallFont, 278, Y, 180, 215, 245, TEXT(">") );
            }
            else
            {
                XboxMenuDrawImage( Canvas, GXboxControlButtons[Button].Image, 58, Y-3, 16, 16 );
                XboxMenuTextFit( Canvas, SmallFont, 116, Y, 166, 180, 205, 230, GXboxControlActions[Action].Label );
            }
        }
    }

    if( ScrollTop > 0 )
        XboxMenuText( Canvas, SmallFont, 306, 68, 135, 190, 225, TEXT("MORE ^") );
    if( ScrollLast < TotalRows )
        XboxMenuText( Canvas, SmallFont, 306, 328.0f, 135, 190, 225, TEXT("MORE v") );

    XboxMenuDrawControlsFacade( Viewport, Canvas );
    if( XboxControlsCanScroll() )
    {
        FLOAT ButtonY = XboxMenuFooterPromptY( Canvas );
        FLOAT ScrollX = Canvas->ClipX - 38.0f - XboxMenuScrollPromptWidth(Canvas);
        XboxMenuDrawScrollPrompt( Canvas, ScrollX, ButtonY );
    }
    {
        const TCHAR* Hint = TEXT("LEFT/RIGHT CHANGES SELECTED ITEM");
        XboxMenuText( Canvas, SmallFont, 58, XboxMenuAboveFooterTextY(Canvas, SmallFont, Hint, 8.0f), 135, 170, 205, Hint );
    }
}

static void XboxMenuDrawSettings( UXboxViewport* Viewport, UCanvas* Canvas )
{
    static const TCHAR* Items[] =
    {
        TEXT("CONTROLS"),
        TEXT("AUDIO"),
        TEXT("VIDEO")
    };

    XboxMenuLoadSettings();
    UFont* MenuFont = Canvas->MedFont;
    XboxMenuDrawChrome( Canvas, TEXT("SETTINGS"), 1 );
    XboxMenuText( Canvas, MenuFont, 46, 62, 255, 255, 255, TEXT("SETTINGS") );

    for( INT i=0; i<ARRAY_COUNT(Items); i++ )
    {
        FLOAT Y = 146.0f + i * 42.0f;
        if( i == GXboxMenu.SettingsFocus )
        {
            XboxMenuDrawSelectionHighlight( Canvas, MenuFont, 42.0f, 304.0f, Y, Items[i], 0.42f );
            XboxMenuText( Canvas, MenuFont, 62, Y, 255, 255, 255, Items[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 62, Y, 135, 170, 205, Items[i] );
        }
    }
}

static void XboxMenuDrawAudioSettings( UXboxViewport* Viewport, UCanvas* Canvas )
{
    static const TCHAR* Labels[] =
    {
        TEXT("MUSIC VOLUME"),
        TEXT("SOUND VOLUME"),
        TEXT("ANNOUNCER VOLUME")
    };

    XboxMenuLoadSettings();
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont;

    TCHAR MusicValue[32];
    TCHAR SoundValue[32];
    TCHAR AnnouncerValue[32];
    appSprintf( MusicValue, TEXT("%i"), GXboxSettingsMusicVolume );
    appSprintf( SoundValue, TEXT("%i"), GXboxSettingsSoundVolume );
    appSprintf( AnnouncerValue, TEXT("%i"), GXboxSettingsAnnouncerVolume );

    const TCHAR* Values[] =
    {
        MusicValue,
        SoundValue,
        AnnouncerValue
    };

    XboxMenuDrawChromeCommands( Canvas, TEXT("AUDIO"), "button_a.xui", TEXT("CHANGE"), "button_b.xui", TEXT("BACK") );
    XboxMenuText( Canvas, MenuFont, 46, 62, 255, 255, 255, TEXT("AUDIO") );
    {
        const TCHAR* Hint = TEXT("LEFT/RIGHT CHANGES SELECTED ITEM");
        XboxMenuText( Canvas, SmallFont, 58, XboxMenuAboveFooterTextY(Canvas, SmallFont, Hint, 8.0f), 135, 170, 205, Hint );
    }

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        FLOAT Y = 132.0f + i * 36.0f;
        FLOAT SliderValue = 0.0f;
        FLOAT SliderMax = 255.0f;
        if( i == XAR_MusicVolume )
            SliderValue = (FLOAT)GXboxSettingsMusicVolume;
        else if( i == XAR_SoundVolume )
            SliderValue = (FLOAT)GXboxSettingsSoundVolume;
        else
        {
            SliderValue = (FLOAT)GXboxSettingsAnnouncerVolume;
            SliderMax = 4.0f;
        }

        if( i == GXboxMenu.SettingsFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-6, 430, Y+18, 12, 82, 166, 0.55f );
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 232, Y, 180, 215, 245, TEXT("<") );
            XboxMenuDrawSlider( Canvas, 248, Y+3, 112, SliderValue, 0.0f, SliderMax );
            XboxMenuText( Canvas, MenuFont, 374, Y, 255, 255, 255, Values[i] );
            XboxMenuText( Canvas, MenuFont, 408, Y, 180, 215, 245, TEXT(">") );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, Labels[i] );
            XboxMenuDrawSlider( Canvas, 248, Y+3, 112, SliderValue, 0.0f, SliderMax );
            XboxMenuText( Canvas, MenuFont, 374, Y, 180, 205, 230, Values[i] );
        }
    }
}

static void XboxMenuDrawVideoSettings( UXboxViewport* Viewport, UCanvas* Canvas )
{
    static INT CrosshairProof = -1;
    static DOUBLE CrosshairProofStart = 0.0;
    if( XboxSmokeMarkerExists("XboxCrosshairProof.ini", CrosshairProof) && Viewport && Viewport->Actor && Viewport->Actor->myHUD )
    {
        if( CrosshairProofStart == 0.0 ) CrosshairProofStart = appSeconds();
        Viewport->Actor->myHUD->Crosshair = ((INT)((appSeconds() - CrosshairProofStart) / 12.0)) % 9;
    }
    static const TCHAR* Labels[] =
    {
        TEXT("BRIGHTNESS"),
        TEXT("CONTRAST"),
        TEXT("GAMMA"),
        TEXT("SAFE AREA SIZE"),
        TEXT("SAFE AREA X"),
        TEXT("SAFE AREA Y"),
        TEXT("CROSSHAIR"),
        TEXT("HUD COLOR"),
        TEXT("CROSSHAIR COLOR"),
        TEXT("HUD OPACITY"),
        TEXT("MATURE LANGUAGE")
    };

    XboxMenuLoadSettings();
    UXboxClient* Client = XboxMenuGetClient( Viewport );
    APlayerPawn* Player = Viewport ? Viewport->Actor : NULL;
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont;

    TCHAR SafeAreaSizeValue[32];
    TCHAR SafeAreaXValue[32];
    TCHAR SafeAreaYValue[32];
    TCHAR CrosshairValue[32];
    TCHAR OpacityValue[32];
    TCHAR MatureValue[32];
    TCHAR BrightnessValue[32];
    TCHAR ContrastValue[32];
    TCHAR GammaValue[32];
    INT Crosshair = (Player && Player->myHUD) ? Player->myHUD->Crosshair : 0;
    UBOOL bNoMature = XboxMenuGetUserBool( TEXT("Botpack.TournamentPlayer"), TEXT("bNoMatureLanguage"), 0 );

    appSprintf( BrightnessValue, TEXT("%i%%"), Client ? appRound(Client->Brightness * 100.0f) : 50 );
    appSprintf( ContrastValue, TEXT("%i%%"), Client ? appRound(Client->DisplayContrast * 100.0f) : 100 );
    appSprintf( GammaValue, TEXT("%i%%"), Client ? appRound(Client->DisplayGamma * 100.0f) : 100 );
    appSprintf( SafeAreaSizeValue, TEXT("%i%%"), Client ? Client->SafeAreaSize : 100 );
    appSprintf( SafeAreaXValue, TEXT("%i"), Client ? Client->SafeAreaX : 0 );
    appSprintf( SafeAreaYValue, TEXT("%i"), Client ? Client->SafeAreaY : 0 );
    appSprintf( CrosshairValue, TEXT("%i"), Crosshair );
    appSprintf( OpacityValue, TEXT("%i"), GXboxSettingsHudOpacity );
    appStrcpy( MatureValue, bNoMature ? TEXT("FILTERED") : TEXT("ON") );

    const TCHAR* Values[] =
    {
        BrightnessValue,
        ContrastValue,
        GammaValue,
        SafeAreaSizeValue,
        SafeAreaXValue,
        SafeAreaYValue,
        CrosshairValue,
        GXboxColorNames[GXboxSettingsHudColor],
        GXboxColorNames[GXboxSettingsCrosshairColor],
        OpacityValue,
        MatureValue
    };

    XboxMenuDrawChromeCommands( Canvas, TEXT("VIDEO"), "button_a.xui", TEXT("CHANGE"), "button_b.xui", TEXT("BACK") );
    XboxMenuText( Canvas, MenuFont, 46, 62, 255, 255, 255, TEXT("VIDEO") );
    {
        const TCHAR* Hint = TEXT("LEFT/RIGHT CHANGES SELECTED ITEM");
        XboxMenuText( Canvas, SmallFont, 58, XboxMenuAboveFooterTextY(Canvas, SmallFont, Hint, 8.0f), 135, 170, 205, Hint );
    }
    XboxMenuDrawSettingsPreview( Canvas, MenuFont, Client, Viewport, Crosshair );

    FLOAT RowRight = XboxMenuSettingsPreviewLeft(Canvas) - 16.0f;
    FLOAT RightColumnShift = RowRight - 420.0f;

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        FLOAT Y = 92.0f + i * 25.0f;
        UBOOL bSlider =
            i == XVR_Brightness ||
            i == XVR_Contrast ||
            i == XVR_Gamma ||
            i == XVR_SafeAreaSize ||
            i == XVR_SafeAreaX ||
            i == XVR_SafeAreaY ||
            i == XVR_HudOpacity;
        FLOAT SliderValue = 0.0f;
        FLOAT SliderMin = 0.0f;
        FLOAT SliderMax = 1.0f;

        if( i == XVR_Brightness )
        {
            SliderValue = Client ? Client->Brightness : 0.5f;
            SliderMin = 0.0f;
            SliderMax = 1.0f;
        }
        else if( i == XVR_Contrast )
        {
            SliderValue = Client ? Client->DisplayContrast : 1.0f;
            SliderMin = 0.5f;
            SliderMax = 1.5f;
        }
        else if( i == XVR_Gamma )
        {
            SliderValue = Client ? Client->DisplayGamma : 1.0f;
            SliderMin = 0.5f;
            SliderMax = 2.0f;
        }
        else if( i == XVR_SafeAreaSize )
        {
            SliderValue = Client ? (FLOAT)Client->SafeAreaSize : 100.0f;
            SliderMin = 85.0f;
            SliderMax = 100.0f;
        }
        else if( i == XVR_SafeAreaX )
        {
            SliderValue = Client ? (FLOAT)Client->SafeAreaX : 0.0f;
            SliderMin = -48.0f;
            SliderMax = 48.0f;
        }
        else if( i == XVR_SafeAreaY )
        {
            SliderValue = Client ? (FLOAT)Client->SafeAreaY : 0.0f;
            SliderMin = -36.0f;
            SliderMax = 36.0f;
        }
        else if( i == XVR_HudOpacity )
        {
            SliderValue = (FLOAT)GXboxSettingsHudOpacity;
            SliderMin = 1.0f;
            SliderMax = 16.0f;
        }

        if( i == GXboxMenu.SettingsFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-5, RowRight, Y+17, 12, 82, 166, 0.55f );
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 232 + RightColumnShift, Y, 180, 215, 245, TEXT("<") );
            if( bSlider )
            {
                XboxMenuDrawSlider( Canvas, 248 + RightColumnShift, Y+3, 112, SliderValue, SliderMin, SliderMax );
                XboxMenuText( Canvas, MenuFont, 374 + RightColumnShift, Y, 255, 255, 255, Values[i] );
            }
            else
            {
                XboxMenuText( Canvas, MenuFont, 270 + RightColumnShift, Y, 255, 255, 255, Values[i] );
            }
            XboxMenuText( Canvas, MenuFont, 408 + RightColumnShift, Y, 180, 215, 245, TEXT(">") );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, Labels[i] );
            if( bSlider )
            {
                XboxMenuDrawSlider( Canvas, 248 + RightColumnShift, Y+3, 112, SliderValue, SliderMin, SliderMax );
                XboxMenuText( Canvas, MenuFont, 374 + RightColumnShift, Y, 180, 205, 230, Values[i] );
            }
            else
            {
                XboxMenuText( Canvas, MenuFont, 270 + RightColumnShift, Y, 180, 205, 230, Values[i] );
            }
        }
    }
}

static void XboxMenuDrawSystemLink( UCanvas* Canvas )
{
    UFont* MenuFont = Canvas->MedFont;
    UFont* SmallFont = Canvas->SmallFont ? Canvas->SmallFont : Canvas->MedFont;

    INT MachineCount = XboxSystemLinkGroupMachineCount();
    INT ConfirmedCount = XboxSystemLinkConfirmedMachineCount();
    if( GXboxSystemLink.HostId && MachineCount >= 2 )
    {
        XboxMenuDrawChromeBase( Canvas, TEXT("SYSTEM LINK") );
        if( GXboxSystemLink.Phase != XSLP_Launching )
            XboxMenuDrawReadyFooter( Canvas, 1 );
        XboxMenuText( Canvas, MenuFont, 46, 64, 255, 255, 255, TEXT("SYSTEM LINK READY") );

        const TCHAR* ReadyStatusHint = GXboxSystemLink.Phase == XSLP_Launching ? TEXT("LAUNCHING MATCH") :
            GXboxSystemLink.Role == XSLR_Client && XboxSystemLinkFindPeerById(GXboxSystemLink.HostId) && XboxSystemLinkFindPeerById(GXboxSystemLink.HostId)->Phase == XSLP_MapSelect ? TEXT("HOST IS CHOOSING THE MATCH") :
            GXboxSystemLink.ReadyConfirmed ? TEXT("WAITING FOR ALL MACHINES TO CONFIRM") :
            XboxSystemLinkLocalReadyCanConfirm() ? TEXT("PLAYER 1 START CONFIRMS THIS XBOX") :
            TEXT("PLAYER 1 MUST JOIN AND LOCK IN");
        FLOAT HintY = XboxMenuAboveFooterTextY( Canvas, SmallFont, ReadyStatusHint, 7.0f );
        FLOAT StatusY = HintY - 18.0f;
        FLOAT X = 22.0f;
        FLOAT Y = 88.0f;
        FLOAT Gap = 8.0f;
        FLOAT Right = XboxMenuContentRight(Canvas) - 4.0f;
        FLOAT W = (Right - X - Gap * 3.0f) * 0.25f;
        FLOAT H = Clamp<FLOAT>( StatusY - Y - 10.0f, 204.0f, 256.0f );
        for( INT i=0; i<4; i++ )
            XboxMenuDrawSplitReadySlot( Canvas, i, X + i * (W + Gap), Y, W, H );

        TCHAR Status[160];
        appSprintf( Status, TEXT("%s    MACHINES %i    CONFIRMED %i/%i"),
            XboxSystemLinkRoleText(GXboxSystemLink.Role), MachineCount, ConfirmedCount, MachineCount );
        XboxMenuTextFit( Canvas, SmallFont, 58, StatusY, XboxMenuContentRight(Canvas)-76.0f, 180, 215, 245, Status );

        const FXboxSystemLinkPeer* HostPeer = XboxSystemLinkFindPeerById( GXboxSystemLink.HostId );
        if( GXboxSystemLink.Phase == XSLP_Launching )
            XboxMenuTextFit( Canvas, SmallFont, 58, HintY, XboxMenuContentRight(Canvas)-76.0f, 135, 255, 120, ReadyStatusHint );
        else if( GXboxSystemLink.Role == XSLR_Client && HostPeer && HostPeer->Phase == XSLP_MapSelect )
            XboxMenuTextFit( Canvas, SmallFont, 58, HintY, XboxMenuContentRight(Canvas)-76.0f, 135, 255, 120, ReadyStatusHint );
        else if( GXboxSystemLink.ReadyConfirmed )
            XboxMenuTextFit( Canvas, SmallFont, 58, HintY, XboxMenuContentRight(Canvas)-76.0f, 135, 170, 205, ReadyStatusHint );
        else if( XboxSystemLinkLocalReadyCanConfirm() )
            XboxMenuTextFit( Canvas, SmallFont, 58, HintY, XboxMenuContentRight(Canvas)-76.0f, 135, 255, 120, ReadyStatusHint );
        else
            XboxMenuTextFit( Canvas, SmallFont, 58, HintY, XboxMenuContentRight(Canvas)-76.0f, 135, 170, 205, ReadyStatusHint );
        return;
    }

    XboxMenuDrawChromeCommands( Canvas, TEXT("SYSTEM LINK"), "button_b.xui", TEXT("BACK") );

    XboxMenuText( Canvas, MenuFont, 58, 74, 255, 255, 255, TEXT("SYSTEM LINK") );

    TCHAR Status[128];
    if( GXboxSystemLink.Started )
        appSprintf( Status, TEXT("%s    LOCAL %08X    PORT %i"),
            XboxSystemLinkRoleText(GXboxSystemLink.Role),
            GXboxSystemLink.LocalId,
            GXboxSystemLink.LocalPort );
    else
        appSprintf( Status, TEXT("NOT STARTED    ERROR %i"), GXboxSystemLink.LastError );
    XboxMenuText( Canvas, MenuFont, 58, 114, 180, 215, 245, Status );

    UBOOL bHostIsLocal = GXboxSystemLink.HostId && GXboxSystemLink.HostId == GXboxSystemLink.LocalId;
    UBOOL bHostPeerFound = 0;
    INT HostPort = 0;
    TCHAR HostAddr[32];
    HostAddr[0] = 0;
    for( INT i=0; i<GXboxSystemLink.Peers.Num(); i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        if( Peer.Id == GXboxSystemLink.HostId )
        {
            XboxSystemLinkFormatAddress( Peer.Address, HostAddr, ARRAY_COUNT(HostAddr) );
            HostPort = Peer.Port;
            bHostPeerFound = 1;
            break;
        }
    }

    if( bHostIsLocal )
        appSprintf( Status, TEXT("GROUP HOST    THIS XBOX") );
    else if( bHostPeerFound )
        appSprintf( Status, TEXT("GROUP HOST    %08X    %s:%i"), GXboxSystemLink.HostId, HostAddr, HostPort );
    else if( GXboxSystemLink.HostId )
        appSprintf( Status, TEXT("GROUP HOST    %08X"), GXboxSystemLink.HostId );
    else
        appSprintf( Status, TEXT("GROUP HOST    SEARCHING") );
    XboxMenuText( Canvas, MenuFont, 58, 150, 255, 255, 255, Status );

    appSprintf( Status, TEXT("GROUP MEMBERS  %i"), MachineCount );
    XboxMenuText( Canvas, MenuFont, 58, 186, 135, 170, 205, Status );

    const TCHAR* DiscoveryHint = GXboxSystemLink.Role == XSLR_Host ? TEXT("HOSTING GROUP - WAITING FOR CLIENTS") :
        GXboxSystemLink.Role == XSLR_Client ? TEXT("JOINED GROUP - WAITING FOR HOST") : TEXT("SEARCHING");
    FLOAT DiscoveryHintY = XboxMenuAboveFooterTextY( Canvas, MenuFont, DiscoveryHint, 8.0f );
    const FLOAT RowStep = 24.0f;
    FLOAT Y = 218.0f;
    INT AvailableRows = Max<INT>( 1, (INT)((DiscoveryHintY - 10.0f - Y) / RowStep) + 1 );
    if( GXboxSystemLink.Started )
    {
        appSprintf( Status, TEXT("%s    THIS XBOX    %08X"), XboxSystemLinkRoleText(GXboxSystemLink.Role), GXboxSystemLink.LocalId );
        XboxMenuDrawRect( Canvas, 48, Y-4, XboxMenuContentRight(Canvas)-10.0f, Y+17, 12, 82, 166, GXboxSystemLink.Role == XSLR_Host ? 0.45f : 0.28f );
        XboxMenuTextFit( Canvas, SmallFont, 62, Y, XboxMenuContentRight(Canvas)-86.0f, 255, 255, 255, Status );
        Y += RowStep;
        AvailableRows--;
    }

    INT PeerCount = Min<INT>( GXboxSystemLink.Peers.Num(), GXboxSystemLinkMaxPeers );
    INT VisiblePeers = Min<INT>( PeerCount, Max<INT>(0, AvailableRows) );
    if( PeerCount > VisiblePeers && VisiblePeers > 0 )
        VisiblePeers--;
    for( INT i=0; i<VisiblePeers; i++ )
    {
        const FXboxSystemLinkPeer& Peer = GXboxSystemLink.Peers(i);
        TCHAR AddrText[32];
        XboxSystemLinkFormatAddress( Peer.Address, AddrText, ARRAY_COUNT(AddrText) );
        appSprintf( Status, TEXT("%s    %08X    %s:%i    PINGS %i"),
            XboxSystemLinkRoleText(Peer.Role),
            Peer.Id,
            AddrText,
            Peer.Port,
            Peer.Packets );
        XboxMenuDrawRect( Canvas, 48, Y-4, XboxMenuContentRight(Canvas)-10.0f, Y+17, 12, 82, 166, Peer.Role == XSLR_Host ? 0.40f : 0.22f );
        XboxMenuTextFit( Canvas, SmallFont, 62, Y, XboxMenuContentRight(Canvas)-86.0f, 180, 215, 245, Status );
        Y += RowStep;
    }

    if( PeerCount > VisiblePeers )
    {
        TCHAR MoreText[48];
        appSprintf( MoreText, TEXT("%i MORE XBOXES"), PeerCount - VisiblePeers );
        XboxMenuText( Canvas, SmallFont, 62, Y, 135, 170, 205, MoreText );
    }

    if( GXboxSystemLink.Peers.Num() == 0 )
        XboxMenuText( Canvas, SmallFont, 62, Y, 135, 170, 205, TEXT("WAITING FOR OTHER XBOXES") );

    if( GXboxSystemLink.Role == XSLR_Host )
        XboxMenuTextFit( Canvas, MenuFont, 58, DiscoveryHintY, XboxMenuContentRight(Canvas)-76.0f, 135, 255, 120, DiscoveryHint );
    else if( GXboxSystemLink.Role == XSLR_Client )
        XboxMenuTextFit( Canvas, MenuFont, 58, DiscoveryHintY, XboxMenuContentRight(Canvas)-76.0f, 135, 255, 120, DiscoveryHint );
    else
        XboxMenuTextFit( Canvas, MenuFont, 58, DiscoveryHintY, XboxMenuContentRight(Canvas)-76.0f, 135, 170, 205, DiscoveryHint );
}

static void XboxMenuDrawComingSoon( UCanvas* Canvas )
{
    XboxMenuDrawChromeCommands( Canvas, GXboxMenu.ComingSoonTitle, "button_b.xui", TEXT("BACK") );
    XboxMenuCenteredText( Canvas, Canvas->MedFont, 150, 255, 255, 255, GXboxMenu.ComingSoonTitle );
    XboxMenuCenteredText( Canvas, Canvas->MedFont, 230, 160, 205, 240, TEXT("COMING SOON") );
}

void XboxMenuPostRender( UViewport* Viewport, UCanvas* Canvas )
{
    guard(XboxMenuPostRender);
    UXboxViewport* XboxViewport = Cast<UXboxViewport>(Viewport);
    // Frontend rendering continues while ordinary gameplay input is paused.
    XboxHaloPortraitProofTick( XboxViewport );
    INT WheelViewportIndex = XboxViewport ? Clamp<INT>( XboxViewportIndex(XboxViewport), 0, 3 ) : 0;
    if( !Viewport || !Canvas )
        return;
    if( XboxViewport )
    {
        XboxMenuApplyPendingMatchRules( XboxViewport );
        XboxSoakSmokeTick( XboxViewport );
        XboxIssueMapSmokeTick( XboxViewport );
        XboxSafeAreaProofSmokeTick( XboxViewport );
        XboxInstantRulesProofSmokeTick( XboxViewport );
        XboxSystemLinkSmokeTick( XboxViewport );
    }
    if( !GXboxMenu.Active )
    {
        if( XboxViewport && GXboxWeaponWheelActive[WheelViewportIndex] )
            XboxWeaponWheelDraw( XboxViewport, Canvas );
        return;
    }

    if( GXboxSplitActive && GXboxMenuGameplayContinues && WheelViewportIndex != GXboxMenuOwnerViewport )
    {
        if( XboxViewport && GXboxWeaponWheelActive[WheelViewportIndex] )
            XboxWeaponWheelDraw( XboxViewport, Canvas );
        return;
    }

    GXboxMenu.Pulse += 0.04f;
    XboxSystemLinkTick( XboxViewport );
    if( XboxViewport )
        XboxSystemLinkSmokeTick( XboxViewport );

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
    else if( GXboxMenu.Screen == XMS_Tournament )
        XboxMenuDrawTournament( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_TournamentPostMatch )
        XboxMenuDrawTournamentPostMatch( Canvas );
    else if( GXboxMenu.Screen == XMS_SplitReady )
        XboxMenuDrawSplitReady( Canvas );
    else if( GXboxMenu.Screen == XMS_SplitMapSelect )
        XboxMenuDrawSplitMapSelect( Canvas );
    else if( GXboxMenu.Screen == XMS_SystemLinkMapSelect )
        XboxMenuDrawSplitMapSelect( Canvas );
    else if( GXboxMenu.Screen == XMS_ProfileSelect )
        XboxMenuDrawProfileSelect( Canvas );
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
        XboxMenuDrawPlayerSetup( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_ProfileName )
        XboxMenuDrawProfileName( Canvas );
    else if( GXboxMenu.Screen == XMS_Controls )
        XboxMenuDrawControls( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_Settings )
        XboxMenuDrawSettings( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_SettingsAudio )
        XboxMenuDrawAudioSettings( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_SettingsVideo )
        XboxMenuDrawVideoSettings( XboxViewport, Canvas );
    else if( GXboxMenu.Screen == XMS_SystemLink )
        XboxMenuDrawSystemLink( Canvas );
    else
        XboxMenuDrawComingSoon( Canvas );

#if XBOX_ENABLE_AUDIO_TONE_SMOKE
    if( GetFileAttributesA( "D:\\XboxAudioToneSmoke.ini" ) != 0xFFFFFFFF )
    {
        TCHAR AudioState[32];
        appSprintf( AudioState, TEXT("AUD%i"), (INT)GXboxAudioToneSmokeState );
        XboxMenuText( Canvas, Canvas->SmallFont, 540, 18, 140, 210, 255, AudioState );
        appSprintf( AudioState, TEXT("MUS%i"), (INT)GXboxAudioMusicStreamState );
        XboxMenuText( Canvas, Canvas->SmallFont, 540, 38, 140, 210, 255, AudioState );
        appSprintf( AudioState, TEXT("PKT%i"), (INT)GXboxAudioMusicPacketState );
        XboxMenuText( Canvas, Canvas->SmallFont, 540, 58, 140, 210, 255, AudioState );
        appSprintf( AudioState, TEXT("LOD%i"), (INT)GXboxAudioMusicLoadState );
        XboxMenuText( Canvas, Canvas->SmallFont, 540, 78, 140, 210, 255, AudioState );
    }
#endif

    unguard;
}

static UBOOL XboxAutoFireSmokeEnabled()
{
    static INT Enabled = -1;
    if( Enabled < 0 )
        Enabled = GetFileAttributesA( "D:\\XboxAutoFireSmoke.ini" ) != 0xFFFFFFFF;
    return Enabled;
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

static void XboxFlickerTraversalProofApply( UXboxViewport* Viewport, XINPUT_GAMEPAD& Pad )
{
    if( !XboxFlickerTraversalProofEnabled() || !Viewport || !Viewport->Actor )
        return;

    APlayerPawn* Player = Viewport->Actor;
    ULevel* Level = Player->GetLevel();
    if( !Level || XboxIsFrontendLevel(Level) || GXboxMenu.Active )
        return;

    // Follow a live bot instead of synthesizing a movement pattern. The bot's
    // navigation and combat decisions exercise real map routes, corners,
    // lifts, stairs, elevation changes, and arbitrary view rotations.
    APawn* Bot = NULL;
    for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; Pawn = Pawn->nextPawn )
    {
        if( Pawn != Player
        &&  Pawn->bIsPlayer
        &&  Pawn->PlayerReplicationInfo
        &&  Pawn->PlayerReplicationInfo->bIsABot
        &&  !Pawn->PlayerReplicationInfo->bIsSpectator
        &&  Pawn->Health > 0
        &&  !Pawn->bHidden )
        {
            Bot = Pawn;
            break;
        }
    }
    if( !Bot )
        return;

    appMemzero( &Pad, sizeof(Pad) );
    if( Player->ViewTarget != Bot || Player->bBehindView )
    {
        Player->ViewTarget = Bot;
        Player->bBehindView = 0;
        GXboxLog.Write( "XFLICKER SPECTATE target=%s map=%s",
            Bot->PlayerReplicationInfo
                ? TCHAR_TO_ANSI(*Bot->PlayerReplicationInfo->PlayerName)
                : TCHAR_TO_ANSI(Bot->GetName()),
            Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "" );
    }

    DWORD Now = GetTickCount();

    // The reported visual corruption was also associated with display-control
    // changes. Cycle through neutral, moderate, and edge settings during the
    // moving bot view so every run exercises both the calibration pass and the
    // transitions into and out of it.
    UXboxClient* Client = Cast<UXboxClient>( Viewport->GetOuter() );
    static DWORD CalibrationStart = 0;
    static INT LastCalibrationPhase = -1;
    if( Client )
    {
        if( !CalibrationStart )
            CalibrationStart = Now;
        INT CalibrationPhase = ((Now - CalibrationStart) / 6000) % 5;
        if( CalibrationPhase != LastCalibrationPhase )
        {
            static const FLOAT BrightnessValues[5] = { 0.60f, 0.35f, 0.80f, 0.50f, 0.20f };
            static const FLOAT ContrastValues[5]   = { 1.10f, 0.75f, 1.40f, 1.00f, 1.30f };
            static const FLOAT GammaValues[5]      = { 1.25f, 0.65f, 1.80f, 1.00f, 1.50f };
            Client->Brightness = BrightnessValues[CalibrationPhase];
            Client->DisplayContrast = ContrastValues[CalibrationPhase];
            Client->DisplayGamma = GammaValues[CalibrationPhase];
            XboxRenderSetDisplayCalibration(
                Client->Brightness,
                Client->DisplayContrast,
                Client->DisplayGamma );
            LastCalibrationPhase = CalibrationPhase;
            GXboxLog.Write( "XFLICKER CALIBRATION phase=%d brightness=%.2f contrast=%.2f gamma=%.2f",
                CalibrationPhase, Client->Brightness, Client->DisplayContrast, Client->DisplayGamma );
        }
    }

    static UBOOL Initialized = 0;
    static DWORD LastLog = 0;
    static FVector StartLocation(0,0,0);
    static FVector LastLocation(0,0,0);
    if( !Initialized )
    {
        Initialized = 1;
        LastLog = Now;
        StartLocation = LastLocation = Bot->Location;
        GXboxLog.Write( "XFLICKER TRAVERSAL START map=%s target=%s loc=%.1f,%.1f,%.1f",
            Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
            Bot->PlayerReplicationInfo
                ? TCHAR_TO_ANSI(*Bot->PlayerReplicationInfo->PlayerName)
                : TCHAR_TO_ANSI(Bot->GetName()),
            Bot->Location.X, Bot->Location.Y, Bot->Location.Z );
    }
    else if( Now - LastLog >= 1000 )
    {
        FLOAT Segment = (Bot->Location - LastLocation).Size();
        FLOAT Total = (Bot->Location - StartLocation).Size();
        GXboxLog.Write( "XFLICKER MOVE ms=%lu map=%s target=%s loc=%.1f,%.1f,%.1f segment=%.1f total=%.1f phase=0",
            Now,
            Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "",
            Bot->PlayerReplicationInfo
                ? TCHAR_TO_ANSI(*Bot->PlayerReplicationInfo->PlayerName)
                : TCHAR_TO_ANSI(Bot->GetName()),
            Bot->Location.X, Bot->Location.Y, Bot->Location.Z,
            Segment, Total );
        LastLocation = Bot->Location;
        LastLog = Now;
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

    // Viewport index maps directly to physical controller port index. Input
    // init can poll once before OpenWindow; keep that handle instead of
    // reopening the same XInput device and losing controls on hardware.
    INT WantedPort = XboxViewportControllerPort( this );
    if( !ControllerHandle || ControllerPort != WantedPort )
    {
        if( ControllerHandle )
            XInputClose( ControllerHandle );
        ControllerPort      = WantedPort;
        ControllerHandle    = NULL;
        ControllerConnected = 0;
        appMemzero( &ControllerState,     sizeof(ControllerState)     );
        appMemzero( &PrevControllerState, sizeof(PrevControllerState) );
    }

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

    if( XboxSplitControlsProofEnabled() || XboxSplitControlsOnlineProofEnabled() )
    {
        PrevControllerState = ControllerState;
        XINPUT_GAMEPAD ProofPad;
        if( XboxSplitControlsProofApply(this, ProofPad) )
        {
            ControllerConnected = 1;
            ControllerState.dwPacketNumber++;
            ControllerState.Gamepad = ProofPad;
            ProcessControllerInput( ControllerState.Gamepad );
            return;
        }
    }

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
        if( XboxTournamentProgressWinSmokeEnabled()
        ||  XboxFullMenuProofRequested() != XFMP_None )
        {
            static UBOOL bTournamentProofInputLogged = 0;
            appMemzero( &ControllerState.Gamepad, sizeof(ControllerState.Gamepad) );
            appMemzero( &PrevControllerState.Gamepad, sizeof(PrevControllerState.Gamepad) );
            if( !bTournamentProofInputLogged )
            {
                bTournamentProofInputLogged = 1;
                GXboxLog.Write( "XPROOF physical controller input suppressed tournamentWin=%d fullMenu=%d",
                    XboxTournamentProgressWinSmokeEnabled() ? 1 : 0,
                    XboxFullMenuProofRequested() );
            }
        }
        XboxWeaponCycleProofSmokeApply( this, ControllerState.Gamepad );
        XboxFlickerTraversalProofApply( this, ControllerState.Gamepad );
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
    if( GXboxSplitActive && !bXboxSplitDummy && !XboxViewportEnsureInputInitialized( this, "ProcessControllerInput" ) )
        return;

    XboxMenuTickPendingFrontendOpen( this );
    XboxTournamentTransitionTick( this );
    if( XboxTournamentCompletedMatchTick( this ) )
        return;
    INT WheelViewportIndex = Clamp<INT>( XboxViewportIndex(this), 0, 3 );
    INT LocalPort = WheelViewportIndex;
    FXboxRuntimeProfileControls* ProfileControls = XboxSplitControlsForPort( LocalPort );
    INT ProfileStickLayout = ProfileControls ? ProfileControls->StickLayout : Client->StickLayout;
    FLOAT ProfileDeadZone = ProfileControls ? ProfileControls->DeadZone : Client->DeadZone;
    FLOAT ProfileMoveSensitivity = ProfileControls ? ProfileControls->MoveSensitivity : Client->ScaleXYZ;
    FLOAT ProfileLookSensitivity = ProfileControls ? ProfileControls->LookSensitivity : Client->ScaleRUV;
    UBOOL bProfileInvertY = ProfileControls ? ProfileControls->InvertY : Client->InvertVertical;
    UBOOL bMenuOwner = WheelViewportIndex == GXboxMenuOwnerViewport;
    UBOOL bMenuCapturesGameplay = GXboxMenu.Active
        && (!GXboxSplitActive || !GXboxMenuGameplayContinues || bMenuOwner);
    const BYTE AnalogThreshold = XINPUT_GAMEPAD_MAX_CROSSTALK; // 30
    UBOOL WheelButtonNow = 0;
    UBOOL WheelButtonPrev = 0;
    for( INT WheelButton=0; WheelButton<XCB_Count; WheelButton++ )
    {
        INT WheelAction = XboxControlButtonActionForPort( Client, LocalPort, WheelButton );
        if( GXboxControlActions[WheelAction].bWheelHold )
        {
            WheelButtonNow  = WheelButtonNow  || XboxControlButtonDown( Pad, WheelButton, AnalogThreshold );
            WheelButtonPrev = WheelButtonPrev || XboxControlButtonDown( PrevControllerState.Gamepad, WheelButton, AnalogThreshold );
        }
    }
    UBOOL bWheelInputActive = Player && !bMenuCapturesGameplay && (GXboxWeaponWheelActive[WheelViewportIndex] || WheelButtonNow || WheelButtonPrev);

    if( GXboxSplitActive && GXboxMenu.Active )
    {
        if( bMenuOwner )
        {
            if( XboxMenuHandleInput( this, Pad, PrevControllerState.Gamepad ) )
                return;
        }
        else if( !GXboxMenuGameplayContinues )
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

    if( Player && !bMenuCapturesGameplay )
        XboxTournamentLogReadyState( this, "poll", 0 );

    if( Player && !bMenuCapturesGameplay )
    {
        const DOUBLE NowSeconds = appSeconds();
        const DOUBLE HoldSeconds = 0.24;
        UBOOL AnyWheelNow = 0;

        for( INT Button=0; Button<XCB_Count; Button++ )
        {
            INT Action = XboxControlButtonActionForPort( Client, LocalPort, Button );
            const FXboxControlActionInfo& ActionInfo = GXboxControlActions[Action];
            if( !ActionInfo.bWheelHold )
                continue;

            UBOOL Now = XboxControlButtonDown( Pad, Button, AnalogThreshold );
            UBOOL Prev = XboxControlButtonDown( PrevControllerState.Gamepad, Button, AnalogThreshold );
            if( Now )
                AnyWheelNow = 1;
            if( Now && !Prev )
                GXboxWeaponWheelPressTime[WheelViewportIndex][Button] = NowSeconds;
            if( Now && (NowSeconds - GXboxWeaponWheelPressTime[WheelViewportIndex][Button]) >= HoldSeconds )
                GXboxWeaponWheelActive[WheelViewportIndex] = 1;
        }

        if( GXboxWeaponWheelActive[WheelViewportIndex] )
        {
            GXboxWeaponWheelFocus[WheelViewportIndex] = XboxWeaponWheelSlotFromStick( ProfileStickLayout, Pad, GXboxWeaponWheelFocus[WheelViewportIndex] );
        }

        if( GXboxWeaponWheelActive[WheelViewportIndex] && !AnyWheelNow )
        {
            INT ChosenSlot = Clamp<INT>( GXboxWeaponWheelFocus[WheelViewportIndex], 0, ARRAY_COUNT(GXboxWeaponWheelSlots)-1 );
            GXboxWeaponWheelActive[WheelViewportIndex] = 0;
            XboxWeaponWheelSelect( this, Player, ChosenSlot );
        }
        else if( !GXboxWeaponWheelActive[WheelViewportIndex] )
        {
            for( INT Button=0; Button<XCB_Count; Button++ )
            {
                INT Action = XboxControlButtonActionForPort( Client, LocalPort, Button );
                const FXboxControlActionInfo& ActionInfo = GXboxControlActions[Action];
                if( !ActionInfo.bWheelHold || ActionInfo.CycleDir == 0 )
                    continue;

                UBOOL Now = XboxControlButtonDown( Pad, Button, AnalogThreshold );
                UBOOL Prev = XboxControlButtonDown( PrevControllerState.Gamepad, Button, AnalogThreshold );
                if( Prev && !Now && (NowSeconds - GXboxWeaponWheelPressTime[WheelViewportIndex][Button]) < HoldSeconds )
                    XboxWeaponCycle( this, Player, ActionInfo.CycleDir > 0 );
            }
        }
    }

    if( GXboxSplitActive && Player )
    {
        Player->bShowMenu = 0;
        Player->bSpecialMenu = 0;
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

    // ---- Analog sticks ----
    // WinDrv feeds Unreal a normalized joystick delta multiplied by the
    // configured joystick scale (Default.ini: ScaleXYZ=1000, ScaleRUV=2000).
    // Feeding raw -1..1 Xbox values makes UInput's 0.01 axis multiplier crawl.
    FLOAT DeadZone = ProfileDeadZone;
    if( DeadZone < 0.0f )
        DeadZone = 0.0f;
    if( DeadZone > 0.95f )
        DeadZone = 0.95f;
    FLOAT Sensitivity = Client->ControllerSensitivity;

    FLOAT LeftX = XboxStickAxis( Pad.sThumbLX, DeadZone );
    FLOAT LeftY = XboxStickAxis( Pad.sThumbLY, DeadZone );
    FLOAT RightX = XboxStickAxis( Pad.sThumbRX, DeadZone );
    FLOAT RightY = XboxStickAxis( Pad.sThumbRY, DeadZone );
    FLOAT MoveX = 0.0f;
    FLOAT MoveY = 0.0f;
    FLOAT LookX = 0.0f;
    FLOAT LookY = 0.0f;
    XboxStickLayoutAxes( ProfileStickLayout, LeftX, LeftY, RightX, RightY, MoveX, MoveY, LookX, LookY );

    FLOAT LX = MoveX * ProfileMoveSensitivity * Sensitivity;
    FLOAT LY = MoveY * ProfileMoveSensitivity * Sensitivity;
    FLOAT RX = LookX * ProfileLookSensitivity * Sensitivity;
    FLOAT RY = LookY * ProfileLookSensitivity * Sensitivity;
    if( GXboxWeaponWheelActive[WheelViewportIndex] )
    {
        RX = 0.0f;
        RY = 0.0f;
    }
    if( bProfileInvertY )
        RY = -RY;

    if( Player )
    {
        FLOAT OldForward = Player->aForward;
        FLOAT OldBaseY   = Player->aBaseY;
        FLOAT OldStrafe  = Player->aStrafe;
        FLOAT OldTurn    = Player->aTurn;
        FLOAT OldLookUp  = Player->aLookUp;

        UBOOL FireNow = 0;
        UBOOL FirePrev = 0;
        UBOOL AltFireNow = 0;
        UBOOL AltFirePrev = 0;
        UBOOL DuckNow = 0;
        UBOOL JumpNow = 0;

        for( INT Button=0; Button<XCB_Count; Button++ )
        {
            INT Action = XboxControlButtonActionForPort( Client, LocalPort, Button );
            const FXboxControlActionInfo& ActionInfo = GXboxControlActions[Action];
            UBOOL Now = XboxControlButtonDown( Pad, Button, AnalogThreshold );
            UBOOL Prev = XboxControlButtonDown( PrevControllerState.Gamepad, Button, AnalogThreshold );

            if( ActionInfo.bReadyFire )
            {
                FireNow = FireNow || Now;
                FirePrev = FirePrev || Prev;
            }
            if( ActionInfo.bReadyAltFire )
            {
                AltFireNow = AltFireNow || Now;
                AltFirePrev = AltFirePrev || Prev;
            }
            if( Action == XCA_Duck )
                DuckNow = DuckNow || Now;
            if( Action == XCA_Jump )
                JumpNow = JumpNow || Now;

            if( ActionInfo.Key != IK_None && !(GXboxWeaponWheelActive[WheelViewportIndex] && ActionInfo.Key == IK_Tab) )
                XboxSendGameplayButton( this, ActionInfo.Key, Now, Prev );
            if( ActionInfo.bDodge && Now && !Prev )
                XboxTriggerDodge( ProfileStickLayout, Player, Pad );
        }

        UBOOL FireEdge = FireNow && !FirePrev;
        UBOOL AltFireEdge = AltFireNow && !AltFirePrev;

        XboxTournamentHandleReadyInput( this, FireEdge, AltFireEdge );
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

    XboxSplitControlsProofObserve( this, Pad );

    unguard;
}

UBOOL UXboxViewport::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear,
                            DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
    guard(UXboxViewport::Lock);
    INT LockX = ViewX;
    INT LockY = ViewY;
    INT LockW = ViewWidth  > 0 ? ViewWidth  : SizeX;
    INT LockH = ViewHeight > 0 ? ViewHeight : SizeY;
    if( !GXboxSplitActive && !bXboxSplitDummy )
    {
        UXboxClient* Client = Cast<UXboxClient>( GetOuter() );
        if( Client )
            XboxViewportApplySafeArea( Client, LockX, LockY, LockW, LockH );
    }
    XboxRenderSetPendingViewRegion( LockX, LockY, Max<INT>( LockW, 1 ), Max<INT>( LockH, 1 ), !GXboxSplitActive && !bXboxSplitDummy );
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
    XboxMenuApplyPendingMatchRules( this );
    XboxTournamentSmokeTick( this );
    XboxMenuProofSmokeTick( this );
    XboxMenuSmokeTick( this );
    XboxSoakSmokeTick( this );
    XboxIssueMapSmokeTick( this );
    XboxSafeAreaProofSmokeTick( this );
    XboxInstantRulesProofSmokeTick( this );
    XboxSystemLinkSmokeTick( this );
    unguard;
}

void* UXboxViewport::GetWindow() { return NULL; }

UBOOL UXboxViewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
    guard(UXboxViewport::Exec);
    return Super::Exec( Cmd, Ar );
    unguard;
}
