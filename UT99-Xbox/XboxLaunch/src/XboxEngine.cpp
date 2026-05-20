// XboxEngine.cpp
// Xbox implementations of InitEngine() and MainLoop().
// These replace the Windows versions that lived in UnEngineWin.h / WinDrv.

#include "XboxLaunchPrivate.h"

// ── InitEngine ────────────────────────────────────────────────────────────
// Creates and initializes the game engine object.
// Mirrors the Windows Launch implementation.
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

	GXboxLog.Write( "MainLoop: entering game loop (Engine=0x%08X)", (DWORD)Engine );
	GXboxLog.Write( "MainLoop: Xbox frame limiter active max=%.1f Hz", XboxMaxTickRate );

	while( GIsRunning && !GIsRequestingExit )
	{
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
	}

	GXboxLog.Write( "MainLoop: exiting (TickCount=%d)", TickCount );
	GIsRunning = 0;

	unguard;
}
