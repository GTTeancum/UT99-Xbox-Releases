// XboxViewport.cpp

extern "C" UBOOL XboxRenderDrawMenuTexture( FSceneNode* Frame, const char* Name, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha );
extern "C" void  XboxRenderDrawMenuRect( FSceneNode* Frame, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, BYTE A );
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

static HANDLE XboxOpenFirstController( INT& OutPort, DWORD DeviceMask )
{
    for( INT Port=0; Port<(INT)XGetPortCount(); Port++ )
    {
        if( DeviceMask & (1 << Port) )
        {
            HANDLE Handle = XboxOpenControllerOnPort( Port, DeviceMask );
            if( Handle )
            {
                OutPort = Port;
                return Handle;
            }
        }
    }
    OutPort = -1;
    return NULL;
}

enum EXboxMenuScreen
{
    XMS_Pause,
    XMS_Main,
    XMS_InstantAction,
    XMS_Mutators,
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
    TEXT("SOUTHPAW STICKS"),
    TEXT("FACE FIRE")
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
static FLOAT GXboxPlayerPreviewYaw = 32768.0f;

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

    GXboxDiscoveredListsLoaded = 1;
    GXboxDiscoveredGameTypes.Empty();
    GXboxDiscoveredMutators.Empty();

    UClass* TournamentGameInfoClass = FindObject<UClass>( ANY_PACKAGE, TEXT("TournamentGameInfo") );
    UClass* MutatorClass = FindObject<UClass>( ANY_PACKAGE, TEXT("Mutator") );

