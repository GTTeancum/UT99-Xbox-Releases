// XboxEngine.cpp
// Xbox implementations of InitEngine() and MainLoop().
// These replace the Windows versions that lived in UnEngineWin.h / WinDrv.

#include "XboxLaunchPrivate.h"

extern DWORD GXboxMallocLiveBytes;
extern DWORD GXboxMallocPeakBytes;
extern DWORD GXboxMallocTotalBytes;
extern DWORD GXboxMallocLargestBytes;
extern DWORD GXboxMallocLastLargeBytes;
extern char  GXboxMallocLargestTag[64];
extern char  GXboxMallocLastLargeTag[64];

struct FXboxSmokeMatchStats
{
	INT PriCount;
	INT PriBots;
	INT PriSpectators;
	INT PriWaiting;
	INT TeamCount[4];
	INT TeamBots[4];
	FLOAT TeamScore[4];
	char ScoreSummary[768];
};

static UBOOL XboxSmokeGetObjectPropertyString( UObject* Object, const TCHAR* PropertyName, FString& OutValue )
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
	if( OutValue.Len() >= 2 && (*OutValue)[0] == '"' && (*OutValue)[OutValue.Len()-1] == '"' )
		OutValue = OutValue.Mid( 1, OutValue.Len() - 2 );
	if( appStricmp( *OutValue, TEXT("None") ) == 0 )
		OutValue = TEXT("");
	return OutValue.Len() > 0;
}

static INT XboxSmokeGetObjectPropertyInt( UObject* Object, const TCHAR* PropertyName, INT DefaultValue )
{
	UProperty* Property = (Object && PropertyName) ? FindField<UProperty>( Object->GetClass(), PropertyName ) : NULL;
	if( !Property )
		return DefaultValue;

	BYTE* Data = (BYTE*)Object + Property->Offset;
	if( Cast<UIntProperty>(Property) )
		return *(INT*)Data;
	if( Cast<UByteProperty>(Property) )
		return *(BYTE*)Data;

	FString Value;
	return XboxSmokeGetObjectPropertyString( Object, PropertyName, Value ) ? appAtoi( *Value ) : DefaultValue;
}

static FLOAT XboxSmokeGetObjectPropertyFloat( UObject* Object, const TCHAR* PropertyName, FLOAT DefaultValue )
{
	UProperty* Property = (Object && PropertyName) ? FindField<UProperty>( Object->GetClass(), PropertyName ) : NULL;
	if( !Property )
		return DefaultValue;

	BYTE* Data = (BYTE*)Object + Property->Offset;
	if( Cast<UFloatProperty>(Property) )
		return *(FLOAT*)Data;
	if( Cast<UIntProperty>(Property) )
		return (FLOAT)(*(INT*)Data);
	if( Cast<UByteProperty>(Property) )
		return (FLOAT)(*(BYTE*)Data);

	FString Value;
	return XboxSmokeGetObjectPropertyString( Object, PropertyName, Value ) ? appAtof( *Value ) : DefaultValue;
}

static void XboxSmokeAppendScore( char* Buffer, INT BufferSize, const char* Text )
{
	if( !Buffer || BufferSize <= 0 || !Text )
		return;

	INT Used = (INT)strlen( Buffer );
	if( Used >= BufferSize - 1 )
		return;

	INT Written = _snprintf( Buffer + Used, BufferSize - Used - 1, "%s%s", Used > 0 ? ";" : "", Text );
	if( Written < 0 )
		Buffer[BufferSize - 1] = 0;
}

