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

	// Load the engine class from config
	UClass* EngineClass = UObject::StaticLoadClass(
		UGameEngine::StaticClass(),
		NULL,
		TEXT("ini:Engine.Engine.GameEngine"),
		NULL,
		LOAD_NoFail,
		NULL
	);

	// Construct and initialize the engine
	UGameEngine* Engine = ConstructObject<UGameEngine>( EngineClass );
	Engine->Init();

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

	while( GIsRunning && !GIsRequestingExit )
	{
		// Calculate delta time
		DOUBLE NewTime = appSeconds();
		FLOAT DeltaTime = (FLOAT)(NewTime - OldTime);
		OldTime = NewTime;

		// Clamp delta time to avoid spiral of death
		if( DeltaTime > 1.0f )
			DeltaTime = 1.0f;

		// Tick the engine
		Engine->Tick( DeltaTime );

		// Controller input is polled in UXboxViewport::Tick

		TickCount++;
	}

	GIsRunning = 0;

	unguard;
}