    if( TournamentGameInfoClass )
    {
        TArray<FRegistryObjectInfo> GameInfos;
        UObject::GetRegistryObjects( GameInfos, UClass::StaticClass(), TournamentGameInfoClass, 0 );
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

    GXboxPlayerListsLoaded = 1;
    GXboxPlayerClasses.Empty();

    UClass* TournamentPlayerClass = FindObject<UClass>( ANY_PACKAGE, TEXT("TournamentPlayer") );
    if( TournamentPlayerClass )
    {
        TArray<FRegistryObjectInfo> Players;
        UObject::GetRegistryObjects( Players, UClass::StaticClass(), TournamentPlayerClass, 0 );
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
        INT PrefixLen = Player.MeshName.Len();

        for( INT i=0; i<Textures.Num(); i++ )
        {
            if( appStrnicmp( *Textures(i).Object, *Player.MeshName, PrefixLen ) != 0 )
                continue;
            if( Textures(i).Description.Len() == 0 )
                continue;

            FString Item;
            FString Prefix;
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
        INT PrefixLen = Player.MeshName.Len();

        for( INT i=0; i<Textures.Num(); i++ )
        {
            if( appStrnicmp( *Textures(i).Object, *Player.MeshName, PrefixLen ) != 0 )
                continue;
            if( Textures(i).Description.Len() == 0 )
                continue;

            FString Item;
            FString Prefix;
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
    UClass* VoiceMetaClass = FindObject<UClass>( ANY_PACKAGE, *Player.VoiceMetaClass );
    if( !VoiceMetaClass )
        VoiceMetaClass = UObject::StaticLoadClass( UObject::StaticClass(), NULL, *Player.VoiceMetaClass, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );

    if( VoiceMetaClass )
    {
        TArray<FRegistryObjectInfo> Voices;
        UObject::GetRegistryObjects( Voices, UClass::StaticClass(), VoiceMetaClass, 0 );
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

static void XboxMenuApplyPreviewSkin( AActor* Actor )
{
    if( !Actor )
        return;

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

    if( !Player.bMultiSkinned || !PlayerClass )
    {
        Actor->Skin = XboxMenuLoadTexture( SkinName );
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
    UMesh* Mesh = MeshName.Len()
        ? Cast<UMesh>( UObject::StaticLoadObject( UMesh::StaticClass(), NULL, *MeshName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) )
        : NULL;
    Actor->Mesh = Mesh;
    Actor->DrawScale = 0.10f;
    Actor->AmbientGlow = 255;
    Actor->bMeshEnviroMap = 0;
    XboxMenuApplyPreviewSkin( Actor );

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

    GXboxPlayerPreviewYaw += 96.0f;
    if( GXboxPlayerPreviewYaw >= 65536.0f )
        GXboxPlayerPreviewYaw -= 65536.0f;

    FLOAT OldFov = Viewport->Actor->FovAngle;
    Viewport->Actor->FovAngle = 30.0f;
    FLOAT FovRadians = Viewport->Actor->FovAngle * PI / 180.0f;
    Actor->Location = FVector( 4.0f / appTan(FovRadians * 0.5f), 0.0f, -1.5f );
    Actor->Rotation = FRotator( 0, (INT)GXboxPlayerPreviewYaw, 0 );

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
    GXboxLog.Write( "XMENU Begin Match travel: %s", TCHAR_TO_ANSI(URL) );
    Client->Engine->SetClientTravel( Viewport, URL, 0, TRAVEL_Absolute );
}

static void XboxMenuReturnToFrontend( UXboxViewport* Viewport )
{
    UXboxClient* Client = Viewport ? (UXboxClient*)Viewport->GetOuter() : NULL;
    if( !Client || !Client->Engine )
        return;

    XboxMenuDestroyPlayerPreview();
    XboxMenuReleaseMatchPause( Viewport );
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
                GXboxMenu.Screen = XMS_PlayerSetup;
                GXboxMenu.PlayerFocus = 0;
                XboxMenuLoadPlayerState();
                GXboxLog.Write( "XMENU screen: Player Setup" );
                break;
            case 2:
                XboxMenuComingSoon( TEXT("SYSTEM LINK") );
                break;
            case 3:
                XboxMenuComingSoon( TEXT("SPLITSCREEN") );
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
                Client->ButtonLayout = XboxMenuWrapInt( Client->ButtonLayout, Delta, ARRAY_COUNT(GXboxButtonLayouts) );
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

    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_UP ) )
        XboxMenuMove( -1 );
    if( XboxButtonPressed( CurDigital, PrevDigital, XINPUT_GAMEPAD_DPAD_DOWN ) )
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
        TEXT("PLAYER SETUP"),
        TEXT("SYSTEM LINK"),
        TEXT("SPLITSCREEN"),
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
    FLOAT X1 = 152.0f;
    FLOAT Y1 = 92.0f;
    FLOAT X2 = 488.0f;
    FLOAT Y2 = 310.0f;
    const INT MutatorCount = XboxMenuMutatorCount();
    const INT VisibleRows = 4;
    INT Focus = Clamp<INT>( GXboxMenu.InstantMutatorChoice, 0, MutatorCount-1 );
    INT First = Focus - VisibleRows / 2;
    First = Clamp<INT>( First, 0, Max<INT>(0, MutatorCount - VisibleRows) );
    INT Last = Min<INT>( MutatorCount, First + VisibleRows );

    XboxMenuDrawRect( Canvas, X1-8, Y1-8, X2+8, Y2+8, 0, 0, 0, 0.76f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y2, 9, 42, 89, 0.88f );
    XboxMenuDrawRect( Canvas, X1, Y1, X2, Y1+4, 28, 108, 205, 0.9f );
    XboxMenuText( Canvas, MenuFont, X1+18, Y1+18, 255, 255, 255, TEXT("MUTATORS") );

    if( First > 0 )
        XboxMenuText( Canvas, MenuFont, X2-72, Y1+18, 135, 170, 205, TEXT("MORE ^") );

    for( INT i=First; i<Last; i++ )
    {
        FLOAT Y = Y1 + 68.0f + (i - First) * 38.0f;
        UBOOL bOn = (GXboxMenu.InstantMutatorMask[i >> 5] & (1 << (i & 31))) != 0;
        UBOOL bFocus = (i == Focus);

        if( bFocus )
            XboxMenuDrawRect( Canvas, X1+18, Y-6, X2-18, Y+24, 12, 82, 166, 0.55f );

        XboxMenuText( Canvas, MenuFont, X1+36, Y, bOn ? 255 : 140, bOn ? 255 : 178, bOn ? 255 : 212, bOn ? TEXT("[X]") : TEXT("[ ]") );
        XboxMenuText( Canvas, MenuFont, X1+92, Y, bFocus ? 255 : 180, bFocus ? 255 : 205, bFocus ? 255 : 230, *XboxMenuMutator(i).Label );
    }

    if( Last < MutatorCount )
        XboxMenuText( Canvas, MenuFont, X2-72, Y2-56, 135, 170, 205, TEXT("MORE v") );

    XboxMenuDrawButtonPrompt( Canvas, X1+18, Y2-34, "button_a.xui", TEXT("TOGGLE") );
    XboxMenuDrawButtonPrompt( Canvas, X1+160, Y2-34, "button_b.xui", TEXT("BACK") );
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

    XboxMenuDrawRect( Canvas, 378, 112, 590, 288, 25, 34, 48, 0.72f );
    XboxMenuDrawRect( Canvas, 388, 122, 580, 278, 0, 0, 0, 0.52f );
    XboxMenuDrawPlayerPreviewActor( Viewport, Canvas, 388.0f, 122.0f, 192.0f, 156.0f );
    if( !GXboxPlayerPreviewActor || !GXboxPlayerPreviewActor->Mesh )
        XboxMenuText( Canvas, MenuFont, 430, 198, 135, 170, 205, TEXT("NO PREVIEW") );

    for( INT i=0; i<ARRAY_COUNT(Labels); i++ )
    {
        FLOAT Y = 150.0f + i * 34.0f;
        if( i == GXboxMenu.PlayerFocus )
        {
            XboxMenuDrawRect( Canvas, 42, Y-6, 360, Y+20, 12, 82, 166, 0.55f );
            XboxMenuText( Canvas, MenuFont, 192, Y, 180, 215, 245, TEXT("<") );
            XboxMenuText( Canvas, MenuFont, 344, Y, 180, 215, 245, TEXT(">") );
            XboxMenuText( Canvas, MenuFont, 58, Y, 255, 255, 255, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 214, Y, 255, 255, 255, Values[i] );
        }
        else
        {
            XboxMenuText( Canvas, MenuFont, 58, Y, 140, 178, 212, Labels[i] );
            XboxMenuText( Canvas, MenuFont, 214, Y, 180, 205, 230, Values[i] );
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
    INT Layout = Client ? XboxMenuWrapInt( Client->ButtonLayout, 0, ARRAY_COUNT(GXboxButtonLayouts) ) : 0;
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

static void XboxMenuDrawComingSoon( UCanvas* Canvas )
{
    XboxMenuDrawChrome( Canvas, GXboxMenu.ComingSoonTitle, 1 );
    XboxMenuCenteredText( Canvas, Canvas->MedFont, 150, 255, 255, 255, GXboxMenu.ComingSoonTitle );
    XboxMenuCenteredText( Canvas, Canvas->MedFont, 230, 160, 205, 240, TEXT("COMING SOON") );
}

void XboxMenuPostRender( UViewport* Viewport, UCanvas* Canvas )
{
    guard(XboxMenuPostRender);
    if( !GXboxMenu.Active || !Viewport || !Canvas )
        return;

    GXboxMenu.Pulse += 0.04f;

    if( GXboxMenu.Screen == XMS_Pause )
        XboxMenuDrawPause( Canvas );
    else if( GXboxMenu.Screen == XMS_Main )
        XboxMenuDrawMain( Canvas );
    else if( GXboxMenu.Screen == XMS_InstantAction )
        XboxMenuDrawInstantAction( Canvas );
    else if( GXboxMenu.Screen == XMS_Mutators )
        XboxMenuDrawMutators( Canvas );
    else if( GXboxMenu.Screen == XMS_PlayerSetup )
        XboxMenuDrawPlayerSetup( Cast<UXboxViewport>(Viewport), Canvas );
    else if( GXboxMenu.Screen == XMS_Settings )
        XboxMenuDrawSettings( Cast<UXboxViewport>(Viewport), Canvas );
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

    // Initialize controller — auto-bind port 0.
    ControllerPort      = -1;
    ControllerHandle    = NULL;
    ControllerConnected = 0;
    appMemzero( &ControllerState,     sizeof(ControllerState)     );
    appMemzero( &PrevControllerState, sizeof(PrevControllerState) );

    DWORD DeviceMask = XGetDevices( XDEVICE_TYPE_GAMEPAD );
    if( !ControllerHandle )
    {
        ControllerHandle = XboxOpenFirstController( ControllerPort, DeviceMask );
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
        ControllerPort = -1;
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
            ControllerHandle = XboxOpenFirstController( ControllerPort, DeviceMask );
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
        ControllerPort = -1;
    }

    unguard;
}

void UXboxViewport::ProcessControllerInput( const XINPUT_GAMEPAD& Pad )
{
    guard(UXboxViewport::ProcessControllerInput);

    UXboxClient* Client = (UXboxClient*)GetOuter();
    if( !Client || !Client->Engine )
        return;

    if( XboxMenuHandleInput( this, Pad, PrevControllerState.Gamepad ) )
        return;

    APlayerPawn* Player = Actor;

    // ---- Digital buttons (bitmask in wButtons) ----
    // Route utility buttons through Unreal's normal binding layer. Core
    // gameplay controls are written directly below so a stale User.ini cannot
    // leave the player without movement/fire/look on hardware.
    struct FDigitalMap { WORD Mask; EInputKey Key; };
    static const FDigitalMap DigitalMap[] =
    {
        { XINPUT_GAMEPAD_DPAD_UP,     IK_JoyPovUp },
        { XINPUT_GAMEPAD_DPAD_DOWN,   IK_JoyPovDown },
        { XINPUT_GAMEPAD_DPAD_LEFT,   IK_JoyPovLeft },
        { XINPUT_GAMEPAD_DPAD_RIGHT,  IK_JoyPovRight },
        { XINPUT_GAMEPAD_BACK,        IK_Tab },        // Scoreboard
        { XINPUT_GAMEPAD_RIGHT_THUMB, IK_Joy6 },       // Reserved / zoom bind
    };

    WORD CurDigital  = Pad.wButtons;
    WORD PrevDigital = PrevControllerState.Gamepad.wButtons;
    WORD DigChanged  = CurDigital ^ PrevDigital;
    static INT InputLogCount = 0;

    for( INT i = 0; i < ARRAY_COUNT(DigitalMap); i++ )
    {
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
    // X, Y, Black, White remain binding-driven utility actions.
    const BYTE AnalogThreshold = XINPUT_GAMEPAD_MAX_CROSSTALK; // 30
    struct FAnalogBtnMap { INT Index; EInputKey Key; };
    static const FAnalogBtnMap AnalogMap[] =
    {
        { XINPUT_GAMEPAD_X,              IK_Enter },      // Use / accept
        { XINPUT_GAMEPAD_Y,              IK_Slash },      // Next weapon
        { XINPUT_GAMEPAD_BLACK,          IK_LeftBracket },// Previous item/weapon
        { XINPUT_GAMEPAD_WHITE,          IK_RightBracket },// Next item/weapon
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
    if( Client->ButtonLayout == 1 )
    {
        FLOAT MoveX = XboxStickAxis( Pad.sThumbRX, DeadZone ) * Client->ScaleXYZ * Sensitivity;
        FLOAT MoveY = XboxStickAxis( Pad.sThumbRY, DeadZone ) * Client->ScaleXYZ * Sensitivity;
        FLOAT LookX = XboxStickAxis( Pad.sThumbLX, DeadZone ) * Client->ScaleRUV * Sensitivity;
        FLOAT LookY = XboxStickAxis( Pad.sThumbLY, DeadZone ) * Client->ScaleRUV * Sensitivity;
        LX = MoveX;
        LY = MoveY;
        RX = LookX;
        RY = LookY;
    }
    if( Client->InvertVertical )
        RY = -RY;

    if( Player )
    {
        UBOOL FaceFireLayout = Client->ButtonLayout == 2;
        UBOOL FireNow =
            Pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER] > AnalogThreshold
        ||  (FaceFireLayout && Pad.bAnalogButtons[XINPUT_GAMEPAD_A] > AnalogThreshold);
        UBOOL AltFireNow =
            Pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER] > AnalogThreshold
        ||  Pad.bAnalogButtons[XINPUT_GAMEPAD_B] > AnalogThreshold;
        UBOOL DuckNow = (Pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        UBOOL JumpNow = FaceFireLayout
            ? XboxAnalogPressed( Pad, PrevControllerState.Gamepad, XINPUT_GAMEPAD_RIGHT_TRIGGER )
            : XboxAnalogPressed( Pad, PrevControllerState.Gamepad, XINPUT_GAMEPAD_A );

        Player->bFire    = FireNow ? 1 : 0;
        Player->bAltFire = AltFireNow ? 1 : 0;
        Player->bDuck    = DuckNow ? 1 : 0;
        if( FireNow || AltFireNow )
            Player->bReadyToPlay = 1;
        if( JumpNow )
            Player->bJumpStatus = !Player->bJumpStatus;

        Player->aStrafe += 0.01f * LX * 2.0f;
        Player->aBaseY  += 0.01f * LY * 2.0f;
        Player->aTurn   += 0.01f * RX * 5.9f;
        Player->aLookUp += 0.01f * RY * 3.0f;

        if( InputLogCount < 32
        &&  (FireNow || AltFireNow || DuckNow || JumpNow
        ||   LX > 0.01f || LX < -0.01f || LY > 0.01f || LY < -0.01f
        ||   RX > 0.01f || RX < -0.01f || RY > 0.01f || RY < -0.01f) )
        {
            InputLogCount++;
            GXboxLog.Write( "XINPUT direct #%d player=0x%08X fire=%d alt=%d duck=%d jump=%d axes=%.2f,%.2f,%.2f,%.2f",
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