static void XboxSmokeBuildMatchStats( AGameReplicationInfo* GRI, FXboxSmokeMatchStats& Stats )
{
	appMemzero( &Stats, sizeof(Stats) );
	Stats.ScoreSummary[0] = 0;
	if( !GRI )
		return;

	for( INT i=0; i<32; i++ )
	{
		APlayerReplicationInfo* PRI = GRI->PRIArray[i];
		if( !PRI )
			continue;

		Stats.PriCount++;
		if( PRI->bIsABot )
			Stats.PriBots++;
		if( PRI->bIsSpectator )
			Stats.PriSpectators++;
		if( PRI->bWaitingPlayer )
			Stats.PriWaiting++;

		INT Team = Clamp<INT>( PRI->Team, 0, 3 );
		if( !PRI->bIsSpectator )
		{
			Stats.TeamCount[Team]++;
			if( PRI->bIsABot )
				Stats.TeamBots[Team]++;
			Stats.TeamScore[Team] += PRI->Score;
		}

		if( strlen(Stats.ScoreSummary) < sizeof(Stats.ScoreSummary) - 96 )
		{
			char OneScore[128];
			_snprintf( OneScore, sizeof(OneScore)-1, "%s:T%d:%s:S%.0f:D%.0f%s%s",
				TCHAR_TO_ANSI(*PRI->PlayerName),
				(INT)PRI->Team,
				PRI->bIsABot ? "bot" : "human",
				PRI->Score,
				PRI->Deaths,
				PRI->bIsSpectator ? ":spec" : "",
				PRI->bWaitingPlayer ? ":wait" : "" );
			OneScore[sizeof(OneScore)-1] = 0;
			XboxSmokeAppendScore( Stats.ScoreSummary, sizeof(Stats.ScoreSummary), OneScore );
		}
	}
}

// ── InitEngine ────────────────────────────────────────────────────────────
// Creates and initializes the game engine object.
// Mirrors the Windows Launch implementation.
static UBOOL XboxSmokeLoadMapList( TArray<FString>& OutURLs )
{
	guard(XboxSmokeLoadMapList);

	OutURLs.Empty();
	FString ConfigText;
	if( !appLoadFileToString( ConfigText, TEXT("D:\\XboxSoakMapList.ini"), GFileManager ) )
		return 0;

	const TCHAR* Stream = *ConfigText;
	TCHAR Line[4096];
	while( ParseLine( &Stream, Line, ARRAY_COUNT(Line), 1 ) )
	{
		TCHAR* Start = Line;
		while( *Start==' ' || *Start=='\t' )
			Start++;

		TCHAR* End = Start + appStrlen(Start);
		while( End > Start && (End[-1]==' ' || End[-1]=='\t' || End[-1]=='\r' || End[-1]=='\n') )
			*--End = 0;

		if( !Start[0] || Start[0]==';' || Start[0]=='#' || Start[0]=='[' )
			continue;

		if( appStrnicmp( Start, TEXT("StartURL="), 9 ) == 0 )
			Start += 9;
		else if( appStrnicmp( Start, TEXT("URL="), 4 ) == 0 )
			Start += 4;

		while( *Start==' ' || *Start=='\t' )
			Start++;
		End = Start + appStrlen(Start);
		while( End > Start && (End[-1]==' ' || End[-1]=='\t' || End[-1]=='\r' || End[-1]=='\n') )
			*--End = 0;

		if( Start[0] )
			new(OutURLs) FString(Start);
	}

	GXboxLog.Write( "SMOKE map-list loaded count=%d", OutURLs.Num() );
	for( INT i=0; i<OutURLs.Num(); i++ )
		GXboxLog.Write( "SMOKE map-list item index=%d url=%s", i, TCHAR_TO_ANSI(*OutURLs(i)) );
	return OutURLs.Num() > 0;

	unguard;
}

UEngine* InitEngine()
{
	guard(InitEngine);

	// ── Pre-flight: dump GSys state so we know packages are findable ──
	GXboxLog.Write( "InitEngine: GSys=%s, GSys->Paths.Num=%d",
		GSys ? "OK" : "NULL",
		GSys ? GSys->Paths.Num() : -1 );
	if( GSys )
	{
		for( INT i=0; i<GSys->Paths.Num() && i<8; i++ )
			GXboxLog.Write( "InitEngine:   Paths[%d] = %s", i, TCHAR_TO_ANSI(*GSys->Paths(i)) );
	}
	GXboxLog.Write( "InitEngine: GIsClient=%d GIsServer=%d GIsScriptable=%d GIsEditor=%d",
		GIsClient, GIsServer, GIsScriptable, GIsEditor );

	static const UBOOL bXboxVerboseClassSizeLog = 0;
	if( bXboxVerboseClassSizeLog )
	{
#define XLOG_CLASS_SIZE(T) \
		GXboxLog.Write( "XSIZE %-18s sizeof=%d props=%d delta=%d", \
			#T, (INT)sizeof(T), T::StaticClass()->GetPropertiesSize(), \
			(INT)sizeof(T) - T::StaticClass()->GetPropertiesSize() )
		XLOG_CLASS_SIZE(UObject);
		XLOG_CLASS_SIZE(UBitmap);
		XLOG_CLASS_SIZE(UTexture);
		XLOG_CLASS_SIZE(AActor);
		XLOG_CLASS_SIZE(APawn);
		XLOG_CLASS_SIZE(APlayerPawn);
		XLOG_CLASS_SIZE(ALevelInfo);
		XLOG_CLASS_SIZE(AInventory);
		XLOG_CLASS_SIZE(APickup);
		XLOG_CLASS_SIZE(AAmmo);
		XLOG_CLASS_SIZE(AWeapon);
		XLOG_CLASS_SIZE(AHUD);
#undef XLOG_CLASS_SIZE
	}

	GXboxLog.Write( "InitEngine: loading GameEngine class from config" );

	// Load the engine class from config
	UClass* EngineClass = UObject::StaticLoadClass(
		UGameEngine::StaticClass(),
		NULL,
		TEXT("ini:Engine.Engine.GameEngine"),
		NULL,
		LOAD_NoFail,
		NULL
	);

	GXboxLog.Write( "InitEngine: EngineClass=%s name=%s",
		EngineClass ? "OK" : "NULL",
		EngineClass ? TCHAR_TO_ANSI(EngineClass->GetName()) : "(null)" );

	// ── Pre-deref: ConstructObject will crash if EngineClass is NULL ──
	if( !EngineClass )
		GXboxLog.Write( "InitEngine: WARNING — EngineClass is NULL, ConstructObject will crash" );

	// Construct and initialize the engine
	UGameEngine* Engine = ConstructObject<UGameEngine>( EngineClass );

	GXboxLog.Write( "InitEngine: ConstructObject returned Engine=0x%08X", (DWORD)Engine );

	if( !Engine )
		GXboxLog.Write( "InitEngine: WARNING — Engine is NULL, Init() will crash" );

	GXboxLog.Write( "InitEngine: calling Engine->Init()" );
	Engine->Init();
	GXboxLog.Write( "InitEngine: Engine->Init() returned" );

	// ── Post-Init state dump: what subsystems came up? ──
	if( Engine )
	{
		UGameEngine* GE = (UGameEngine*)Engine;
		GXboxLog.Write( "InitEngine: post-Init Client=0x%08X Audio=0x%08X GLevel=0x%08X GEntry=0x%08X",
			(DWORD)GE->Client, (DWORD)GE->Audio,
			(DWORD)GE->GLevel, (DWORD)GE->GEntry );
		if( GE->Client )
		{
			GXboxLog.Write( "InitEngine:   Client class=%s Viewports.Num=%d",
				TCHAR_TO_ANSI(GE->Client->GetClass()->GetName()),
				GE->Client->Viewports.Num() );
			for( INT i=0; i<GE->Client->Viewports.Num() && i<4; i++ )
			{
				UViewport* VP = GE->Client->Viewports(i);
				GXboxLog.Write( "InitEngine:     VP[%d] class=%s RenDev=0x%08X Actor=0x%08X SizeX=%d SizeY=%d",
					i,
					VP ? TCHAR_TO_ANSI(VP->GetClass()->GetName()) : "(null)",
					VP ? (DWORD)VP->RenDev : 0,
					VP ? (DWORD)VP->Actor  : 0,
					VP ? VP->SizeX : -1,
					VP ? VP->SizeY : -1 );
			}
		}
		if( GE->GLevel )
		{
			GXboxLog.Write( "InitEngine:   GLevel Actors.Num=%d URL=%s",
				GE->GLevel->Actors.Num(),
				TCHAR_TO_ANSI(*GE->GLevel->URL.String()) );
		}
	}

	return Engine;

	unguard;
}

// ── MainLoop ──────────────────────────────────────────────────────────────
// Runs the game loop until GIsRequestingExit is set.
void MainLoop( UEngine* Engine )
{
	guard(MainLoop);

	check(Engine);
	GIsRunning = 1;

	DOUBLE OldTime = appSeconds();
	DOUBLE SecondStartTime = OldTime;
	INT TickCount = 0;
	const FLOAT XboxMaxTickRate = 60.0f;
	FString LastSmokeURL;
	FString XboxStartSmokeReadyURL;
	TArray<FString> XboxSoakURLs;
	INT XboxSoakIndex = 0;
	UBOOL bXboxSoakEnabled = XboxSmokeLoadMapList( XboxSoakURLs );
	UBOOL bXboxSoakTravelScheduled = 0;
	UBOOL bXboxStartSmokeEnabled = bXboxSoakEnabled || (GetFileAttributesA( "D:\\XboxStartURL.ini" ) != 0xFFFFFFFF);
	UBOOL bXboxCharacterSoakEnabled = GetFileAttributesA( "D:\\XboxCharacterSoak.ini" ) != 0xFFFFFFFF;
	UBOOL bXboxSmokeMatchEndLogged = 0;
	DWORD XboxSoakCameraNextTime = 0;
	INT XboxSoakCameraIndex = 0;

	GXboxLog.Write( "MainLoop: entering game loop (Engine=0x%08X)", (DWORD)Engine );
	GXboxLog.Write( "MainLoop: Xbox frame limiter active max=%.1f Hz", XboxMaxTickRate );

	while( GIsRunning && !GIsRequestingExit )
	{
		XboxDebugHeartbeat();

		// Calculate delta time
		DOUBLE NewTime = appSeconds();
		FLOAT DeltaTime = (FLOAT)(NewTime - OldTime);
		OldTime = NewTime;

		// Clamp delta time to avoid spiral of death
		if( DeltaTime > 1.0f )
			DeltaTime = 1.0f;

		// ── First-tick state dump: confirm what we're about to tick ──
		if( TickCount == 0 )
		{
			UGameEngine* GE = (UGameEngine*)Engine;
			GXboxLog.Write( "MainLoop: pre-tick-1 Engine=0x%08X Client=0x%08X GLevel=0x%08X",
				(DWORD)GE,
				GE ? (DWORD)GE->Client : 0,
				GE ? (DWORD)GE->GLevel : 0 );
			if( GE && GE->Client )
				GXboxLog.Write( "MainLoop: pre-tick-1 Viewports.Num=%d", GE->Client->Viewports.Num() );
			if( GE && GE->GLevel )
				GXboxLog.Write( "MainLoop: pre-tick-1 GLevel->Actors.Num=%d", GE->GLevel->Actors.Num() );
		}

		const UBOOL bVerboseTickLog = 0;
		UBOOL bBoundaryTick = bVerboseTickLog && ((TickCount >= 79 && TickCount <= 139)
			|| (TickCount >= 209 && TickCount <= 259)
			|| (TickCount >= 299 && TickCount <= 359));

		if( bBoundaryTick )
			GXboxLog.Write( "MainLoop: pre-tick %d dt=%.3f", TickCount + 1, DeltaTime );

		// Keep the diagnostic camera alive and unobstructed for rendered character
		// soaks. This path is enabled only by the staged XboxCharacterSoak.ini.
		if( bXboxCharacterSoakEnabled )
		{
			UGameEngine* SoakEngine = (UGameEngine*)Engine;
			UViewport* SoakViewport = (SoakEngine && SoakEngine->Client && SoakEngine->Client->Viewports.Num() > 0)
				? SoakEngine->Client->Viewports(0)
				: NULL;
			APlayerPawn* SoakActor = SoakViewport ? SoakViewport->Actor : NULL;
			if( SoakActor )
			{
				SoakActor->ReducedDamageType = FName(TEXT("All"));
				SoakActor->Health = Max( SoakActor->Health, 100 );
				SoakActor->bBehindView = 1;
				SoakActor->DesiredFOV = 40.0f;
				SoakActor->bShowScores = 0;
				SoakActor->bShowMenu = 0;
				SoakActor->bSpecialMenu = 0;

				DWORD CameraNow = GetTickCount();
				ULevel* SoakLevel = SoakActor->GetLevel();
				if( TickCount >= 30 && SoakLevel && SoakLevel->GetLevelInfo()
				&& (!XboxSoakCameraNextTime || (INT)(CameraNow-XboxSoakCameraNextTime) >= 0) )
				{
					INT LiveBotCount = 0;
					for( APawn* Bot = SoakLevel->GetLevelInfo()->PawnList; Bot; Bot = Bot->nextPawn )
					{
						Bot->bViewTarget = 0;
						if( Bot->PlayerReplicationInfo && Bot->PlayerReplicationInfo->bIsABot && Bot->Health > 0 )
							LiveBotCount++;
					}
					if( LiveBotCount )
					{
						const INT WantedBot = XboxSoakCameraIndex % LiveBotCount;
						INT BotIndex = 0;
						for( APawn* Bot = SoakLevel->GetLevelInfo()->PawnList; Bot; Bot = Bot->nextPawn )
						{
							if( !Bot->PlayerReplicationInfo || !Bot->PlayerReplicationInfo->bIsABot || Bot->Health <= 0 )
								continue;
							if( BotIndex++ == WantedBot )
							{
								Bot->ReducedDamageType = FName(TEXT("All"));
								Bot->Health = Max( Bot->Health, 100 );
								SoakActor->ViewTarget = Bot;
								Bot->bViewTarget = 1;
								GXboxLog.Write( "XSKELCAM tick=%d index=%d class=%s",
									TickCount,
									WantedBot,
									Bot->GetClass() ? TCHAR_TO_ANSI(Bot->GetClass()->GetFullName()) : "(none)" );
								break;
							}
						}
						XboxSoakCameraIndex++;
						XboxSoakCameraNextTime = CameraNow + 300000;
					}
				}
			}
		}

		// Tick the engine
		Engine->Tick( DeltaTime );

		if( bBoundaryTick )
			GXboxLog.Write( "MainLoop: post-tick %d", TickCount + 1 );

		// Controller input is polled in UXboxViewport::Tick

		TickCount++;

		// Original standalone UT can return 0 from GetMaxTickRate(), which
		// means uncapped. On Xbox/CXBX that hammers Tick->Present hundreds of
		// times per second and can starve the D3D/runtime side. Match the
		// healthy Xbox ports by yielding every loop, and cap standalone play
		// to a sane display-rate cadence.
		DOUBLE TickElapsed = appSeconds() - NewTime;
		DOUBLE TargetFrame = 1.0 / XboxMaxTickRate;
		if( TickElapsed < TargetFrame )
			appSleep( (FLOAT)(TargetFrame - TickElapsed) );
		else
			appSleep( 0.001f );

		// Log first few ticks so we know the loop is running
		if( TickCount <= 2 )
			GXboxLog.Write( "MainLoop: tick %d (dt=%.3f)", TickCount, DeltaTime );

		// Periodic heartbeat — every 60 ticks (~1s at 60Hz) so we know we're alive
		static const UBOOL GXboxVerboseHeartbeatLog = 0;
		if( GXboxVerboseHeartbeatLog && TickCount > 2 && (TickCount % 300) == 0 )
			GXboxLog.Write( "MainLoop: heartbeat tick=%d", TickCount );

		if( TickCount == 30 || (TickCount > 30 && (TickCount % 300) == 0) )
		{
			UGameEngine* GE = (UGameEngine*)Engine;
			ULevel* Level = GE ? GE->GLevel : NULL;
			if( Level && Level->GetLevelInfo() )
			{
				FString CurrentURL = Level->URL.String();
				if( bXboxStartSmokeEnabled && CurrentURL != XboxStartSmokeReadyURL && Level->URL.Map.Len() > 0 && appStricmp( *Level->URL.Map, TEXT("CityIntro") ) != 0 )
				{
					INT ReadyPlayers = 0;
					for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; Pawn = Pawn->nextPawn )
					{
						APlayerPawn* Player = Cast<APlayerPawn>(Pawn);
						if( Player )
						{
							Player->bReadyToPlay = 1;
							Player->bShowMenu = 0;
							Player->bSpecialMenu = 0;
							if( bXboxCharacterSoakEnabled )
							{
								Player->ReducedDamageType = FName(TEXT("All"));
								Player->Health = Max( Player->Health, 100 );
							}
							ReadyPlayers++;
						}
					}
					XboxStartSmokeReadyURL = CurrentURL;
					GXboxLog.Write( "SMOKE auto-ready players=%d url=%s", ReadyPlayers, TCHAR_TO_ANSI(*CurrentURL) );
				}

				INT PawnCount = 0;
				INT PlayerPawnCount = 0;
				INT BotPawnCount = 0;
				if( TickCount == 30 )
					GXboxLog.Write( "SMOKE stage=post-ready level=0x%08X info=0x%08X", (DWORD)Level, (DWORD)Level->GetLevelInfo() );
				MEMORYSTATUS MemStatus;
				appMemzero( &MemStatus, sizeof(MemStatus) );
				MemStatus.dwLength = sizeof(MemStatus);
				GlobalMemoryStatus( &MemStatus );
				if( TickCount == 30 )
					GXboxLog.Write( "SMOKE stage=memory availKB=%d", MemStatus.dwAvailPhys / 1024 );
				for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; Pawn = Pawn->nextPawn )
				{
					PawnCount++;
					if( Cast<APlayerPawn>(Pawn) )
						PlayerPawnCount++;
					if( Pawn->PlayerReplicationInfo && Pawn->PlayerReplicationInfo->bIsABot )
						BotPawnCount++;
				}
				if( TickCount == 30 )
					GXboxLog.Write( "SMOKE stage=pawns count=%d players=%d bots=%d", PawnCount, PlayerPawnCount, BotPawnCount );

				AGameInfo* Game = Level->GetLevelInfo()->Game;
				AGameReplicationInfo* GRI = Game ? Game->GameReplicationInfo : NULL;
				if( TickCount == 30 )
					GXboxLog.Write( "SMOKE stage=game game=0x%08X gri=0x%08X", (DWORD)Game, (DWORD)GRI );
				FXboxSmokeMatchStats MatchStats;
				XboxSmokeBuildMatchStats( GRI, MatchStats );
				if( TickCount == 30 )
					GXboxLog.Write( "SMOKE stage=match-stats pri=%d bots=%d", MatchStats.PriCount, MatchStats.PriBots );
				FString EndedComment = TEXT("");
				if( GRI )
					EndedComment = GRI->GameEndedComments;
				INT RemainingTime = GRI ? GRI->RemainingTime : -1;
				INT ElapsedTime = GRI ? GRI->ElapsedTime : -1;
				INT MinPlayers = XboxSmokeGetObjectPropertyInt( Game, TEXT("MinPlayers"), -1 );
				INT InitialBots = XboxSmokeGetObjectPropertyInt( Game, TEXT("InitialBots"), -1 );
				INT NumBots = XboxSmokeGetObjectPropertyInt( Game, TEXT("NumBots"), -1 );
				INT RemainingBots = XboxSmokeGetObjectPropertyInt( Game, TEXT("RemainingBots"), -1 );
				INT TimeLimit = XboxSmokeGetObjectPropertyInt( Game, TEXT("TimeLimit"), -1 );
				FLOAT GoalTeamScore = XboxSmokeGetObjectPropertyFloat( Game, TEXT("GoalTeamScore"), -1.0f );
				if( TickCount == 30 )
					GXboxLog.Write( "SMOKE stage=properties min=%d initial=%d bots=%d remaining=%d time=%d goal=%.1f", MinPlayers, InitialBots, NumBots, RemainingBots, TimeLimit, GoalTeamScore );
				UBOOL bURLChanged = CurrentURL != LastSmokeURL;
				if( bURLChanged )
				{
					bXboxSmokeMatchEndLogged = 0;
					bXboxSoakTravelScheduled = 0;
				}
				UBOOL bMatchEnded = Game && Game->bGameEnded;
				UBOOL bForceMatchEndLog = bMatchEnded && !bXboxSmokeMatchEndLogged;

				if( bURLChanged || (TickCount % 300) == 0 || bForceMatchEndLog )
				{
					UViewport* LocalViewport = (GE && GE->Client && GE->Client->Viewports.Num() > 0) ? GE->Client->Viewports(0) : NULL;
					APlayerPawn* LocalActor = LocalViewport ? LocalViewport->Actor : NULL;
					if( bXboxCharacterSoakEnabled && LocalActor )
					{
						LocalActor->ReducedDamageType = FName(TEXT("All"));
						LocalActor->Health = Max( LocalActor->Health, 100 );
						LocalActor->bBehindView = 1;
					}
					ULevel* LocalLevel = LocalActor ? LocalActor->GetLevel() : NULL;
					APlayerReplicationInfo* LocalPRI = LocalActor ? LocalActor->PlayerReplicationInfo : NULL;
					LastSmokeURL = CurrentURL;
					GXboxLog.Write( "SMOKE tick=%d url=%s game=%s ended=%d rem=%d elapsed=%d actors=%d pawns=%d players=%d bots=%d minPlayers=%d initialBots=%d numBots=%d remainingBots=%d goalTeamScore=%.1f timeLimit=%d pri=%d priBots=%d priSpec=%d priWait=%d teams=%d/%d/%d/%d teamBots=%d/%d/%d/%d teamScore=%.0f/%.0f/%.0f/%.0f localClass=%s localLevel=%s localPRI=%s ready=%d showMenu=%d waiting=%d spectator=%d availKB=%d heapLiveKB=%u heapPeakKB=%u heapTotalKB=%u largestKB=%u largestTag=%s lastLargeKB=%u lastLargeTag=%s comment=%s",
						TickCount,
						TCHAR_TO_ANSI(*CurrentURL),
						(Game && Game->GetClass()) ? TCHAR_TO_ANSI(Game->GetClass()->GetName()) : "(none)",
						bMatchEnded ? 1 : 0,
						RemainingTime,
						ElapsedTime,
						Level->Actors.Num(),
						PawnCount,
						PlayerPawnCount,
						BotPawnCount,
						MinPlayers,
						InitialBots,
						NumBots,
						RemainingBots,
						GoalTeamScore,
						TimeLimit,
						MatchStats.PriCount,
						MatchStats.PriBots,
						MatchStats.PriSpectators,
						MatchStats.PriWaiting,
						MatchStats.TeamCount[0],
						MatchStats.TeamCount[1],
						MatchStats.TeamCount[2],
						MatchStats.TeamCount[3],
						MatchStats.TeamBots[0],
						MatchStats.TeamBots[1],
						MatchStats.TeamBots[2],
						MatchStats.TeamBots[3],
						MatchStats.TeamScore[0],
						MatchStats.TeamScore[1],
						MatchStats.TeamScore[2],
						MatchStats.TeamScore[3],
						(LocalActor && LocalActor->GetClass()) ? TCHAR_TO_ANSI(LocalActor->GetClass()->GetName()) : "(none)",
						(LocalLevel && LocalLevel->URL.Map.Len()) ? TCHAR_TO_ANSI(*LocalLevel->URL.Map) : "(none)",
						LocalPRI ? TCHAR_TO_ANSI(*LocalPRI->PlayerName) : "(none)",
						LocalActor ? LocalActor->bReadyToPlay : 0,
						LocalActor ? LocalActor->bShowMenu : 0,
						LocalPRI ? LocalPRI->bWaitingPlayer : 0,
						LocalPRI ? LocalPRI->bIsSpectator : 0,
						MemStatus.dwAvailPhys / 1024,
						(unsigned)(GXboxMallocLiveBytes / 1024),
						(unsigned)(GXboxMallocPeakBytes / 1024),
						(unsigned)(GXboxMallocTotalBytes / 1024),
						(unsigned)(GXboxMallocLargestBytes / 1024),
						GXboxMallocLargestTag,
						(unsigned)(GXboxMallocLastLargeBytes / 1024),
						GXboxMallocLastLargeTag,
						TCHAR_TO_ANSI(*EndedComment) );
					if( MatchStats.ScoreSummary[0] )
						GXboxLog.Write( "SMOKE scores tick=%d %s", TickCount, MatchStats.ScoreSummary );
					if( bXboxCharacterSoakEnabled )
					{
						for( APawn* Pawn = Level->GetLevelInfo()->PawnList; Pawn; Pawn = Pawn->nextPawn )
						{
							APlayerReplicationInfo* PRI = Pawn->PlayerReplicationInfo;
							if( !PRI || !PRI->bIsABot )
								continue;

							GXboxLog.Write( "XCHAR tick=%d name=%s class=%s mesh=%s skin=%s multi0=%s multi1=%s multi2=%s multi3=%s team=%d score=%.0f deaths=%.0f availKB=%d",
								TickCount,
								TCHAR_TO_ANSI(*PRI->PlayerName),
								Pawn->GetClass() ? TCHAR_TO_ANSI(Pawn->GetClass()->GetFullName()) : "(none)",
								Pawn->Mesh ? TCHAR_TO_ANSI(Pawn->Mesh->GetFullName()) : "(none)",
								Pawn->Skin ? TCHAR_TO_ANSI(Pawn->Skin->GetFullName()) : "(none)",
								Pawn->MultiSkins[0] ? TCHAR_TO_ANSI(Pawn->MultiSkins[0]->GetFullName()) : "(none)",
								Pawn->MultiSkins[1] ? TCHAR_TO_ANSI(Pawn->MultiSkins[1]->GetFullName()) : "(none)",
								Pawn->MultiSkins[2] ? TCHAR_TO_ANSI(Pawn->MultiSkins[2]->GetFullName()) : "(none)",
								Pawn->MultiSkins[3] ? TCHAR_TO_ANSI(Pawn->MultiSkins[3]->GetFullName()) : "(none)",
								(INT)PRI->Team,
								PRI->Score,
								PRI->Deaths,
								MemStatus.dwAvailPhys / 1024 );
						}
					}
					if( bForceMatchEndLog )
					{
						GXboxLog.Write( "SMOKE match-ended tick=%d url=%s rem=%d elapsed=%d pri=%d priBots=%d teams=%d/%d/%d/%d teamScore=%.0f/%.0f/%.0f/%.0f comment=%s",
							TickCount,
							TCHAR_TO_ANSI(*CurrentURL),
							RemainingTime,
							ElapsedTime,
							MatchStats.PriCount,
							MatchStats.PriBots,
							MatchStats.TeamCount[0],
							MatchStats.TeamCount[1],
							MatchStats.TeamCount[2],
							MatchStats.TeamCount[3],
							MatchStats.TeamScore[0],
							MatchStats.TeamScore[1],
							MatchStats.TeamScore[2],
							MatchStats.TeamScore[3],
							TCHAR_TO_ANSI(*EndedComment) );
						bXboxSmokeMatchEndLogged = 1;
						if( bXboxSoakEnabled && !bXboxSoakTravelScheduled )
						{
							if( XboxSoakIndex + 1 < XboxSoakURLs.Num() )
							{
								XboxSoakIndex++;
								Level->GetLevelInfo()->NextURL = XboxSoakURLs(XboxSoakIndex);
								Level->GetLevelInfo()->bNextItems = 0;
								Level->GetLevelInfo()->NextSwitchCountdown = 5.0f;
								bXboxSoakTravelScheduled = 1;
								GXboxLog.Write( "SMOKE map-advance scheduled tick=%d index=%d/%d next=%s delay=5.0",
									TickCount,
									XboxSoakIndex,
									XboxSoakURLs.Num(),
									TCHAR_TO_ANSI(*XboxSoakURLs(XboxSoakIndex)) );
							}
							else
							{
								bXboxSoakTravelScheduled = 1;
								GXboxLog.Write( "SMOKE map-list complete tick=%d index=%d count=%d",
									TickCount,
									XboxSoakIndex,
									XboxSoakURLs.Num() );
							}
						}
					}
				}
			}
		}
	}

	GXboxLog.Write( "MainLoop: exiting (TickCount=%d)", TickCount );
	GIsRunning = 0;

	unguard;
}
