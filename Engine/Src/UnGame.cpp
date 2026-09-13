/*=============================================================================
	UnGame.cpp: Unreal game engine.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnNet.h"

/*-----------------------------------------------------------------------------
	Object class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UGameEngine);

#if TARGET_XBOX
extern void XboxCanvasProfileBegin();
extern DOUBLE XboxCanvasProfileEnd(INT& Calls);
extern void XboxMenuPostRender( UViewport* Viewport, UCanvas* Canvas );
extern "C" void XboxViewportApplyProfileHUD( UViewport* Viewport );
extern "C" void XboxViewportApplyViewRegion( UViewport* Viewport, FSceneNode* Frame );
extern "C" UBOOL XboxViewportShouldPostRenderPlayer( UViewport* Viewport );
extern "C" UBOOL XboxViewportShouldUpdateAudio( UViewport* Viewport );
extern "C" void XboxMenuPreClientTravelCleanup();
extern "C" void XboxSystemLinkAbortTravelCleanup( const char* Reason );
extern "C" void XboxDebugMirrorWriteAnsi( const char* Line );
extern "C" void XboxRenderDrawMenuRect( FSceneNode* Frame, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, BYTE A );
extern "C" void XboxRenderRequestLoadingFrameBackground( URenderDevice* RenderDevice );
extern "C" void XboxRenderReleaseLoadingFrameBackground( URenderDevice* RenderDevice );
extern "C" UBOOL XboxRenderDrawLoadingFrame( URenderDevice* RenderDevice, INT Step, INT DrawCount );
extern DWORD GXboxMallocLiveBytes;
extern DWORD GXboxMallocPeakBytes;
extern DWORD GXboxMallocTotalBytes;
extern DWORD GXboxMallocLargestBytes;
extern DWORD GXboxMallocLastLargeBytes;
extern char  GXboxMallocLargestTag[64];
extern char  GXboxMallocLastLargeTag[64];
static UBOOL GXboxShowLoadActivity = 0;
static UBOOL GXboxDrawingLoadActivity = 0;
static UGameEngine* GXboxLoadActivityEngine = NULL;
static INT GXboxLoadActivityStep = 0;
static INT GXboxLoadActivityDrawCount = 0;
static DOUBLE GXboxLoadActivityStartTime = 0.0;
static DOUBLE GXboxLoadActivityLastDrawTime = 0.0;
static UBOOL GXboxSuppressLoadActivityDraw = 0;
static UBOOL GXboxLightingProofInitialized = 0;
static UBOOL GXboxLightingProofEnabled = 0;
static ULevel* GXboxLightingProofLevel = NULL;
static TArray<AActor*> GXboxLightingProofSlots;
static FVector GXboxLightingProofCenter(0,0,0);
static INT GXboxLightingProofStartDraw = 0;
static INT GXboxLightingProofLastSlot = INDEX_NONE;

static void XboxMirrorLoadActivityLine( const char* Line )
{
	if( !Line )
		return;
	OutputDebugStringA( Line );
	XboxDebugMirrorWriteAnsi( Line );
}

struct FXboxSkeletalProofState
{
	const TCHAR* Label;
	const TCHAR* Candidates[7];
};

static const FXboxSkeletalProofState GXboxSkeletalProofStates[] =
{
	{ TEXT("idle"),   { TEXT("Breath1"),   TEXT("Breath2"),   TEXT("Breath3"),   TEXT("StillFrRp"), TEXT("StillLgFr"), NULL, NULL } },
	{ TEXT("walk"),   { TEXT("WalkLg"),    TEXT("WalkSm"),    TEXT("Walk"),      NULL, NULL, NULL, NULL } },
	{ TEXT("run"),    { TEXT("RunLg"),     TEXT("RunSm"),     TEXT("Jog"),       TEXT("Run"), NULL, NULL, NULL } },
	{ TEXT("jump"),   { TEXT("JumpLgFr"),  TEXT("JumpSmFr"),  TEXT("InAir"),     NULL, NULL, NULL, NULL } },
	{ TEXT("attack"), { TEXT("StillFrRp"), TEXT("StillSmFr"), TEXT("Firing"),    TEXT("Fire"), NULL, NULL, NULL } },
	{ TEXT("hit"),    { TEXT("GutHit"),    TEXT("HeadHit"),   TEXT("LeftHit"),   TEXT("RightHit"), NULL, NULL, NULL } },
	{ TEXT("dodge"),  { TEXT("DodgeL"),    TEXT("RollLeft"),  TEXT("LeftDodge"), TEXT("DodgeF"), NULL, NULL, NULL } },
	{ TEXT("swim"),   { TEXT("SwimLg"),    TEXT("SwimSm"),    TEXT("Swim"),      TEXT("TreadLg"), TEXT("TreadSm"), NULL, NULL } },
	{ TEXT("crouch"), { TEXT("DuckWlkL"),  TEXT("DuckWlkS"),  TEXT("DuckStill"), TEXT("Duck"), NULL, NULL, NULL } },
	{ TEXT("death"),  { TEXT("Dead4"),     TEXT("Dead1"),     TEXT("Death"),     TEXT("Death2"), TEXT("Dead2"), NULL, NULL } },
};

static UBOOL XboxSkeletalStateProofEnabled()
{
	static UBOOL Initialized = 0;
	static UBOOL Enabled = 0;
	if( !Initialized )
	{
		Enabled = GetFileAttributesA( "D:\\XboxSkeletalStateProof.ini" ) != 0xFFFFFFFF;
		Initialized = 1;
	}
	return Enabled;
}

static UBOOL XboxSkaarjSkinProofEnabled()
{
	static UBOOL Initialized = 0;
	static UBOOL Enabled = 0;
	if( !Initialized )
	{
		Enabled = GetFileAttributesA( "D:\\XboxSkaarjSkinProof.ini" ) != 0xFFFFFFFF;
		Initialized = 1;
	}
	return Enabled;
}

static void XboxApplySkaarjSkinProof( ULevel* Level )
{
	if( !XboxSkaarjSkinProofEnabled() || !Level || !Level->GetLevelInfo() )
		return;

	APawn* Target = NULL;
	for( APawn* Pawn=Level->GetLevelInfo()->PawnList; Pawn; Pawn=Pawn->nextPawn )
	{
		if
		(
			Pawn->bViewTarget
		&&	Pawn->Health > 0
		&&	Pawn->GetClass()
		&&	appStrstr( Pawn->GetClass()->GetName(), TEXT("SkaarjHybrid") )
		)
		{
			Target = Pawn;
			break;
		}
	}
	if( !Target )
		return;

	static APawn* LastTarget = NULL;
	static INT ProofTick = 0;
	if( Target != LastTarget )
	{
		LastTarget = Target;
		ProofTick = 0;
	}

	if( (ProofTick % 15) == 0 )
	{
		UBoolProperty* GreenBloodProperty
			= FindField<UBoolProperty>( Target->GetClass(), TEXT("bGreenBlood") );
		const BITFIELD* Value = GreenBloodProperty
			? (const BITFIELD*)((BYTE*)Target + GreenBloodProperty->Offset)
			: NULL;
		const UBOOL GreenBlood = Value
			? ((*Value & GreenBloodProperty->BitMask) != 0)
			: 0;
		const INT SavedHealth = Target->Health;
		const FVector HitLocation
			= Target->Location + FVector(0,0,Target->CollisionHeight * 0.55f);
		Target->eventTakeDamage
		(
			1,
			Target,
			HitLocation,
			FVector(30,0,0),
			FName(TEXT("shot"))
		);
		Target->Health = SavedHealth;
		if( (ProofTick % 60) == 0 )
		{
			debugf
			(
				NAME_Log,
				TEXT("XSKAARJSKINPROOF tick=%i class=%s greenblood=%i health=%i"),
				(INT)GTicks,
				Target->GetClass()->GetFullName(),
				GreenBlood,
				Target->Health
			);
		}
	}
	ProofTick++;
}

static const FMeshAnimSeq* XboxFindSkeletalProofSequence
(
	UMesh* Mesh,
	const FXboxSkeletalProofState& State,
	FName& SequenceName
)
{
	if( !Mesh )
		return NULL;

	for( INT CandidateIndex=0; CandidateIndex<ARRAY_COUNT(State.Candidates); CandidateIndex++ )
	{
		if( !State.Candidates[CandidateIndex] )
			break;
		FName Candidate( State.Candidates[CandidateIndex] );
		const FMeshAnimSeq* Sequence = Mesh->GetAnimSeq( Candidate );
		if( Sequence )
		{
			SequenceName = Candidate;
			return Sequence;
		}
	}
	return NULL;
}

static FLOAT XboxSkeletalProofFrame( FLOAT Elapsed, INT NumFrames, FLOAT Rate, UBOOL Loop )
{
	if( NumFrames <= 1 || Rate <= 0.0f )
		return 0.0f;
	FLOAT Frame = Max(Elapsed,0.0f) * Rate / NumFrames;
	return Loop ? Frame-appFloor(Frame) : Min(Frame,1.0f-1.0f/NumFrames);
}

static void XboxApplySkeletalStateProof( ULevel* Level, FLOAT DeltaSeconds )
{
	if( !XboxSkeletalStateProofEnabled() || !Level || !Level->GetLevelInfo() )
		return;

	APawn* Target = NULL;
	for( APawn* Pawn=Level->GetLevelInfo()->PawnList; Pawn; Pawn=Pawn->nextPawn )
	{
		if
		(
			Pawn->bViewTarget
		&&	Pawn->Health > 0
		&&	Pawn->Mesh
		&&	Pawn->Mesh->IsA(USkeletalMesh::StaticClass())
		)
		{
			Target = Pawn;
			break;
		}
	}
	if( !Target )
		return;

	enum { STATE_LOG_INTERVAL = 15 };
	static APawn* LastTarget = NULL;
	static INT ProofTick = 0;
	static INT ProofStateIndex = 0;
	static FLOAT StateElapsed = 0.0f;
	static INT LastStateIndex = INDEX_NONE;
	static FRotator ProofRotation(0,0,0);
	if( Target != LastTarget )
	{
		LastTarget = Target;
		ProofTick = 0;
		ProofStateIndex = 0;
		StateElapsed = 0.0f;
		LastStateIndex = INDEX_NONE;
		ProofRotation = Target->Rotation;
		// A state proof must not freeze the bot under a lift or against a wall.
		// Choose the map's most open player start; gameplay capture never uses
		// this path and continues to follow ordinary bot navigation.
		FLOAT BestClearance = -1.0f;
		AActor* BestStart = NULL;
		for( INT ActorIndex=0; ActorIndex<Level->Actors.Num(); ActorIndex++ )
		{
			AActor* Start = Level->Actors(ActorIndex);
			if( !Start || !Start->IsA(APlayerStart::StaticClass()) )
				continue;
			const FVector Focus = Start->Location + FVector(0,0,24);
			const FVector Directions[4] = { FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0), FVector(0,-1,0) };
			FLOAT Clearance = 1.0f;
			for( INT Direction=0; Direction<4; Direction++ )
			{
				FCheckResult Hit;
				Level->SingleLineCheck( Hit, Target, Focus + Directions[Direction]*180.0f, Focus, TRACE_VisBlocking );
				Clearance = Min( Clearance, Hit.Time );
			}
			if( Clearance > BestClearance )
			{
				BestClearance = Clearance;
				BestStart = Start;
			}
		}
		if( BestStart && Level->FarMoveActor(Target,BestStart->Location,0,0) )
			ProofRotation = FRotator(0,0,0);
	}

	static INT AllAnimationProof = -1;
	if( AllAnimationProof < 0 )
		AllAnimationProof = GetFileAttributesA( "D:\\XboxAllAnimationProof.ini" ) != 0xFFFFFFFF;
	const INT StateCount = AllAnimationProof ? Target->Mesh->AnimSeqs.Num() : ARRAY_COUNT(GXboxSkeletalProofStates);
	if( StateCount <= 0 )
		return;
	const INT StateIndex = ProofStateIndex % StateCount;
	FXboxSkeletalProofState ExactState;
	appMemzero( &ExactState, sizeof(ExactState) );
	if( AllAnimationProof )
	{
		ExactState.Label = *Target->Mesh->AnimSeqs(StateIndex).Name;
		ExactState.Candidates[0] = ExactState.Label;
	}
	const FXboxSkeletalProofState& State = AllAnimationProof ? ExactState : GXboxSkeletalProofStates[StateIndex];
	FName SequenceName = NAME_None;
	const FString TargetClassName = Target->GetClass() ? Target->GetClass()->GetFullName() : FString(TEXT("None"));
	const FString TargetMeshName = Target->Mesh ? Target->Mesh->GetFullName() : FString(TEXT("None"));
	if( ProofTick == 0 )
		debugf( NAME_Log, TEXT("XSKELSTATE stage=sequence-begin class=%s"), *TargetClassName );
	const FMeshAnimSeq* Sequence = XboxFindSkeletalProofSequence( Target->Mesh, State, SequenceName );
	const FLOAT CycleSeconds = Sequence && Sequence->NumFrames > 1 && Sequence->Rate > 0.0f
		? Sequence->NumFrames / Sequence->Rate : 0.0f;
	if( ProofTick == 0 )
		debugf( NAME_Log, TEXT("XSKELSTATE stage=sequence-end found=%i"), Sequence ? 1 : 0 );

	Target->Rotation = ProofRotation;
	Target->Velocity = FVector(0,0,0);
	Target->Acceleration = FVector(0,0,0);
	// This is a render-only proof. Calling setPhysics(PHYS_None) performs floor
	// discovery and base changes that are unrelated to the pose being qualified.
	Target->Physics = PHYS_None;
	if( ProofTick == 0 )
		debugf( NAME_Log, TEXT("XSKELSTATE stage=pose-begin") );
	if( Sequence )
	{
		Target->AnimSequence = SequenceName;
		// Match PlayAnim/LoopAnim's Rate/NumFrames normalization using elapsed
		// simulation seconds. A fixed 120-tick sweep changes every clip's speed.
		// Match the sustained movement/idle families used by LoopAnim.
		// Jump, landing, hit, gesture and death clips are one-shot motions.
		const TCHAR* Name = *SequenceName;
		const UBOOL Loop = appStrnicmp(Name,TEXT("Run"),3)==0
			|| appStrnicmp(Name,TEXT("Walk"),4)==0 || appStrnicmp(Name,TEXT("Back"),4)==0
			|| appStrnicmp(Name,TEXT("Strafe"),6)==0 || appStrnicmp(Name,TEXT("Duck"),4)==0
			|| appStrnicmp(Name,TEXT("Swim"),4)==0 || appStrnicmp(Name,TEXT("Tread"),5)==0
			|| appStrnicmp(Name,TEXT("Breath"),6)==0 || appStrnicmp(Name,TEXT("Chat"),4)==0
			|| appStrnicmp(Name,TEXT("Still"),5)==0;
		Target->AnimFrame = XboxSkeletalProofFrame(StateElapsed,Sequence->NumFrames,Sequence->Rate,Loop);
		Target->AnimRate = 0.0f;
		Target->TweenRate = 0.0f;
		Target->bAnimLoop = Loop;
	}
	if( ProofTick == 0 )
		debugf( NAME_Log, TEXT("XSKELSTATE stage=pose-end") );

	if( StateIndex != LastStateIndex || (ProofTick % STATE_LOG_INTERVAL) == 0 )
	{
		debugf
		(
			NAME_Log,
			TEXT("XSKELSTATE tick=%i class=%s mesh=%s state=%s seq=%s frame=%.4f numframes=%i rate=%.2f found=%i elapsed=%.4f cycleSeconds=%.4f speed=1.0 loop=%i"),
			(INT)GTicks,
			*TargetClassName,
			*TargetMeshName,
			State.Label,
			Sequence ? *SequenceName : TEXT("None"),
			Sequence ? Target->AnimFrame : 0.0f,
			Sequence ? Sequence->NumFrames : 0,
			Sequence ? Sequence->Rate : 0.0f,
			Sequence ? 1 : 0,
			StateElapsed,
			CycleSeconds,
			Target->bAnimLoop
		);
		LastStateIndex = StateIndex;
	}
	ProofTick++;
	StateElapsed += Max(DeltaSeconds,0.0f);
	// Keep a useful inspection window without stretching long or short clips.
	if( StateElapsed >= Max(2.0f,CycleSeconds+0.5f) )
	{
		StateElapsed = 0.0f;
		ProofStateIndex++;
	}
}

static void XboxMemMark( const TCHAR* Label )
{
	guard(XboxMemMark);
	static DWORD LastAvail = 0;
	static DWORD LastHeapLive = 0;
	MEMORYSTATUS MemStatus;
	appMemzero( &MemStatus, sizeof(MemStatus) );
	MemStatus.dwLength = sizeof(MemStatus);
	GlobalMemoryStatus( &MemStatus );
	DWORD AvailKB = MemStatus.dwAvailPhys / 1024;
	DWORD HeapLiveKB = GXboxMallocLiveBytes / 1024;
	INT DeltaAvailKB = LastAvail ? (INT)AvailKB - (INT)LastAvail : 0;
	INT DeltaHeapLiveKB = LastHeapLive ? (INT)HeapLiveKB - (INT)LastHeapLive : 0;
	debugf
	(
		NAME_Init,
		TEXT("XMEM %s availKB=%u dAvailKB=%i heapLiveKB=%u dHeapKB=%i heapPeakKB=%u heapTotalKB=%u largestKB=%u largestTag=%s lastLargeKB=%u lastLargeTag=%s"),
		Label ? Label : TEXT("mark"),
		(unsigned)AvailKB,
		DeltaAvailKB,
		(unsigned)HeapLiveKB,
		DeltaHeapLiveKB,
		(unsigned)(GXboxMallocPeakBytes / 1024),
		(unsigned)(GXboxMallocTotalBytes / 1024),
		(unsigned)(GXboxMallocLargestBytes / 1024),
		GXboxMallocLargestTag,
		(unsigned)(GXboxMallocLastLargeBytes / 1024),
		GXboxMallocLastLargeTag
	);
	LastAvail = AvailKB;
	LastHeapLive = HeapLiveKB;
	unguard;
}

static DWORD XboxAvailPhysKB()
{
	MEMORYSTATUS MemStatus;
	appMemzero( &MemStatus, sizeof(MemStatus) );
	MemStatus.dwLength = sizeof(MemStatus);
	GlobalMemoryStatus( &MemStatus );
	return MemStatus.dwAvailPhys / 1024;
}

extern "C" UBOOL XboxEnsureConsoleClass( UViewport* Viewport, const TCHAR* ConsoleClassName, const char* Reason )
{
	guard(XboxEnsureConsoleClass);
	if( !Viewport || !ConsoleClassName || !ConsoleClassName[0] )
		return 0;

	UClass* ConsoleClass = UObject::StaticLoadClass( UConsole::StaticClass(), NULL, ConsoleClassName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
	if( !ConsoleClass )
	{
		debugf( NAME_Log, TEXT("Xbox: console switch failed reason=%s class=%s load=missing"), Reason ? appFromAnsi(Reason) : TEXT(""), ConsoleClassName );
		return 0;
	}

	if( Viewport->Console && Viewport->Console->GetClass() == ConsoleClass )
		return 1;

	UConsole* OldConsole = Viewport->Console;
	Viewport->Console = NULL;
	UConsole* NewConsole = ConstructObject<UConsole>( ConsoleClass );
	if( !NewConsole )
	{
		Viewport->Console = OldConsole;
		debugf( NAME_Log, TEXT("Xbox: console switch failed reason=%s class=%s construct=NULL"), Reason ? appFromAnsi(Reason) : TEXT(""), ConsoleClassName );
		return 0;
	}

	Viewport->Console = NewConsole;
	NewConsole->_Init( Viewport );
	if( OldConsole )
		delete OldConsole;

	debugf( NAME_Log, TEXT("Xbox: console switch reason=%s class=%s"), Reason ? appFromAnsi(Reason) : TEXT(""), ConsoleClassName );
	return 1;
	unguard;
}

static void XboxReleaseEntryLevel( ULevel*& EntryLevel, ULevel* ActiveLevel )
{
	guard(XboxReleaseEntryLevel);
	if( EntryLevel && EntryLevel != ActiveLevel )
	{
		debugf( NAME_Init, TEXT("Xbox: releasing retained Entry level for frontend memory headroom") );
		XboxMemMark( TEXT("XboxReleaseEntry pre") );
		UObject::ResetLoaders( EntryLevel->GetOuter(), 1, 0 );
		EntryLevel = NULL;
		UObject::CollectGarbage( RF_Native );
		XboxMemMark( TEXT("XboxReleaseEntry post") );
	}
	unguard;
}

static UBOOL GetXboxStartURL( TCHAR* OutURL, INT MaxLen )
{
	guard(GetXboxStartURL);

	OutURL[0] = 0;

	FString ConfigText;
	if( !appLoadFileToString( ConfigText, TEXT("D:\\XboxStartURL.ini"), GFileManager ) )
	{
		debugf( NAME_Init, TEXT("XboxStartURL: D:\\XboxStartURL.ini missing; using normal startup") );
		return 0;
	}

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
		while( End > Start && (End[-1]==' ' || End[-1]=='\t') )
			*--End = 0;

		if( Start[0] )
		{
			appStrncpy( OutURL, Start, MaxLen );
			OutURL[MaxLen-1] = 0;
			debugf( NAME_Init, TEXT("XboxStartURL: using [%s]"), OutURL );
			return 1;
		}
	}

	debugf( NAME_Init, TEXT("XboxStartURL: file empty; using normal startup") );
	return 0;

	unguard;
}

static void SanitizeXboxDefaultPlayerURLConfig()
{
	guard(SanitizeXboxDefaultPlayerURLConfig);
	if( !GConfig )
		return;

	const TCHAR* Class = GConfig->GetStr( TEXT("DefaultPlayer"), TEXT("Class"), TEXT("User.ini") );
	const TCHAR* Skin  = GConfig->GetStr( TEXT("DefaultPlayer"), TEXT("Skin"),  TEXT("User.ini") );
	const TCHAR* Face  = GConfig->GetStr( TEXT("DefaultPlayer"), TEXT("Face"),  TEXT("User.ini") );
	const TCHAR* Voice = GConfig->GetStr( TEXT("DefaultPlayer"), TEXT("Voice"), TEXT("User.ini") );
	const TCHAR* Team  = GConfig->GetStr( TEXT("DefaultPlayer"), TEXT("Team"),  TEXT("User.ini") );

	UBOOL bChanged = 0;
	UBOOL bKnownXboxClass =
		Class
	&&	(	appStricmp( Class, TEXT("Botpack.TMale1") ) == 0
		||	appStricmp( Class, TEXT("Botpack.TMale2") ) == 0
		||	appStricmp( Class, TEXT("Botpack.TFemale1") ) == 0
		||	appStricmp( Class, TEXT("Botpack.TFemale2") ) == 0
		||	appStricmp( Class, TEXT("Botpack.TBoss") ) == 0
		||	appStricmp( Class, TEXT("MultiMesh.TSkaarj") ) == 0
		||	appStricmp( Class, TEXT("MultiMesh.TNali") ) == 0
		||	appStricmp( Class, TEXT("MultiMesh.TCow") ) == 0
		||	appStricmp( Class, TEXT("HaloMasterChief.HaloMasterChief") ) == 0
		||	appStricmp( Class, TEXT("UTPS2Characters.DamienPS2") ) == 0
		||	appStricmp( Class, TEXT("UTPS2Characters.SkaarjBossPS2") ) == 0
		||	appStricmp( Class, TEXT("UTPS2Characters.WarbossPS2") ) == 0
		||	appStricmp( Class, TEXT("UTPS2Characters.XanPS2") ) == 0 );
	UBOOL bXboxFaceOptional =
		bKnownXboxClass
	&&	( appStricmp( Class, TEXT("Botpack.TBoss") ) == 0
		|| appStricmp( Class, TEXT("UTPS2Characters.DamienPS2") ) == 0
		|| appStricmp( Class, TEXT("UTPS2Characters.WarbossPS2") ) == 0
		|| appStricmp( Class, TEXT("UTPS2Characters.XanPS2") ) == 0 );

	if( !Class || !Class[0] || !bKnownXboxClass )
	{
		GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Class"), TEXT("Botpack.TMale2"), TEXT("User.ini") );
		GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Skin"), TEXT("SoldierSkins.blkt"), TEXT("User.ini") );
		GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Face"), TEXT("SoldierSkins.Othello"), TEXT("User.ini") );
		GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Voice"), TEXT("BotPack.VoiceMaleTwo"), TEXT("User.ini") );
		GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Team"), TEXT("255"), TEXT("User.ini") );
		bChanged = 1;
	}
	else
	{
		if( !Skin || !Skin[0] )
		{
			GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Skin"), TEXT("SoldierSkins.blkt"), TEXT("User.ini") );
			bChanged = 1;
		}
		if( (!Face || !Face[0]) && !bXboxFaceOptional )
		{
			GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Face"), TEXT("SoldierSkins.Othello"), TEXT("User.ini") );
			bChanged = 1;
		}
		if( !Voice || !Voice[0] )
		{
			GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Voice"), TEXT("BotPack.VoiceMaleTwo"), TEXT("User.ini") );
			bChanged = 1;
		}
		if( !Team || !Team[0] )
		{
			GConfig->SetString( TEXT("DefaultPlayer"), TEXT("Team"), TEXT("255"), TEXT("User.ini") );
			bChanged = 1;
		}
	}

	if( bChanged )
	{
		GConfig->Flush( 0, TEXT("User.ini") );
		debugf( NAME_Init, TEXT("Xbox: sanitized incomplete [DefaultPlayer] URL config") );
	}
	unguard;
}

static UBOOL XboxIsCityIntroURL( const FURL& URL )
{
	return URL.Map.Len()
	&&	(	appStricmp( *URL.Map, TEXT("CityIntro") ) == 0
		||	appStricmp( *URL.Map, TEXT("CityIntro.unr") ) == 0 );
}

static UBOOL XboxIsCityIntroLevel( ULevel* Level )
{
	return Level
	&&	(	XboxIsCityIntroURL( Level->URL )
		||	(	Level->GetLevelInfo()
			&&	Level->GetLevelInfo()->Game
			&&	Level->GetLevelInfo()->Game->GetClass()
			&&	appStricmp( Level->GetLevelInfo()->Game->GetClass()->GetName(), TEXT("UTIntro") ) == 0 ) );
}

static UBOOL XboxIsCityIntroTravelString( const FString& URL )
{
	if( URL==TEXT("?RESTART") )
		return 1;
	FURL Parsed( NULL, *URL, TRAVEL_Relative );
	return XboxIsCityIntroURL( Parsed );
}

static AInterpolationPoint* XboxFindCityIntroPathStart( ULevel* Level )
{
	AInterpolationPoint* IntroPathStart = NULL;
	if( !Level )
		return NULL;
	for( INT PathIndex=0; PathIndex<Level->Actors.Num(); PathIndex++ )
	{
		AInterpolationPoint* PathActor = Cast<AInterpolationPoint>( Level->Actors(PathIndex) );
		if( PathActor && PathActor->Position == 0 && (PathActor->Tag == FName(TEXT("Path")) || !IntroPathStart) )
			IntroPathStart = PathActor;
	}
	return IntroPathStart;
}

static UBOOL XboxRestartCityIntroFlyby( ULevel* Level, UViewport* Viewport )
{
	guard(XboxRestartCityIntroFlyby);
	if( !XboxIsCityIntroLevel( Level ) || !Viewport || !Viewport->Actor )
		return 0;

	AInterpolationPoint* IntroPathStart = XboxFindCityIntroPathStart( Level );
	if( !IntroPathStart )
	{
		debugf( NAME_Warning, TEXT("Xbox: CityIntro flythrough rewind failed; no interpolation start") );
		return 0;
	}

	AActor* FlyActor = Viewport->Actor;
	if( Viewport->Actor->ViewTarget && (Viewport->Actor->ViewTarget->bInterpolating || Cast<AInterpolationPoint>(Viewport->Actor->ViewTarget->Target)) )
		FlyActor = Viewport->Actor->ViewTarget;

	FCheckResult Hit;
	FlyActor->GetLevel()->MoveActor( FlyActor, IntroPathStart->Location - FlyActor->Location, IntroPathStart->Rotation, Hit );
	FlyActor->Target = IntroPathStart;
	FlyActor->PhysRate = 0.5f;
	FlyActor->PhysAlpha = 0.0f;
	FlyActor->bInterpolating = 1;
	FlyActor->setPhysics( PHYS_Interpolating );
	if( FlyActor->IsA(APawn::StaticClass()) )
		((APawn*)FlyActor)->ViewRotation = IntroPathStart->Rotation;
	Level->GetLevelInfo()->NextURL = TEXT("");
	Level->GetLevelInfo()->NextSwitchCountdown = 0.0f;
	debugf( NAME_Init, TEXT("Xbox: CityIntro flythrough rewound without reload actor=%s path=%s"), FlyActor->GetName(), IntroPathStart->GetName() );
	return 1;
	unguard;
}

static void XboxSetLoadActivity( UGameEngine* GameEngine, UBOOL bShow )
{
	if( bShow && !GXboxShowLoadActivity )
	{
		GXboxLoadActivityStep = 0;
		GXboxLoadActivityDrawCount = 0;
		GXboxLoadActivityStartTime = appSeconds();
		GXboxLoadActivityLastDrawTime = 0.0;
		debugf( NAME_Init, TEXT("XLOADANIM begin") );
		XboxMirrorLoadActivityLine( "XLOADANIM begin" );
	}
	else if( !bShow && GXboxShowLoadActivity )
	{
		DWORD ElapsedMS = (DWORD)Max<DOUBLE>( 0.0, (appSeconds() - GXboxLoadActivityStartTime) * 1000.0 );
		debugf( NAME_Init, TEXT("XLOADANIM end frames=%i elapsedMS=%u"), GXboxLoadActivityDrawCount, (unsigned)ElapsedMS );
		char Line[128];
		_snprintf( Line, sizeof(Line)-1, "XLOADANIM end frames=%i elapsedMS=%u", GXboxLoadActivityDrawCount, (unsigned)ElapsedMS );
		Line[sizeof(Line)-1] = 0;
		XboxMirrorLoadActivityLine( Line );
		if( GXboxLoadActivityEngine && GXboxLoadActivityEngine->Client && GXboxLoadActivityEngine->Client->Viewports.Num() )
		{
			UViewport* Viewport = GXboxLoadActivityEngine->Client->Viewports(0);
			if( Viewport )
				XboxRenderReleaseLoadingFrameBackground( Viewport->RenDev );
		}
	}
	GXboxShowLoadActivity = bShow;
	GXboxLoadActivityEngine = bShow ? GameEngine : NULL;
}

extern "C" void XboxPulseLoadingActivity()
{
	if( !GXboxShowLoadActivity || GXboxDrawingLoadActivity || !GXboxLoadActivityEngine )
		return;

	UGameEngine* GameEngine = GXboxLoadActivityEngine;
	if( !GameEngine || !GameEngine->Client || !GameEngine->Client->Viewports.Num() )
		return;

	UViewport* Viewport = GameEngine->Client->Viewports(0);
	if( !Viewport || !Viewport->RenDev )
		return;

	DOUBLE Now = appSeconds();
	if( GXboxLoadActivityLastDrawTime > 0.0 && Now - GXboxLoadActivityLastDrawTime < 0.10 )
		return;

	GXboxLoadActivityLastDrawTime = Now;
	GXboxLoadActivityStep = (GXboxLoadActivityStep + 1) & 7;
	GXboxLoadActivityDrawCount++;
	UBOOL bDrew = XboxRenderDrawLoadingFrame( Viewport->RenDev, GXboxLoadActivityStep, GXboxLoadActivityDrawCount );
	if( !bDrew && Viewport->Actor && Viewport->Canvas )
	{
		ULevel* ViewLevel = Viewport->Actor->GetLevel();
		ALevelInfo* LevelInfo = ViewLevel ? ViewLevel->GetLevelInfo() : NULL;
		if( LevelInfo )
		{
			BYTE SavedAction = LevelInfo->LevelAction;
			LevelInfo->LevelAction = LEVACT_Loading;
			GXboxDrawingLoadActivity = 1;
			GameEngine->PaintProgress();
			GXboxDrawingLoadActivity = 0;
			LevelInfo->LevelAction = SavedAction;
			bDrew = 1;
		}
	}

	if( GXboxLoadActivityDrawCount <= 16 || (GXboxLoadActivityDrawCount & 7) == 0 )
	{
		DWORD ElapsedMS = (DWORD)Max<DOUBLE>( 0.0, (appSeconds() - GXboxLoadActivityStartTime) * 1000.0 );
		debugf( NAME_Init, TEXT("XLOADANIM frame=%i step=%i elapsedMS=%u drew=%i"),
			GXboxLoadActivityDrawCount, GXboxLoadActivityStep, (unsigned)ElapsedMS, bDrew ? 1 : 0 );
		char Line[128];
		_snprintf( Line, sizeof(Line)-1, "XLOADANIM frame=%i step=%i elapsedMS=%u drew=%i",
			GXboxLoadActivityDrawCount, GXboxLoadActivityStep, (unsigned)ElapsedMS, bDrew ? 1 : 0 );
		Line[sizeof(Line)-1] = 0;
		XboxMirrorLoadActivityLine( Line );
	}
}

static void XboxDrawLoadingActivity( UViewport* Viewport )
{
	guard(XboxDrawLoadingActivity);
	if( !GXboxShowLoadActivity || GXboxSuppressLoadActivityDraw || !Viewport || !Viewport->Canvas || !Viewport->Canvas->Frame )
		return;

	FSceneNode* Frame = Viewport->Canvas->Frame;
	FLOAT W = Viewport->Canvas->ClipX;
	FLOAT H = Viewport->Canvas->ClipY;
	FLOAT CX = W * 0.5f;
	FLOAT CY = H - 28.0f;
	static const FLOAT Offsets[8][2] =
	{
		{  0.0f,-12.0f }, {  8.0f, -8.0f }, { 12.0f,  0.0f }, {  8.0f,  8.0f },
		{  0.0f, 12.0f }, { -8.0f,  8.0f }, {-12.0f,  0.0f }, { -8.0f, -8.0f }
	};
	for( INT i=0; i<8; i++ )
	{
		INT Age = (i - GXboxLoadActivityStep + 8) & 7;
		BYTE A = (BYTE)(70 + (7 - Age) * 22);
		BYTE R = (BYTE)(18 + (7 - Age) * 4);
		BYTE G = (BYTE)(80 + (7 - Age) * 18);
		BYTE B = (BYTE)(135 + (7 - Age) * 14);
		FLOAT X = CX + Offsets[i][0];
		FLOAT Y = CY + Offsets[i][1];
		XboxRenderDrawMenuRect( Frame, X - 3.0f, Y - 3.0f, X + 3.0f, Y + 3.0f, R, G, B, A );
	}
	unguard;
}

static UBOOL XboxLightingProofIsEnabled()
{
	if( !GXboxLightingProofInitialized )
	{
		GXboxLightingProofEnabled = GetFileAttributesA( "D:\\XboxLightingProof.ini" ) != 0xFFFFFFFF;
		GXboxLightingProofInitialized = 1;
	}
	return GXboxLightingProofEnabled;
}

static UBOOL XboxLightingProofSlotContains( AActor* Actor )
{
	for( INT i=0; i<GXboxLightingProofSlots.Num(); i++ )
		if( GXboxLightingProofSlots(i) == Actor )
			return 1;
	return 0;
}

static FLOAT XboxLightingProofMinDistSq( AActor* Actor )
{
	if( !Actor || !GXboxLightingProofSlots.Num() )
		return 0.0f;

	FLOAT Best = 1.0e30f;
	for( INT i=0; i<GXboxLightingProofSlots.Num(); i++ )
	{
		AActor* Other = GXboxLightingProofSlots(i);
		if( Other )
			Best = Min( Best, (Actor->Location - Other->Location).SizeSquared() );
	}
	return Best;
}

static void XboxLightingProofBuildSlots( ULevel* Level, INT DrawCount )
{
	guard(XboxLightingProofBuildSlots);
	GXboxLightingProofLevel = Level;
	GXboxLightingProofSlots.Empty();
	GXboxLightingProofCenter = FVector(0,0,0);
	GXboxLightingProofStartDraw = DrawCount;
	GXboxLightingProofLastSlot = INDEX_NONE;
	if( !Level || !Level->GetLevelInfo() )
		return;

	TArray<AActor*> Candidates;
	TArray<AActor*> Priority;
	FVector Center(0,0,0);
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AActor* Actor = Level->Actors(i);
		if( !Actor || Actor->bDeleteMe || !Actor->IsA(ANavigationPoint::StaticClass()) )
			continue;

		Candidates.AddItem( Actor );
		Center += Actor->Location;
		const TCHAR* ClassName = Actor->GetClass() ? Actor->GetClass()->GetName() : TEXT("");
		if( Actor->IsA(APlayerStart::StaticClass()) || appStrstr(ClassName, TEXT("Control")) )
			Priority.AddItem( Actor );
	}
	if( Candidates.Num() )
		Center /= Candidates.Num();
	GXboxLightingProofCenter = Center;

	enum { DesiredSlots = 8 };
	for( INT p=0; p<Priority.Num() && GXboxLightingProofSlots.Num()<DesiredSlots; p++ )
	{
		AActor* Actor = Priority(p);
		if( !XboxLightingProofSlotContains(Actor) )
			GXboxLightingProofSlots.AddItem( Actor );
	}

	while( Candidates.Num() && GXboxLightingProofSlots.Num()<DesiredSlots )
	{
		AActor* BestActor = NULL;
		FLOAT BestScore = -1.0f;
		for( INT c=0; c<Candidates.Num(); c++ )
		{
			AActor* Actor = Candidates(c);
			if( !Actor || XboxLightingProofSlotContains(Actor) )
				continue;
			const FLOAT Score = GXboxLightingProofSlots.Num()
				? XboxLightingProofMinDistSq( Actor )
				: (Actor->Location - Center).SizeSquared();
			if( Score > BestScore )
			{
				BestScore = Score;
				BestActor = Actor;
			}
		}
		if( !BestActor )
			break;
		GXboxLightingProofSlots.AddItem( BestActor );
	}

	char Line[256];
	_snprintf
	(
		Line,
		sizeof(Line)-1,
		"XLIGHTSETUP draw=%d map=%s slots=%d candidates=%d center=(%.1f,%.1f,%.1f)",
		DrawCount,
		Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "(none)",
		GXboxLightingProofSlots.Num(),
		Candidates.Num(),
		GXboxLightingProofCenter.X,
		GXboxLightingProofCenter.Y,
		GXboxLightingProofCenter.Z
	);
	Line[sizeof(Line)-1] = 0;
	XboxMirrorLoadActivityLine( Line );
	unguard;
}

static void XboxLightingProofApplyView
(
	UViewport* Viewport,
	INT DrawCount,
	AActor*& ViewActor,
	FVector& ViewLocation,
	FRotator& ViewRotation
)
{
	guard(XboxLightingProofApplyView);
	if( !XboxLightingProofIsEnabled() || !Viewport || !Viewport->Actor )
		return;

	ULevel* Level = Viewport->Actor->GetLevel();
	if( !Level )
		return;
	if( GXboxLightingProofLevel != Level || !GXboxLightingProofSlots.Num() )
		XboxLightingProofBuildSlots( Level, DrawCount );
	if( !GXboxLightingProofSlots.Num() )
		return;

	enum { SlotFrames = 90 };
	INT Slot = ((DrawCount - GXboxLightingProofStartDraw) / SlotFrames) % GXboxLightingProofSlots.Num();
	if( Slot < 0 )
		Slot = 0;
	AActor* Anchor = GXboxLightingProofSlots(Slot);
	if( !Anchor )
		return;

	APlayerPawn* Player = Cast<APlayerPawn>( Viewport->Actor );
	if( Player )
	{
		Player->ViewTarget = NULL;
		Player->bBehindView = 0;
		Player->DesiredFOV = 90.0f;
		Player->FovAngle = 90.0f;
		Player->bShowScores = 0;
		Player->bShowMenu = 0;
		Player->bSpecialMenu = 0;
		Player->Health = Max( Player->Health, 100 );
		Player->ReducedDamageType = FName(TEXT("All"));
	}

	FVector Focus = GXboxLightingProofCenter;
	if( (Focus - Anchor->Location).SizeSquared() < 4096.0f )
	{
		const INT OtherSlot = (Slot + 1) % GXboxLightingProofSlots.Num();
		if( GXboxLightingProofSlots(OtherSlot) )
			Focus = GXboxLightingProofSlots(OtherSlot)->Location;
	}
	const FLOAT ProofEyeHeight = Player ? Max( Player->BaseEyeHeight, 1.0f ) : 48.0f;
	FVector CameraLocation = Anchor->Location + FVector(0,0,ProofEyeHeight);
	FVector Aim = Focus - CameraLocation;
	if( Aim.SizeSquared() < 4096.0f )
		Aim = Anchor->Rotation.Vector();
	ViewActor = Viewport->Actor;
	ViewLocation = CameraLocation;
	ViewRotation = Aim.Rotation();
	if( Player )
	{
		Player->Velocity = FVector(0,0,0);
		Player->Acceleration = FVector(0,0,0);
		Player->MoveTimer = -1.0f;
		Level->FarMoveActor( Player, Anchor->Location, 0, 1 );
		ViewLocation = Player->Location + FVector(0,0,ProofEyeHeight);
		Aim = Focus - ViewLocation;
		if( Aim.SizeSquared() < 4096.0f )
			Aim = Anchor->Rotation.Vector();
		ViewRotation = Aim.Rotation();
		FCheckResult MoveHit(1.0f);
		Level->MoveActor( Player, FVector(0,0,0), ViewRotation, MoveHit, 0, 1, 0, 1 );
		Player->ViewRotation = ViewRotation;
		Player->DesiredFOV = 90.0f;
		Player->FovAngle = 90.0f;
		Player->EyeHeight = ProofEyeHeight;
	}

	if( Slot != GXboxLightingProofLastSlot || (DrawCount % 60) == 0 )
	{
		FCheckResult PointHit;
		const UBOOL bWouldClear = (Level && Level->Model)
			? !Level->Model->PointCheck( PointHit, NULL, ViewLocation, FVector(0,0,0), 0 )
			: 1;
		char Line[512];
		_snprintf
		(
			Line,
			sizeof(Line)-1,
			"XLIGHTCAM draw=%d slot=%d count=%d map=%s anchor=%s class=%s loc=(%.1f,%.1f,%.1f) rot=(%d,%d,%d) wouldClear=%d",
			DrawCount,
			Slot,
			GXboxLightingProofSlots.Num(),
			Level->URL.Map.Len() ? TCHAR_TO_ANSI(*Level->URL.Map) : "(none)",
			Anchor->GetName() ? TCHAR_TO_ANSI(Anchor->GetName()) : "(none)",
			Anchor->GetClass() ? TCHAR_TO_ANSI(Anchor->GetClass()->GetFullName()) : "(none)",
			ViewLocation.X,
			ViewLocation.Y,
			ViewLocation.Z,
			ViewRotation.Pitch,
			ViewRotation.Yaw,
			ViewRotation.Roll,
			bWouldClear ? 1 : 0
		);
		Line[sizeof(Line)-1] = 0;
		XboxMirrorLoadActivityLine( Line );
		GXboxLightingProofLastSlot = Slot;
	}
	unguard;
}

static void XboxStartFrontendMusic( UAudioSubsystem* Audio, const TCHAR* Tag )
{
	if( !Audio )
		return;

	debugf( NAME_Init, TEXT("Xbox: starting native CityIntro music stream (%s)"), Tag ? Tag : TEXT("frontend") );
	Audio->Exec( TEXT("XAUDIOSTARTNATIVE Uttitle 0") );
	XboxMemMark( Tag ? Tag : TEXT("Xbox frontend music") );
}

static void XboxUnloadNativeLevelMusicBulk( ULevel* Level, const TCHAR* Reason )
{
	guard(XboxUnloadNativeLevelMusicBulk);
	if( !Level || !GFileManager )
		return;

	ALevelInfo* Info = Level->GetLevelInfo();
	if( !Info || !Info->Song || !Info->Song->GetName() || !Info->Song->GetName()[0] )
		return;

	UMusic* Music = Info->Song;
	INT BulkBytes = Music->Data.Num();
	if( BulkBytes <= 0 )
		return;

	TCHAR NativePath[256];
	appSprintf( NativePath, TEXT("D:\\MusicXbox\\%s.wav"), Music->GetName() );
	INT NativeBytes = GFileManager->FileSize( NativePath );
	if( NativeBytes <= 0 )
	{
		debugf( NAME_Init, TEXT("Xbox: retaining UMusic bulk song=%s bulkKB=%d native=%s nativeBytes=%d reason=%s"),
			Music->GetName(), (BulkBytes + 1023) / 1024, NativePath, NativeBytes, Reason ? Reason : TEXT("") );
		return;
	}

	DWORD BeforeKB = XboxAvailPhysKB();
	Music->Data.Unload();
	DWORD AfterKB = XboxAvailPhysKB();
	debugf( NAME_Init, TEXT("Xbox: early native music bulk unload song=%s bulkKB=%d nativeKB=%d availBeforeKB=%u availAfterKB=%u reason=%s"),
		Music->GetName(), (BulkBytes + 1023) / 1024, (NativeBytes + 1023) / 1024,
		(unsigned)BeforeKB, (unsigned)AfterKB, Reason ? Reason : TEXT("") );
	XboxMemMark( TEXT("LoadMap native music early unload") );

	unguard;
}
#endif

/*-----------------------------------------------------------------------------
	cleanup!!
-----------------------------------------------------------------------------*/

void UGameEngine::PaintProgress()
{
	guard(PaintProgress);

	FVector LoadFog(0,.1,.25);
	FVector LoadScale(.2,.2,.2);
	UViewport* Viewport=Client->Viewports(0);
	Exchange(Viewport->Actor->FlashFog,LoadFog);
	Exchange(Viewport->Actor->FlashScale,LoadScale);
	Draw( Viewport );
	Exchange(Viewport->Actor->FlashFog,LoadFog);
	Exchange(Viewport->Actor->FlashScale,LoadScale);

	unguard;
}

INT UGameEngine::ChallengeResponse( INT Challenge )
{
	guard(UGameEngine::ChallengeResponse);
	return (Challenge*237) ^ (0x93fe92Ce) ^ (Challenge>>16) ^ (Challenge<<16);
	unguard;
}

void UGameEngine::UpdateConnectingMessage()
{
	guard(UGameEngine::UpdateConnectingMessage);
	if( GPendingLevel && Client && Client->Viewports.Num() )
	{
		APlayerPawn* Actor = Client->Viewports(0)->Actor;
		if( Actor->ProgressTimeOut<Actor->Level->TimeSeconds )
		{
			TCHAR Msg1[256], Msg2[256];
			if( GPendingLevel->DemoRecDriver )
			{
				appSprintf( Msg1, TEXT("") );
				appSprintf( Msg2, *GPendingLevel->URL.Map );
			}
			else
			{
				appSprintf( Msg1, LocalizeProgress("ConnectingText") );
				appSprintf( Msg2, LocalizeProgress("ConnectingURL"), *GPendingLevel->URL.Host, *GPendingLevel->URL.Map );
			}
			SetProgress( Msg1, Msg2, 60.0 );
		}
	}
	unguard;
}
void UGameEngine::BuildServerMasterMap( UNetDriver* NetDriver, ULevel* InLevel )
{
	guard(UGameEngine::BuildServerMasterMap);
	check(NetDriver);
	check(InLevel);
	BeginLoad();
	{
		// Init LinkerMap.
		check(InLevel->GetLinker());
		NetDriver->MasterMap->AddLinker( InLevel->GetLinker() );

		// Load server-required packages.
		for( INT i=0; i<ServerPackages.Num(); i++ )
		{
#if TARGET_XBOX
			if( appStricmp(*ServerPackages(i),TEXT("SoldierSkins"))==0
			||	appStricmp(*ServerPackages(i),TEXT("CommandoSkins"))==0
			||	appStricmp(*ServerPackages(i),TEXT("FCommandoSkins"))==0
			||	appStricmp(*ServerPackages(i),TEXT("SGirlSkins"))==0
			||	appStricmp(*ServerPackages(i),TEXT("BossSkins"))==0 )
			{
				debugf( TEXT("Xbox skipping cosmetic server package: %s"), *ServerPackages(i) );
				continue;
			}
#endif
			debugf( TEXT("Server Package: %s"), *ServerPackages(i) );
			ULinkerLoad* Linker = GetPackageLinker( NULL, *ServerPackages(i), LOAD_NoFail, NULL, NULL );
			if( NetDriver->MasterMap->AddLinker( Linker )==INDEX_NONE )
				debugf( TEXT("   (server-side only)") );
		}

		// Add GameInfo's package to map.
		check(InLevel->GetLevelInfo());
		check(InLevel->GetLevelInfo()->Game);
		check(InLevel->GetLevelInfo()->Game->GetClass()->GetLinker());
		NetDriver->MasterMap->AddLinker( InLevel->GetLevelInfo()->Game->GetClass()->GetLinker() );

		// Precompute linker info.
		NetDriver->MasterMap->Compute();
	}
	EndLoad();
	unguard;
}

/*-----------------------------------------------------------------------------
	Game init and exit.
-----------------------------------------------------------------------------*/

//
// Construct the game engine.
//
UGameEngine::UGameEngine()
: LastURL(TEXT(""))
, ServerActors( E_NoInit )
, ServerPackages( E_NoInit )
{}

//
// Class creator.
//
void UGameEngine::StaticConstructor()
{
	guard(UGameEngine::StaticConstructor);

	UArrayProperty* A = new(GetClass(),TEXT("ServerActors"),RF_Public)UArrayProperty( CPP_PROPERTY(ServerActors), TEXT("Settings"), CPF_Config );
	A->Inner = new(A,TEXT("StrProperty0"),RF_Public)UStrProperty;

	UArrayProperty* B = new(GetClass(),TEXT("ServerPackages"),RF_Public)UArrayProperty( CPP_PROPERTY(ServerPackages), TEXT("Settings"), CPF_Config );
	B->Inner = new(B,TEXT("StrProperty0"),RF_Public)UStrProperty;

	unguard;
}

//
// Initialize the game engine.
//
void UGameEngine::Init()
{
	guard(UGameEngine::Init);
	debugf( NAME_Init, TEXT("[GE] Init: enter") );
#if TARGET_XBOX
	XboxMemMark( TEXT("GE.Init enter") );
#endif
	check(sizeof(*this)==GetClass()->GetPropertiesSize());

	// Call base.
	debugf( NAME_Init, TEXT("[GE] Init: pre UEngine::Init") );
#if TARGET_XBOX
	XboxMemMark( TEXT("GE.Init pre UEngine::Init") );
#endif
	UEngine::Init();
	debugf( NAME_Init, TEXT("[GE] Init: post UEngine::Init") );
#if TARGET_XBOX
	XboxMemMark( TEXT("GE.Init post UEngine::Init") );
#endif

	// Init variables.
	GLevel = NULL;

	// Delete temporary files in cache.
	appCleanFileCache();

	// If not a dedicated server.
	if( GIsClient )
	{
		// Init client.
		debugf( NAME_Init, TEXT("[GE] Init: pre Client StaticLoadClass") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre Client StaticLoadClass") );
#endif
		UClass* ClientClass = StaticLoadClass( UClient::StaticClass(), NULL, TEXT("ini:Engine.Engine.ViewportManager"), NULL, LOAD_NoFail, NULL );
		debugf( NAME_Init, TEXT("[GE] Init: pre Client ConstructObject") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Client StaticLoadClass") );
#endif
		Client = ConstructObject<UClient>( ClientClass );
		debugf( NAME_Init, TEXT("[GE] Init: pre Client->Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Client ConstructObject") );
#endif
		Client->Init( this );
		debugf( NAME_Init, TEXT("[GE] Init: post Client->Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Client->Init") );
#endif

		// Init rendering.
		debugf( NAME_Init, TEXT("[GE] Init: pre Render StaticLoadClass") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre Render StaticLoadClass") );
#endif
		UClass* RenderClass = StaticLoadClass( URenderBase::StaticClass(), NULL, TEXT("ini:Engine.Engine.Render"), NULL, LOAD_NoFail, NULL );
		debugf( NAME_Init, TEXT("[GE] Init: pre Render ConstructObject") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Render StaticLoadClass") );
#endif
		Render = ConstructObject<URenderBase>( RenderClass );
		debugf( NAME_Init, TEXT("[GE] Init: pre Render->Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Render ConstructObject") );
#endif
		Render->Init( this );
		debugf( NAME_Init, TEXT("[GE] Init: post Render->Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Render->Init") );
#endif
	}

	// Load the entry level.
	FString Error;
	if( Client )
	{
#if TARGET_XBOX
		debugf( NAME_Init, TEXT("Xbox: skipping hidden Entry warm-up level for hardware memory headroom") );
		GEntry = NULL;
		XboxMemMark( TEXT("GE.Init skip LoadMap Entry") );
#else
		debugf( NAME_Init, TEXT("[GE] Init: pre LoadMap(Entry)") );
		if( !LoadMap( FURL(TEXT("Entry")), NULL, NULL, Error ) )
			appErrorf( LocalizeError("FailedBrowse"), TEXT("Entry"), *Error );
		debugf( NAME_Init, TEXT("[GE] Init: post LoadMap(Entry)") );
		Exchange( GLevel, GEntry );
		debugf( NAME_Init, TEXT("[GE] Init: post Exchange GLevel/GEntry") );
#endif
	}

	// Create default URL.
#if TARGET_XBOX
	SanitizeXboxDefaultPlayerURLConfig();
#endif
	FURL DefaultURL;
	DefaultURL.LoadURLConfig( TEXT("DefaultPlayer"), TEXT("User") );

	// Enter initial world.
	TCHAR Parm[4096]=TEXT("");
	const TCHAR* Tmp = appCmdLine();
	if
	(	!ParseToken( Tmp, Parm, ARRAY_COUNT(Parm), 0 )
	||	(appStricmp(Parm,TEXT("SERVER"))==0 && !ParseToken( Tmp, Parm, ARRAY_COUNT(Parm), 0 ))
	||	Parm[0]=='-' )
		appStrcpy( Parm, *FURL::DefaultLocalMap );
#if TARGET_XBOX
	TCHAR XboxStartURL[4096]=TEXT("");
	UBOOL bXboxOverrideStartupURL = GetXboxStartURL( XboxStartURL, ARRAY_COUNT(XboxStartURL) );
	if( bXboxOverrideStartupURL )
		appStrcpy( Parm, XboxStartURL );
	else if( appStricmp( Parm, *FURL::DefaultLocalMap ) == 0 )
	{
		DefaultURL.AddOption( TEXT("Game=Engine.GameInfo") );
		DefaultURL.AddOption( TEXT("Class=Engine.Spectator") );
		DefaultURL.AddOption( TEXT("Team=255") );
		debugf( NAME_Init, TEXT("Xbox: using lightweight frontend player URL for startup") );
	}
#endif
	FURL URL( &DefaultURL, Parm, TRAVEL_Partial );
	if( !URL.Valid )
		appErrorf( LocalizeError("InvalidUrl"), Parm );
	debugf( NAME_Init, TEXT("[GE] Init: pre Browse(%s)"), Parm );
#if TARGET_XBOX
	XboxMemMark( TEXT("GE.Init pre Browse startup") );
#endif
	UBOOL Success = Browse( URL, NULL, Error );
	debugf( NAME_Init, TEXT("[GE] Init: post Browse Success=%d"), Success );
#if TARGET_XBOX
	XboxMemMark( TEXT("GE.Init post Browse startup") );
	if( Success && !bXboxOverrideStartupURL && appStricmp( Parm, *FURL::DefaultLocalMap ) == 0 )
		XboxReleaseEntryLevel( GEntry, GLevel );
#endif

	// If waiting for a network connection, go into the starting level.
	if( !Success && Error==TEXT("") && appStricmp( Parm, *FURL::DefaultLocalMap )!=0 )
	{
		debugf( NAME_Init, TEXT("[GE] Init: pre Browse(DefaultLocalMap fallback)") );
		Success = Browse( FURL(&DefaultURL,*FURL::DefaultLocalMap,TRAVEL_Partial), NULL, Error );
		debugf( NAME_Init, TEXT("[GE] Init: post Browse(DefaultLocalMap) Success=%d"), Success );
	}

	// Handle failure.
	if( !Success )
		appErrorf( LocalizeError("FailedBrowse"), Parm, *Error );

	// Open initial Viewport.
	if( Client )
	{
		// Init input.!!Temporary
		debugf( NAME_Init, TEXT("[GE] Init: pre StaticInitInput") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre StaticInitInput") );
#endif
		UInput::StaticInitInput();
		debugf( NAME_Init, TEXT("[GE] Init: post StaticInitInput") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post StaticInitInput") );
#endif

		// Create viewport.
		debugf( NAME_Init, TEXT("[GE] Init: pre NewViewport") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre NewViewport") );
#endif
		UViewport* Viewport = Client->NewViewport( NAME_None );
		debugf( NAME_Init, TEXT("[GE] Init: post NewViewport ptr=0x%08X"), (DWORD)Viewport );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post NewViewport") );
#endif

		// Create console.
		debugf( NAME_Init, TEXT("[GE] Init: pre Console StaticLoadClass") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre Console StaticLoadClass") );
		const TCHAR* ConfigConsole = GConfig ? GConfig->GetStr( TEXT("Engine.Engine"), TEXT("Console"), NULL ) : TEXT("");
		if( appStricmp( ConfigConsole ? ConfigConsole : TEXT(""), TEXT("Engine.Console") ) != 0 )
		{
			debugf( NAME_Log, TEXT("Xbox: overriding Console '%s' -> Engine.Console"), ConfigConsole ? ConfigConsole : TEXT("") );
			if( GConfig )
				GConfig->SetString( TEXT("Engine.Engine"), TEXT("Console"), TEXT("Engine.Console"), NULL );
		}
#endif
		UClass* ConsoleClass = StaticLoadClass( UConsole::StaticClass(), NULL, TEXT("ini:Engine.Engine.Console"), NULL, LOAD_NoFail, NULL );
		debugf( NAME_Init, TEXT("[GE] Init: pre Console ConstructObject") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Console StaticLoadClass") );
#endif
		Viewport->Console = ConstructObject<UConsole>( ConsoleClass );
		debugf( NAME_Init, TEXT("[GE] Init: pre Console->_Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Console ConstructObject") );
#endif
		Viewport->Console->_Init( Viewport );
		debugf( NAME_Init, TEXT("[GE] Init: post Console->_Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Console Init") );
#endif

		// Spawn play actor.
		FString Error;
		debugf( NAME_Init, TEXT("[GE] Init: pre SpawnPlayActor") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre SpawnPlayActor") );
#endif
		if( !GLevel->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Error ) )
			appErrorf( TEXT("%s"), *Error );
		debugf( NAME_Init, TEXT("[GE] Init: post SpawnPlayActor") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post SpawnPlayActor") );
#endif
		debugf( NAME_Init, TEXT("[GE] Init: pre Viewport->Input->Init") );
		Viewport->Input->Init( Viewport );
		debugf( NAME_Init, TEXT("[GE] Init: post Viewport->Input->Init") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Viewport Input Init") );
#endif
		debugf( NAME_Init, TEXT("[GE] Init: pre Viewport->OpenWindow") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre Viewport OpenWindow") );
#endif
		Viewport->OpenWindow( 0, 0, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE );
		debugf( NAME_Init, TEXT("[GE] Init: post Viewport->OpenWindow") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post Viewport OpenWindow") );
#endif
		debugf( NAME_Init, TEXT("[GE] Init: pre DetailChange (RenDev=0x%08X)"), (DWORD)(Viewport ? Viewport->RenDev : NULL) );
		GLevel->DetailChange( Viewport->RenDev->HighDetailActors );
		debugf( NAME_Init, TEXT("[GE] Init: post DetailChange") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post DetailChange") );
#endif
		debugf( NAME_Init, TEXT("[GE] Init: pre InitAudio") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init pre InitAudio") );
#endif
		InitAudio();
		debugf( NAME_Init, TEXT("[GE] Init: post InitAudio") );
#if TARGET_XBOX
		XboxMemMark( TEXT("GE.Init post InitAudio") );
#endif
		if( Audio )
		{
			Audio->SetViewport( Viewport );
#if TARGET_XBOX
			if( XboxIsCityIntroURL( URL ) )
				XboxStartFrontendMusic( Audio, TEXT("GE.Init post CityIntro native music") );
#endif
		}
	}
	debugf( NAME_Init, TEXT("Game engine initialized") );

	unguard;
}

//
// Pre exit.
//
void UGameEngine::Exit()
{
	guard(UGameEngine::Exit);
	Super::Exit();

	// Exit net.
	if( GLevel->NetDriver )
	{
		delete GLevel->NetDriver;
		GLevel->NetDriver = NULL;
	}

	unguard;
}

//
// Game exit.
//
void UGameEngine::Destroy()
{
	guard(UGameEngine::Destroy);

	// Game exit.
	if( GPendingLevel )
		CancelPending();
	GLevel = NULL;
	debugf( NAME_Exit, TEXT("Game engine shut down") );

	Super::Destroy();
	unguard;
}

//
// Progress text.
//
void UGameEngine::SetProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds )
{
	guard(UGameEngine::SetProgress);
	if( Client && Client->Viewports.Num() )
	{
		APlayerPawn* Actor = Client->Viewports(0)->Actor;
		if( Seconds==-1.0 )
		{
			// Upgrade message.
			Actor->eventShowUpgradeMenu();
		}
		Actor->ProgressMessage[0] = Str1;
		Actor->ProgressColor[0].R = 255;
		Actor->ProgressColor[0].G = 255;
		Actor->ProgressColor[0].B = 255;

		Actor->ProgressMessage[1] = Str2;
		Actor->ProgressColor[1].R = 255;
		Actor->ProgressColor[1].G = 255;
		Actor->ProgressColor[1].B = 255;

		Actor->ProgressTimeOut    = Actor->Level->TimeSeconds + Seconds;
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Command line executor.
-----------------------------------------------------------------------------*/

//
// This always going to be the last exec handler in the chain. It
// handles passing the command to all other global handlers.
//
UBOOL UGameEngine::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UGameEngine::Exec);
	const TCHAR* Str=Cmd;
	if( ParseCommand( &Str, TEXT("OPEN") ) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), Str, 0, TRAVEL_Partial );
		else
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Partial), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Open failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("START") ) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), Str, 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Absolute), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Start failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("SERVERTRAVEL") ) && (GIsServer && !GIsClient) )
	{
		GLevel->GetLevelInfo()->eventServerTravel(Str,0);
		return 1;
	}
	else if( (GIsServer && !GIsClient) && ParseCommand( &Str, TEXT("SAY") ) )
	{
		GLevel->GetLevelInfo()->eventBroadcastMessage(Str,1,NAME_None);
		return 1;
	}
	else if( ParseCommand(&Str, TEXT("DISCONNECT")) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), TEXT("?failed"), 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(&LastURL,TEXT("?failed"),TRAVEL_Absolute), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Disconnect failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand(&Str, TEXT("RECONNECT")) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), *LastURL.String(), 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(LastURL), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Reconnect failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GETCURRENTTICKRATE") ) )
	{
		Ar.Logf( TEXT("%f"), CurrentTickRate );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GETMAXTICKRATE") ) )
	{
		Ar.Logf( TEXT("%f"), GetMaxTickRate() );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GSPYLITE") ) )
	{
		FString Error;
		appLaunchURL( TEXT("GSpyLite.exe"), TEXT(""), &Error );
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("SAVEGAME")) )
	{
		if( appIsDigit(Str[0]) )
			SaveGame( appAtoi(Str) );
		return 1;
	}
	else if( ParseCommand( &Cmd, TEXT("CANCEL") ) )
	{
		if( GPendingLevel )
			SetProgress( LocalizeProgress("CancelledConnect"), TEXT(""), 2.0 );
		else
			SetProgress( TEXT(""), TEXT(""), 0.0 );
		CancelPending();
		return 1;
	}
	else if( GLevel && GLevel->Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( GLevel && GLevel->GetLevelInfo()->Game && GLevel->GetLevelInfo()->Game->ScriptConsoleExec(Cmd,Ar,NULL) )
	{
		return 1;
	}
	else if( UEngine::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Serialization.
-----------------------------------------------------------------------------*/

//
// Serializer.
//
void UGameEngine::Serialize( FArchive& Ar )
{
	guard(UGameEngine::Serialize);
	Super::Serialize( Ar );

	Ar << GLevel << GEntry << GPendingLevel;

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Game entering.
-----------------------------------------------------------------------------*/

//
// Cancel pending level.
//
void UGameEngine::CancelPending()
{
	guard(UGameEngine::CancelPending);
	if( GPendingLevel )
	{
#if TARGET_XBOX
		if( Audio ) Audio->Exec( TEXT("XAUDIOSETLOADING 0") );
#endif
		delete GPendingLevel;
		GPendingLevel = NULL;
	}
	unguard;
}

//
// Match Viewports to actors.
//
static void MatchViewportsToActors( UClient* Client, ULevel* Level, const FURL& URL )
{
	guard(MatchViewportsToActors);
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		FString Error;
		UViewport* Viewport = Client->Viewports(i);
		debugf( NAME_Log, TEXT("Spawning new actor for Viewport %s"), Viewport->GetName() );
		if( !Level->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Error ) )
			appErrorf( TEXT("%s"), *Error );
	}
	unguardf(( TEXT("(%s)"), *Level->URL.Map ));
}

//
// Browse to a specified URL, relative to the current one.
//
UBOOL UGameEngine::Browse( FURL URL, const TMap<FString,FString>* TravelInfo, FString& Error )
{
	guard(UGameEngine::Browse);
	Error = TEXT("");
	const TCHAR* Option;

	// Convert .unreal link files.
	const TCHAR* LinkStr = TEXT(".unreal");//!!
	if( appStrstr(*URL.Map,LinkStr)-*URL.Map==appStrlen(*URL.Map)-appStrlen(LinkStr) )
	{
		debugf( TEXT("Link: %s"), *URL.Map );
		FString NewUrlString;
		if( GConfig->GetString( TEXT("Link")/*!!*/, TEXT("Server"), NewUrlString, *URL.Map ) )
		{
			// Go to link.
			URL = FURL( NULL, *NewUrlString, TRAVEL_Absolute );//!!
		}
		else
		{
			// Invalid link.
			guard(InvalidLink);
			Error = FString::Printf( LocalizeError("InvalidLink"), *URL.Map );
			unguard;
			return 0;
		}
	}

	// Crack the URL.
	debugf( TEXT("Browse: %s"), *URL.String() );

#if TARGET_XBOX
	if( GLevel && XboxIsCityIntroLevel( GLevel ) && XboxIsCityIntroURL( URL ) )
	{
		debugf( NAME_Init, TEXT("Xbox: intercepting CityIntro Browse loop %s"), *URL.String() );
		if( GPendingLevel )
		{
			debugf( NAME_Log, TEXT("Xbox: cancelling pending network travel before CityIntro rewind") );
			CancelPending();
		}
		if( Client && Client->Viewports.Num() )
			Client->Viewports(0)->TravelURL = TEXT("");
		if( XboxRestartCityIntroFlyby( GLevel, Client && Client->Viewports.Num() ? Client->Viewports(0) : NULL ) )
			return 1;
	}
#endif

	// Handle it.
	if( !URL.Valid )
	{
		// Unknown URL.
		guard(UnknownURL);
		Error = FString::Printf( LocalizeError("InvalidUrl"), *URL.String() );
		unguard;
		return 0;
	}
	else if( URL.HasOption(TEXT("failed")) || URL.HasOption(TEXT("entry")) )
	{
		// Handle failure URL.
#if TARGET_XBOX
		if( !GEntry )
		{
			Error = TEXT("Entry level was released on Xbox");
			debugf( NAME_Log, TEXT("Xbox: abort-to-entry requested after Entry release") );
			return 0;
		}
#endif
		guard(FailedURL);
		debugf( NAME_Log, LocalizeError("AbortToEntry") );
		if( GLevel && GLevel!=GEntry )
		{
			if( GLevel->BrushTracker )
			{
				delete GLevel->BrushTracker;
				GLevel->BrushTracker = NULL;
			}
			ResetLoaders( GLevel->GetOuter(), 1, 0 );
		}
		NotifyLevelChange();
#if TARGET_XBOX
		// UC2004 releases connection-owned XNet addresses before unregistering the
		// session key.  The stock failure path otherwise abandons the old level's
		// driver for GC, which can reverse that order and leave a stale association.
		if( GLevel && GLevel!=GEntry && GLevel->NetDriver )
		{
			delete GLevel->NetDriver;
			GLevel->NetDriver = NULL;
		}
		XboxSystemLinkAbortTravelCleanup( URL.HasOption(TEXT("failed")) ? "Browse failed URL post net shutdown" : "Browse entry URL post net shutdown" );
#endif
		GLevel = GEntry;
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
		check(Client && Client->Viewports.Num());
		MatchViewportsToActors( Client, GLevel, URL );
		if( Audio )
			Audio->SetViewport( Audio->GetViewport() );
		//CollectGarbage( RF_Native ); // Causes texture corruption unless you flush.
		if( URL.HasOption(TEXT("failed")) )
		{
			if( !GPendingLevel )
				SetProgress( LocalizeError("ConnectionFailed"), TEXT(""), 6.0 );
		}
		unguard;
		return 1;
	}
	else if( URL.HasOption(TEXT("pop")) )
	{
		// Pop the hub.
		guard(PopURL);
		if( GLevel && GLevel->GetLevelInfo()->HubStackLevel>0 )
		{
			TCHAR Filename[256], SavedPortal[256];
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, GLevel->GetLevelInfo()->HubStackLevel-1 );
			appStrcpy( SavedPortal, *URL.Portal );
			URL = FURL( &URL, Filename, TRAVEL_Partial );
			URL.Portal = SavedPortal;
		}
		else return 0;
		unguard;
	}
	else if( URL.HasOption(TEXT("restart")) )
	{
		// Handle restarting.
		guard(RestartURL);
		URL = LastURL;
		unguard;
	}
	else if( (Option=URL.GetOption(TEXT("load="),NULL))!=NULL )
	{
		// Handle loadgame.
		guard(LoadURL);
		FString Error, Temp=FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Save%i.usa?load"), *GSys->SavePath, appAtoi(Option) );
		if( LoadMap(FURL(&LastURL,*Temp,TRAVEL_Partial),NULL,NULL,Error) )
		{
			// Copy the hub stack.
			for( INT i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
			{
				TCHAR Src[256], Dest[256];//!!
				appSprintf( Src, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, appAtoi(Option), i );
				appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i );
				GFileManager->Copy( Src, Dest );
			}
			while( 1 )
			{
				Temp = FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i++ );
				if( GFileManager->FileSize(*Temp)<=0 )
					break;
				GFileManager->Delete( *Temp );
			}
			LastURL = GLevel->URL;
			return 1;
		}
		else return 0;
		unguard;
	}

	// Handle normal URL's.
	if( URL.IsLocalInternal() )
	{
		// Local map file.
		guard(LocalMapURL);
		return LoadMap( URL, NULL, TravelInfo, Error )!=NULL;
		unguard;
	}
	else if( URL.IsInternal() && GIsClient )
	{
		// Network URL.
		guard(NetworkURL);
		if( GPendingLevel )
			CancelPending();
		GPendingLevel = new UNetPendingLevel( this, URL );
		if( !GPendingLevel->NetDriver )
		{
			SetProgress( TEXT("Networking Failed"), *GPendingLevel->Error, 6.0 );
			delete GPendingLevel;
			GPendingLevel = NULL;
		}
		return 0;
		unguard;
	}
	else if( URL.IsInternal() )
	{
		// Invalid.
		guard(InvalidURL);
		Error = LocalizeError("ServerOpen");
		unguard;
		return 0;
	}
	else
	{
		// External URL.
		guard(ExternalURL);
		appLaunchURL( *URL.String(), TEXT(""), &Error );
		unguard;
		return 0;
	}
	unguard;
}

//
// Notify that level is changing
//
void UGameEngine::NotifyLevelChange()
{
	guard(UGameEngine::NotifyLevelChange);
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->Console )
		Client->Viewports(0)->Console->eventNotifyLevelChange();
	unguard;	
}

//
// Load a map.
//
#if TARGET_XBOX
// Loading can paint the old viewport and run script notifications. Keep audio
// blocked for the entire load, including early failures, rather than just
// stopping the buffers once and allowing those callbacks to restart them.
struct FXboxLoadAudioSilence
{
    UAudioSubsystem* Audio;
    FXboxLoadAudioSilence( UAudioSubsystem* InAudio ) : Audio(InAudio)
    {
        if( Audio ) Audio->Exec( TEXT("XAUDIOSETLOADING 1") );
    }
    ~FXboxLoadAudioSilence()
    {
        if( Audio ) Audio->Exec( TEXT("XAUDIOSETLOADING 0") );
    }
};
#endif
ULevel* UGameEngine::LoadMap( const FURL& URL, UPendingLevel* Pending, const TMap<FString,FString>* TravelInfo, FString& Error )
{
	guard(UGameEngine::LoadMap);
#if TARGET_XBOX
	FXboxLoadAudioSilence LoadAudioSilence( Audio );
#endif
	Error = TEXT("");
	debugf( NAME_Log, TEXT("LoadMap: %s"), *URL.String() );
#if TARGET_XBOX
	XboxSetLoadActivity( this, !XboxIsCityIntroURL( URL ) );
	XboxMemMark( *FString::Printf( TEXT("LoadMap enter %s"), *URL.String() ) );
#endif
	GInitRunaway();

	// Remember current level's stack level.
	INT SavedHubStackLevel = GLevel ? GLevel->GetLevelInfo()->HubStackLevel : 0;

	// Display loading screen.
	guard(LoadingScreen);
	if( Client && Client->Viewports.Num() && GLevel )
	{
		GLevel->GetLevelInfo()->LevelAction = LEVACT_Loading;
		GLevel->GetLevelInfo()->Pauser = TEXT("");
		APlayerPawn* PP = Client->Viewports(0)->Actor;
		if( PP )
			PP->bShowMenu = 0;
#if TARGET_XBOX
		UViewport* XboxLoadingViewport = Client->Viewports(0);
		if( GXboxShowLoadActivity && XboxLoadingViewport && XboxLoadingViewport->RenDev )
		{
			GXboxSuppressLoadActivityDraw = 1;
			XboxRenderRequestLoadingFrameBackground( XboxLoadingViewport->RenDev );
		}
#endif
		PaintProgress();
#if TARGET_XBOX
		GXboxSuppressLoadActivityDraw = 0;
#endif
		if( Audio )
			Audio->SetViewport( Audio->GetViewport() );
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post LoadingScreen") );
#endif

	// Get network package map.
	UPackageMap* PackageMap = NULL;
	if( Pending )
		PackageMap = Pending->GetDriver()->ServerConnection->PackageMap;

	// Verify that we can load all packages we need.
	UObject* MapParent = NULL;
	guard(VerifyPackages);
	try
	{
#if TARGET_XBOX
		const UBOOL bXboxSkipVerifyPackages =
			!Pending;
		if( bXboxSkipVerifyPackages )
		{
			debugf( NAME_Log, TEXT("Xbox: skipping local package verify pass for memory headroom map=%s"), *URL.Map );
		}
		else
#endif
		{
		BeginLoad();
		if( Pending )
		{
			// Verify that we can load everything needed for client in this network level.
			for( INT i=0; i<PackageMap->List.Num(); i++ )
				PackageMap->List(i).Linker = GetPackageLinker
				(
					PackageMap->List(i).Parent,
					NULL,
					LOAD_Verify | LOAD_Throw | LOAD_NoWarn | LOAD_NoVerify,
					NULL,
					&PackageMap->List(i).Guid
				);
			for( i=0; i<PackageMap->List.Num(); i++ )
				VerifyLinker( PackageMap->List(i).Linker );
			if( PackageMap->List.Num() )
				MapParent = PackageMap->List(0).Parent;
		}
		LoadObject<ULevel>( MapParent, TEXT("MyLevel"), *URL.Map, LOAD_Verify | LOAD_Throw | LOAD_NoWarn, NULL );
		EndLoad();
		}
#if TARGET_XBOX
		XboxMemMark( TEXT("LoadMap post VerifyPackages") );
#endif

#if DEMOVERSION
		// If we area demo, prevent third party maps from being loaded.
		if( !Pending || !Pending->DemoRecDriver )
		{
			FString FileName(FString(TEXT("../Maps/"))+URL.Map);
			if( FileName.Right(4).Caps() != TEXT(".UNR"))
				FileName = FileName + TEXT(".unr");
			INT FileSize = GFileManager->FileSize( *FileName );
			debugf(TEXT("Looking for file: %s %d"), *FileName, FileSize);
			if( //FileSize != 0 &&
				( FileName.Caps() != TEXT("../MAPS/DM-TURBINEDEMO.UNR")	|| FileSize != 2135105 ) &&
				( FileName.Caps() != TEXT("../MAPS/DM-PHOBOSDEMO.UNR")	|| FileSize != 1618994 ) &&
				( FileName.Caps() != TEXT("../MAPS/DM-MORPHEUSDEMO.UNR")|| FileSize != 1193759 ) &&
				( FileName.Caps() != TEXT("../MAPS/DM-TEMPESTDEMO.UNR")	|| FileSize != 2152238 ) &&
				( FileName.Caps() != TEXT("../MAPS/CTF-CORETDEMO.UNR")	|| FileSize != 3498978 ) &&
				( FileName.Caps() != TEXT("../MAPS/DOM-SESMARDEMO.UNR")	|| FileSize != 2155658 ) &&
				( FileName.Caps() != TEXT("../MAPS/ENTRY.UNR")			|| FileSize != 34822 ) &&
				( FileName.Caps() != TEXT("../MAPS/UT-LOGO-MAP.UNR")	|| FileSize != 34884 ) )
			{
				Error = TEXT("Sorry, only the retail version of UT can load third party maps.");
				SetProgress( LocalizeError(TEXT("UrlFailed"),TEXT("Core")), *Error, 6.0 );
				return NULL;
			}
		}
#endif
	}
	catch( TCHAR* CatchError )
	{
		// Safely failed loading.
		EndLoad();
#if TARGET_XBOX
		XboxMemMark( TEXT("LoadMap VerifyPackages catch") );
#endif
		Error = CatchError;
		SetProgress( LocalizeError(TEXT("UrlFailed"),TEXT("Core")), CatchError, 6.0 );
#if TARGET_XBOX
		XboxSetLoadActivity( this, 0 );
#endif
		return NULL;
	}
	unguard;

	// Notify of the level change, before we dissociate Viewport actors
	guard(NotifyLevelChange);
	if( GLevel )
		NotifyLevelChange();
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post NotifyLevelChange") );
#endif

	// Dissociate Viewport actors.
	guard(DissociateViewports);
	if( Client )
	{
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			if( !Client->Viewports(i) || !Client->Viewports(i)->Actor )
				continue;

			APlayerPawn* Actor          = Client->Viewports(i)->Actor;
			ULevel*      Level          = Actor->GetLevel();
			Actor->Player               = NULL;
			Client->Viewports(i)->Actor = NULL;
			if( Level )
				Level->DestroyActor( Actor );
		}
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post DissociateViewports") );
	UBOOL bXboxHadOldLevel = (GLevel != NULL);
#endif

	// Clean up game state.
	guard(ExitLevel);
	if( GLevel )
	{
		// Shut down.
		ResetLoaders( GLevel->GetOuter(), 1, 0 );
		if( GLevel->BrushTracker )
		{
			delete GLevel->BrushTracker;
			GLevel->BrushTracker = NULL;
		}
		if( GLevel->NetDriver )
		{
#if TARGET_XBOX
			// UC2004 explicitly saves the System Link host/session across server
			// travel.  A LAN-to-LAN transition is the equivalent signal in UT99;
			// leaving LAN gameplay still performs the ordered final cleanup below.
			const UBOOL bPreserveXboxSystemLinkSession = GLevel->URL.HasOption(TEXT("LAN")) && URL.HasOption(TEXT("LAN"));
#endif
			delete GLevel->NetDriver;
			GLevel->NetDriver = NULL;
#if TARGET_XBOX
			if( bPreserveXboxSystemLinkSession )
				debugf( NAME_Log, TEXT("XNET preserved System Link session across LAN travel old=%s new=%s"), *GLevel->URL.String(), *URL.String() );
			else
				XboxSystemLinkAbortTravelCleanup( "LoadMap post net shutdown" );
#endif
		}
		if( GLevel->DemoRecDriver )
		{
			delete GLevel->DemoRecDriver;
			GLevel->DemoRecDriver = NULL;
		}
		if( URL.HasOption(TEXT("push")) )
		{
			// Save the current level minus players actors.
			GLevel->CleanupDestroyed( 1 );
			TCHAR Filename[256];
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, SavedHubStackLevel );
			SavePackage( GLevel->GetOuter(), GLevel, 0, Filename, GLog );
		}
		GLevel = NULL;
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post ExitLevel") );
	if( bXboxHadOldLevel && !Pending )
	{
		XboxMemMark( TEXT("LoadMap pre LoadLevel cleanup") );
		Flush(0);
		UObject::CollectGarbage( RF_Native );
		XboxMemMark( TEXT("LoadMap post LoadLevel cleanup") );
	}
#endif

	// Load the level and all objects under it, using the proper Guid.
	guard(LoadLevel);
	GLevel = LoadObject<ULevel>( MapParent, TEXT("MyLevel"), *URL.Map, LOAD_NoFail, NULL );
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post LoadLevel") );
#endif

	// If pending network level.
	if( Pending )
	{
		// If playing this network level alone, ditch the pending level.
		if( Pending && Pending->LonePlayer )
			Pending = NULL;

		// Setup network package info.
		PackageMap->Compute();
		for( INT i=0; i<PackageMap->List.Num(); i++ )
			if( PackageMap->List(i).LocalGeneration!=PackageMap->List(i).RemoteGeneration )
				Pending->NetDriver->ServerConnection->Logf( TEXT("HAVE GUID=%s GEN=%i"), PackageMap->List(i).Guid.String(), PackageMap->List(i).LocalGeneration );
	}

	// Verify classes.
	guard(VerifyClasses);
	VERIFY_CLASS_OFFSET( A, Actor,       Owner         );
	VERIFY_CLASS_OFFSET( A, Actor,       TimerCounter  );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  Player        );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  MaxStepHeight );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  SmoothMouseX );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  ProgressMessage );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  ngWorldSecret );
	unguard;

	// Get LevelInfo.
	check(GLevel);
	ALevelInfo* Info = GLevel->GetLevelInfo();
	Info->ComputerName = appComputerName();
#if TARGET_XBOX
	XboxUnloadNativeLevelMusicBulk( GLevel, TEXT("LoadMap post GetLevelInfo") );
#endif

	// Handle pushing.
	guard(ProcessHubStack);
	Info->HubStackLevel
	=	URL.HasOption(TEXT("load")) ? Info->HubStackLevel
	:	URL.HasOption(TEXT("push")) ? SavedHubStackLevel+1
	:	URL.HasOption(TEXT("pop" )) ? Max(SavedHubStackLevel-1,0)
	:	URL.HasOption(TEXT("peer")) ? SavedHubStackLevel
	:	                              0;
	unguard;

	// Handle pending level.
	guard(ActivatePending);
	if( Pending )
	{
		check(Pending==GPendingLevel);

		// Hook network driver up to level.
		GLevel->NetDriver = Pending->NetDriver;
		if( GLevel->NetDriver )
			GLevel->NetDriver->Notify = GLevel;

		// Hook demo playback driver to level
		GLevel->DemoRecDriver = Pending->DemoRecDriver;
		if( GLevel->DemoRecDriver )
			GLevel->DemoRecDriver->Notify = GLevel;

		// Setup level.
		GLevel->GetLevelInfo()->NetMode = NM_Client;
	}
	else check(!GLevel->NetDriver);
	unguard;

	// Set level info.
	guard(InitLevel);
	if( !URL.GetOption(TEXT("load"),NULL) )
		GLevel->URL = URL;
	Info->EngineVersion = FString::Printf( TEXT("%i"), ENGINE_VERSION );
	Info->MinNetVersion = FString::Printf( TEXT("%i"), ENGINE_MIN_NET_VERSION );
	GLevel->Engine = this;
	if( TravelInfo )
		GLevel->TravelInfo = *TravelInfo;
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post InitLevel") );
#endif

	// Purge unused objects and flush caches.
	guard(Cleanup);
	if( appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))!=0 )
	{
		Flush(0);
		{for( TObjectIterator<AActor> It; It; ++It )
			if( It->IsIn(GLevel->GetOuter()) )
				It->SetFlags( RF_EliminateObject );}
		{for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->ClearFlags( RF_EliminateObject );}
		CollectGarbage( RF_Native );
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post CleanupGC") );
#endif

	// Init collision.
	GLevel->SetActorCollision( 1 );

	// Setup zone distance table for sound damping. Fast enough: Approx 3 msec.
	guard(SetupZoneTable);
	QWORD OldConvConn[64];
	QWORD ConvConn[64];
	for( INT i=0; i<64; i++ )
	{
		for( INT j=0; j<64; j++ )
		{
			OldConvConn[i] = GLevel->Model->Zones[i].Connectivity;
			if( i == j )
				GLevel->ZoneDist[i][j] = 0;
			else
				GLevel->ZoneDist[i][j] = 255;
		}
	}
	for( i=1; i<64; i++ )
	{
		for( INT j=0; j<64; j++ )
			for( INT k=0; k<64; k++ )
				if( (GLevel->ZoneDist[j][k] > i) && ((OldConvConn[j] & ((QWORD)1 << k)) != 0) )
					GLevel->ZoneDist[j][k] = i;
		for( j=0; j<64; j++ )
			ConvConn[j] = 0;
		for( j=0; j<64; j++ )
			for( INT k=0; k<64; k++ )
				if( (OldConvConn[j] & ((QWORD)1 << k)) != 0 )
					ConvConn[j] = ConvConn[j] | OldConvConn[k];
	for( j=0; j<64; j++ )
			OldConvConn[j] = ConvConn[j];
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post SetupZoneTable") );
#endif

	// Update the LevelInfo's time.
	GLevel->UpdateTime(Info);

	// Init the game info.
	TCHAR Options[1024]=TEXT("");
	TCHAR GameClassName[256]=TEXT("");
	FString Error=TEXT("");
	guard(InitGameInfo);
	for( INT i=0; i<URL.Op.Num(); i++ )
	{
		appStrcat( Options, TEXT("?") );
		appStrcat( Options, *URL.Op(i) );
		Parse( *URL.Op(i), TEXT("GAME="), GameClassName, ARRAY_COUNT(GameClassName) );
	}
	if( GLevel->IsServer() && !Info->Game )
	{
		// Get the GameInfo class.
		UClass* GameClass=NULL;
		if( !GameClassName[0] )
		{
			GameClass=Info->DefaultGameType;
			if( !GameClass )
				GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, Client ? TEXT("ini:Engine.Engine.DefaultGame") : TEXT("ini:Engine.Engine.DefaultServerGame"), NULL, LOAD_NoFail, PackageMap );
		}
		else GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, GameClassName, NULL, LOAD_NoFail, PackageMap );

		// Spawn the GameInfo.
		debugf( NAME_Log, TEXT("Game class is '%s'"), GameClass->GetName() );
		Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
		check(Info->Game!=NULL);
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post InitGameInfo") );
#endif

	// Listen for clients.
	guard(Listen);
	if( !Client || URL.HasOption(TEXT("Listen")) )
	{
		if( GPendingLevel )
		{
			guard(CancelPendingForListen);
			check(!Pending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
		FString Error;
		if( !GLevel->Listen( Error ) )
			appErrorf( LocalizeError("ServerListen"), *Error );
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post Listen") );
#endif

	// Init detail.
	Info->bHighDetailMode = 1;
	if
	(	Client
	&&	Client->Viewports.Num()
	&&	Client->Viewports(0)->RenDev
	&&	!Client->Viewports(0)->RenDev->HighDetailActors )
		Info->bHighDetailMode = 0;

	// Init level gameplay info.
	guard(BeginPlay);
	GLevel->iFirstDynamicActor = 0;
	if( !Info->bBegunPlay )
	{
		// Lock the level.
		debugf( NAME_Log, TEXT("Bringing %s up for play (%i)..."), GLevel->GetFullName(), appRound(GetMaxTickRate()) );
		GLevel->TimeSeconds = 0;
		GLevel->GetLevelInfo()->TimeSeconds = 0;

		// Init touching actors.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				for( INT j=0; j<ARRAY_COUNT(GLevel->Actors(i)->Touching); j++ )
					GLevel->Actors(i)->Touching[j] = NULL;

		// Kill off actors that aren't interesting to the client.
		if( !GLevel->IsServer() )
		{
			for( INT i=0; i<GLevel->Actors.Num(); i++ )
			{
				AActor* Actor = GLevel->Actors(i);
				if( Actor )
				{
					if( Actor->bStatic || Actor->bNoDelete )
						Exchange( Actor->Role, Actor->RemoteRole );
					else
						GLevel->DestroyActor( Actor );
				}
			}
		}

		// Init scripting.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->InitExecution();

		// Enable actor script calls.
		Info->bBegunPlay = 1;
		Info->bStartup = 1;

		// Init the game.
		if( Info->Game )
			Info->Game->eventInitGame( Options, Error );

		// Send PreBeginPlay.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventPreBeginPlay();

		// Set BeginPlay.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventBeginPlay();

		// Set zones.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->SetActorZone( GLevel->Actors(i), 1, 1 );

		// Post begin play.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventPostBeginPlay();

		// Begin scripting.
		for( i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventSetInitialState();

		// Find bases
		for( i=0; i<GLevel->Actors.Num(); i++ )
		{
			if( GLevel->Actors(i) ) 
			{
				if ( GLevel->Actors(i)->AttachTag != NAME_None )
				{
					//find actor to attach self onto
					for( INT j=0; j<GLevel->Actors.Num(); j++ )
					{
						if( GLevel->Actors(j) && (GLevel->Actors(j)->Tag == GLevel->Actors(i)->AttachTag) )
						{
							GLevel->Actors(i)->SetBase(GLevel->Actors(j), 0);
							break;
						}
					}
				}
				else if( !GLevel->Actors(i)->Base && GLevel->Actors(i)->bCollideWorld 
				 && (GLevel->Actors(i)->IsA(ADecoration::StaticClass()) || GLevel->Actors(i)->IsA(AInventory::StaticClass()) || GLevel->Actors(i)->IsA(APawn::StaticClass())) 
				 &&	((GLevel->Actors(i)->Physics == PHYS_None) || (GLevel->Actors(i)->Physics == PHYS_Rotating)) )
				{
					 GLevel->Actors(i)->FindBase();
					 if ( GLevel->Actors(i)->Base == Info )
						 GLevel->Actors(i)->SetBase(NULL, 0);
				}
			}
		}
		Info->bStartup = 0;
	}
	else GLevel->TimeSeconds = GLevel->GetLevelInfo()->TimeSeconds;
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post BeginPlay") );
#endif

	// Rearrange actors: static first, then others.
	guard(Rearrange);
	TArray<AActor*> Actors;
	Actors.AddItem(GLevel->Actors(0));
	Actors.AddItem(GLevel->Actors(1));
	for( INT i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && GLevel->Actors(i)->bStatic )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->iFirstDynamicActor=Actors.Num();
	for( i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && !GLevel->Actors(i)->bStatic )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->Actors.Empty();
	GLevel->Actors.Add( Actors.Num() );
	for( i=0; i<Actors.Num(); i++ )
		GLevel->Actors(i) = Actors(i);
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post Rearrange") );
#endif

	// Cleanup profiling.
#if DO_GUARD_SLOW
	guard(CleanupProfiling);
	for( TObjectIterator<UFunction> It; It; ++It )
		It->Calls = It->Cycles=0;
	GTicks=1;
	unguard;
#endif

	// Client init.
	guard(ClientInit);
	if( Client )
	{
		// Match Viewports to actors.
#if TARGET_XBOX
		ULevel* ViewLevel = GLevel->IsServer() ? GLevel : GEntry;
		if( !ViewLevel )
		{
			Error = TEXT("Xbox network client viewport has no Entry holding level");
			debugf( NAME_Log, TEXT("Xbox: aborting client viewport match because pending network map has no Entry level") );
			XboxSetLoadActivity( this, 0 );
			return NULL;
		}
		MatchViewportsToActors( Client, ViewLevel, URL );
#else
		MatchViewportsToActors( Client, GLevel->IsServer() ? GLevel : GEntry, URL );
#endif

		// Init brush tracker.
		if( appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))!=0 )//!!
			GLevel->BrushTracker = GNewBrushTracker( GLevel );

		// Set up audio.
		if( Audio )
		{
			Audio->SetViewport( Audio->GetViewport() );
#if TARGET_XBOX
			if( XboxIsCityIntroURL( URL ) )
				XboxStartFrontendMusic( Audio, TEXT("LoadMap post CityIntro native music") );
#endif
		}

		// Reset viewports.
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			UViewport* Viewport = Client->Viewports(i);
			Viewport->Input->ResetInput();
			if( Viewport->RenDev )
				Viewport->RenDev->Flush(1);
		}
	}
	unguard;
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post ClientInit") );
#endif

	// Init detail.
	GLevel->DetailChange( Info->bHighDetailMode );
#if TARGET_XBOX
	XboxMemMark( TEXT("LoadMap post DetailChange") );
#endif

	// Remember the URL.
	guard(RememberURL);
	LastURL = URL;
	unguard;

	// Remember DefaultPlayer options.
	if( GIsClient
#if TARGET_XBOX
	&&	appStricmp( *URL.Map, *FURL::DefaultLocalMap ) != 0
	&&	appStricmp( *URL.Map, TEXT("CityIntro") ) != 0
	&&	appStricmp( *URL.Map, TEXT("CityIntro.unr") ) != 0
#endif
	)
	{
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Name" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Team" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Class"), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Skin" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Face" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Voice" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("OverrideClass" ), TEXT("User") );
	}

	// Successfully started local level.
#if TARGET_XBOX
	XboxSetLoadActivity( this, 0 );
	XboxMemMark( TEXT("LoadMap return") );
#endif
	return GLevel;
	unguard;
}

/*-----------------------------------------------------------------------------
	Game Viewport functions.
-----------------------------------------------------------------------------*/

//
// Draw a global view.
//
void UGameEngine::Draw( UViewport* Viewport, UBOOL Blit, BYTE* HitData, INT* HitSize )
{
	guard(UGameEngine::Draw);
#if TARGET_XBOX
	static const UBOOL ProfileView = GetFileAttributesA("D:\\XboxSplitCombatBenchmark.ini") != 0xFFFFFFFF;
	const DOUBLE ProfileViewStart = ProfileView ? appSeconds() : 0.0;
#endif
	static INT DrawDiagCount = 0;
	DrawDiagCount++;
	UBOOL bDrawDiag = 0;
	if( bDrawDiag )
		debugf( NAME_Log, TEXT("XDRAW draw=%d begin viewport=%08X actor=%08X rendev=%08X"), DrawDiagCount, (DWORD)Viewport, Viewport ? (DWORD)Viewport->Actor : 0, Viewport ? (DWORD)Viewport->RenDev : 0 );

	// If not up and running yet, don't draw.
	if( !GIsRunning )
		return;
	UpdateConnectingMessage();
	if( bDrawDiag )
		debugf( NAME_Log, TEXT("XDRAW draw=%d after-connect-msg"), DrawDiagCount );

	// Get view location.
	AActor*      ViewActor    = Viewport->Actor;
	FVector      ViewLocation = ViewActor->Location;
	FRotator     ViewRotation = ViewActor->Rotation;
#if TARGET_XBOX
	APawn* XboxCharacterProofTarget = NULL;
	APlayerPawn* XboxCharacterProofViewer = NULL;
	static FName XboxCharacterProofTargetName = NAME_None;
	static INT XboxCharacterSoakEnabled = -1;
	static INT XboxGameplayRecording = -1;
	static INT XboxWeaponCloseup = -1;
	if( XboxWeaponCloseup < 0 )
		XboxWeaponCloseup = GetFileAttributesA( "D:\\XboxWeaponCloseup.ini" ) != 0xFFFFFFFF;
	if( XboxGameplayRecording < 0 )
		XboxGameplayRecording = GetFileAttributesA( "D:\\XboxGameplayRecording.ini" ) != 0xFFFFFFFF;
	if( XboxCharacterSoakEnabled < 0 )
		XboxCharacterSoakEnabled = GetFileAttributesA( "D:\\XboxCharacterSoak.ini" ) != 0xFFFFFFFF;
	if( XboxCharacterSoakEnabled )
	{
		XboxCharacterProofViewer = Cast<APlayerPawn>( Viewport->Actor );
		ULevel* ProofLevel = XboxCharacterProofViewer ? XboxCharacterProofViewer->GetLevel() : NULL;
		if( XboxCharacterProofViewer && ProofLevel && ProofLevel->GetLevelInfo() )
		{
			for
			(
				APawn* Pawn = ProofLevel->GetLevelInfo()->PawnList;
				!XboxCharacterProofTarget && Pawn;
				Pawn = Pawn->nextPawn
			)
			{
				if
				(
					Pawn != XboxCharacterProofViewer
				&&	Pawn->bViewTarget
				&&	Pawn->Health > 0
				)
				{
					XboxCharacterProofTarget = Pawn;
					XboxCharacterProofTargetName = Pawn->GetFName();
					break;
				}
			}
			APawn* AssignedTarget = Cast<APawn>( XboxCharacterProofViewer->ViewTarget );
			if
			(
				!XboxCharacterProofTarget
			&&	AssignedTarget
			&&	AssignedTarget != XboxCharacterProofViewer
			&&	AssignedTarget->Health > 0
			)
			{
				XboxCharacterProofTarget = AssignedTarget;
				XboxCharacterProofTargetName = AssignedTarget->GetFName();
			}
			for
			(
				APawn* Pawn = ProofLevel->GetLevelInfo()->PawnList;
				!XboxCharacterProofTarget && Pawn;
				Pawn = Pawn->nextPawn
			)
			{
				if
				(
					Pawn != XboxCharacterProofViewer
				&&	Pawn->GetFName() == XboxCharacterProofTargetName
				&&	Pawn->Health > 0
				)
				{
					XboxCharacterProofTarget = Pawn;
					break;
				}
			}
			if( XboxCharacterProofTarget )
			{
				XboxCharacterProofViewer->ViewTarget = NULL;
				XboxCharacterProofViewer->bBehindView = 0;
				XboxCharacterProofViewer->DesiredFOV = 75.0f;
				XboxCharacterProofViewer->FovAngle = 75.0f;
			}
		}
	}
#endif
	Viewport->Actor->eventPlayerCalcView( ViewActor, ViewLocation, ViewRotation );
	check(ViewActor);
#if TARGET_XBOX
	if( XboxCharacterProofTarget && XboxCharacterProofViewer )
	{
		// PlayerCalcView may restore a gameplay ViewTarget. The proof camera is
		// calculated below and must not make the observed pawn owner-invisible.
		XboxCharacterProofViewer->ViewTarget = NULL;
		XboxCharacterProofViewer->bBehindView = 0;
		const FLOAT DesiredDistance = XboxWeaponCloseup ? 60.0f : (XboxGameplayRecording ? 140.0f : 72.0f);
		FVector TargetFocus = XboxCharacterProofTarget->Location;
		// Centre a right-side proof on the body so feet and extended death/swim
		// poses remain visible. The older head-height close-up cropped them.
		if( !XboxGameplayRecording )
			TargetFocus.Z += Max( XboxCharacterProofTarget->EyeHeight * 0.55f, 12.0f );
		FRotator TargetRotation = XboxCharacterProofTarget->Rotation;
		FVector Forward = TargetRotation.Vector();
		Forward.Z = 0.0f;
		Forward = Forward.SafeNormal();
		if( XboxWeaponCloseup )
		{
			TargetFocus += Forward * 10.0f;
			TargetFocus.Z += Max( XboxCharacterProofTarget->EyeHeight * 0.55f, 12.0f );
		}
		const FVector Right(-Forward.Y,Forward.X,0.0f);
		const FVector CameraCandidates[6] =
		{
			TargetFocus + (XboxGameplayRecording ? (Right + Forward * 0.35f).SafeNormal() : Forward) * DesiredDistance + FVector(0,0,6),
			TargetFocus + Right * DesiredDistance + FVector(0,0,6),
			TargetFocus + (XboxGameplayRecording ? (Right - Forward * 0.35f).SafeNormal() : -Right) * DesiredDistance + FVector(0,0,6),
			TargetFocus + (XboxGameplayRecording ? (Right + Forward).SafeNormal() : -Forward) * DesiredDistance + FVector(0,0,6),
			TargetFocus + (Right - Forward).SafeNormal() * DesiredDistance + FVector(0,0,6),
			TargetFocus + Right * DesiredDistance + FVector(0,0,48)
		};
		INT CameraCandidate = 0;
		FLOAT BestCameraTime = -1.0f;
		FCheckResult CameraHit;
		for( INT CandidateIndex=0; CandidateIndex<(XboxGameplayRecording ? ARRAY_COUNT(CameraCandidates) : 4); CandidateIndex++ )
		{
			XboxCharacterProofTarget->GetLevel()->SingleLineCheck
			(
				CameraHit,
				XboxCharacterProofTarget,
				CameraCandidates[CandidateIndex],
				TargetFocus,
				TRACE_VisBlocking,
				FVector(6,6,6)
			);
			if( CameraHit.Time > BestCameraTime )
			{
				CameraCandidate = CandidateIndex;
				BestCameraTime = CameraHit.Time;
			}
			if( CameraHit.Time >= 0.98f )
			{
				CameraCandidate = CandidateIndex;
				BestCameraTime = 1.0f;
				break;
			}
		}
		const FLOAT CameraFraction = BestCameraTime >= 0.98f
			? 1.0f
			: Max( BestCameraTime - 0.08f, 0.0f );
		ViewActor = XboxCharacterProofViewer;
		ViewLocation = TargetFocus
			+ (CameraCandidates[CameraCandidate] - TargetFocus) * CameraFraction;
		ViewRotation = (TargetFocus-ViewLocation).Rotation();
		if( DrawDiagCount == 1 || (DrawDiagCount % 300) == 0 )
			debugf( NAME_Log, TEXT("XSKELCAMVIEW draw=%d target=%s dist=%.1f fov=%.1f source=manual candidate=%i fraction=%.2f"),
				DrawDiagCount, XboxCharacterProofTarget->GetFullName(), DesiredDistance,
				Viewport->Actor->FovAngle, CameraCandidate, CameraFraction );
	}
#endif
#if TARGET_XBOX
	XboxLightingProofApplyView( Viewport, DrawDiagCount, ViewActor, ViewLocation, ViewRotation );
#endif
	if( bDrawDiag )
		debugf( NAME_Log, TEXT("XDRAW draw=%d after-calc-view loc=(%.1f,%.1f,%.1f)"), DrawDiagCount, ViewLocation.X, ViewLocation.Y, ViewLocation.Z );

	// Precaching message.
	BYTE SavedAction = ViewActor->Level->LevelAction;
	if( Viewport->RenDev->PrecacheOnFlip && !Viewport->bSuspendPrecaching )
		ViewActor->Level->LevelAction = LEVACT_Precaching;

	// See if viewer is inside world.
	DWORD LockFlags=0;
	FCheckResult Hit;
	if( !GLevel->Model->PointCheck(Hit,NULL,ViewLocation,FVector(0,0,0),0) )
		LockFlags |= LOCKR_ClearScreen;

#if defined(LEGEND) //MWP
	if( Viewport->Actor->IsA( APlayerPawn::StaticClass() ) )
	{
		// call the PlayerPawn Render Control Interface (RCI) to assess clear-screen operations
		if( Viewport->Actor->ClearScreen() )
		{
			LockFlags |= LOCKR_ClearScreen;
		}

		// call the PlayerPawn Render Control Interface (RCI) to assess lighting recomputation
		//
		// WARNING: RecomputeLighting() should *not* return false regularly, or rendering 
		//          performance will be severly compromised
		if( Viewport->Actor->RecomputeLighting() )
		{
			guard(RecomputeLighting);
			Flush();
			unguard;
		}
	}
#endif

	// Lock the Viewport.
	check(Render);
	FPlane FlashScale = Client->ScreenFlashes ? 0.5*Viewport->Actor->FlashScale : FVector(0.5,0.5,0.5);
	FPlane FlashFog   = Client->ScreenFlashes ? Viewport->Actor->FlashFog : FVector(0,0,0);
	FlashScale.X = Clamp( FlashScale.X, 0.f, 1.f );
	FlashScale.Y = Clamp( FlashScale.Y, 0.f, 1.f );
	FlashScale.Z = Clamp( FlashScale.Z, 0.f, 1.f );
	FlashFog.X   = Clamp( FlashFog.X  , 0.f, 1.f );
	FlashFog.Y   = Clamp( FlashFog.Y  , 0.f, 1.f );
	FlashFog.Z   = Clamp( FlashFog.Z  , 0.f, 1.f );
	if( Viewport->Lock(FlashScale,FlashFog,FPlane(0,0,0,0),LockFlags,HitData,HitSize) )
	{
		if( bDrawDiag )
			debugf( NAME_Log, TEXT("XDRAW draw=%d lock-ok"), DrawDiagCount );
		// Setup rendering coords.
		FSceneNode* Frame = Render->CreateMasterFrame( Viewport, ViewLocation, ViewRotation, NULL );
#if TARGET_XBOX
		XboxViewportApplyViewRegion( Viewport, Frame );
#endif
		if( bDrawDiag )
			debugf( NAME_Log, TEXT("XDRAW draw=%d master-frame=%08X"), DrawDiagCount, (DWORD)Frame );

		// Update level audio.
#if TARGET_XBOX
		const DOUBLE ProfileAudioStart = ProfileView ? appSeconds() : 0.0;
		UBOOL bXboxUpdateAudio = XboxViewportShouldUpdateAudio( Viewport );
#else
		UBOOL bXboxUpdateAudio = 1;
#endif
		if( Audio && bXboxUpdateAudio )
		{
			clock(GLevel->AudioTickCycles);
			Audio->Update( ViewActor->Region, Frame->Coords );
			unclock(GLevel->AudioTickCycles);
		}

		// Render.
#if TARGET_XBOX
		const DOUBLE ProfileAudioSeconds = ProfileView ? appSeconds()-ProfileAudioStart : 0.0;
#endif
		Render->PreRender( Frame );
		if( bDrawDiag )
			debugf( NAME_Log, TEXT("XDRAW draw=%d prerender-done"), DrawDiagCount );
		Viewport->Canvas->Render = Render;
#if TARGET_XBOX
		// UC2004 does not run the normal scripted loading-screen draw on Xbox.
		// Our native renderer still needs one world frame to capture its loading
		// background, but calling console/player/menu script here is unsafe: a
		// TournamentConsole can DynamicLoadObject its LadderFonts while the map
		// loader has an active object queue.  Keep these capture/fallback draws
		// native-only and let ordinary frames resume script rendering.
		const UBOOL bXboxNativeLoadingDraw = GXboxSuppressLoadActivityDraw || GXboxDrawingLoadActivity;
		static INT XboxNativeLoadingDrawLogCount = 0;
		if( bXboxNativeLoadingDraw && XboxNativeLoadingDrawLogCount < 8 )
		{
			XboxNativeLoadingDrawLogCount++;
			debugf( NAME_Init, TEXT("Xbox: native-only loading frame suppressed scripted render") );
		}
#else
		const UBOOL bXboxNativeLoadingDraw = 0;
#endif
		if( Viewport->Console && !bXboxNativeLoadingDraw )
			Viewport->Console->PreRender( Frame );
		Viewport->Canvas->Update( Frame );
#if TARGET_XBOX
		if( !bXboxNativeLoadingDraw ) XboxViewportApplyProfileHUD(Viewport);
#endif
		if( !bXboxNativeLoadingDraw )
			Viewport->Actor->eventPreRender( Viewport->Canvas );
#if defined(LEGEND) //MWP
		INT SaveXB = Frame->XB, SaveYB = Frame->YB, SaveX = Frame->X, SaveY = Frame->Y;
		Frame->XB += Viewport->Canvas->OrgX;
		Frame->YB += Viewport->Canvas->OrgY;
		Frame->X = Viewport->Canvas->ClipX;
		Frame->Y = Viewport->Canvas->ClipY;
		Frame->ComputeRenderSize();
#endif
#if TARGET_XBOX
		const DOUBLE ProfileWorldStart = ProfileView ? appSeconds() : 0.0;
#endif
		if( Frame->X>0 && Frame->Y>0 && (!Viewport->Console || Viewport->Console->GetDrawWorld()) )
		{
			if( bDrawDiag )
				debugf( NAME_Log, TEXT("XDRAW draw=%d drawworld-begin frame=%08X"), DrawDiagCount, (DWORD)Frame );
			Render->DrawWorld( Frame );
			if( bDrawDiag )
				debugf( NAME_Log, TEXT("XDRAW draw=%d drawworld-end"), DrawDiagCount );
		}
#if defined(LEGEND) //MWP
		Frame->XB = SaveXB, Frame->YB = SaveYB, Frame->X = SaveX, Frame->Y = SaveY;
		Frame->ComputeRenderSize();
#endif
#if TARGET_XBOX
		const DOUBLE ProfileWorldEnd = ProfileView ? appSeconds() : 0.0;
#endif
		Viewport->RenDev->EndFlash();
#if TARGET_XBOX
		if( !bXboxNativeLoadingDraw ) XboxViewportApplyProfileHUD(Viewport);
		extern void XboxCanvasBeginAspect(UCanvas*);
		extern void XboxCanvasEndAspect(UCanvas*);
		XboxCanvasBeginAspect(Viewport->Canvas);
		const DOUBLE ProfileHudStart = ProfileView ? appSeconds() : 0.0;
		if( ProfileView ) XboxCanvasProfileBegin();
		if( !bXboxNativeLoadingDraw && XboxViewportShouldPostRenderPlayer( Viewport ) )
#endif
		Viewport->Actor->eventPostRender( Viewport->Canvas );
#if TARGET_XBOX
		INT ProfileCanvasCalls=0;
		const DOUBLE ProfileCanvasSeconds = ProfileView ? XboxCanvasProfileEnd(ProfileCanvasCalls) : 0.0;
		const DOUBLE ProfileHudPlayerEnd = ProfileView ? appSeconds() : 0.0;
#endif
		if( !bXboxNativeLoadingDraw && Viewport->Console
#if TARGET_XBOX
		&&	XboxViewportShouldPostRenderPlayer( Viewport )
#endif
		)
		{
			Viewport->Console->PostRender( Frame );
#if TARGET_XBOX
            Viewport->Console->PostRenderCanvas(Viewport->Canvas);
#else
			Viewport->Console->eventPostRender( Viewport->Canvas );
#endif
		}
#if TARGET_XBOX
		const DOUBLE ProfileHudConsoleEnd = ProfileView ? appSeconds() : 0.0;
		if( !bXboxNativeLoadingDraw )
			XboxMenuPostRender( Viewport, Viewport->Canvas );
		if( ViewActor->Level && ViewActor->Level->LevelAction == LEVACT_Loading )
			XboxDrawLoadingActivity( Viewport );
		XboxCanvasEndAspect(Viewport->Canvas);
#endif
		if( Audio && bXboxUpdateAudio )
			Audio->PostRender( Frame );

#if 0
/* BEGIN BETA VERSION */
		if(GLevel && GLevel->GetLevelInfo() && GLevel->GetLevelInfo()->Game && FString(GLevel->GetLevelInfo()->Game->GetClass()->GetName()) == FString(TEXT("UTIntro")))
		{
			if ( ((AGameInfo*) AGameInfo::StaticClass()->GetDefaultObject())->DemoBuild == 0 )
			{
				// "BETA VERSION" XOR'd with BetaDecoder
				static TCHAR BetaCypher[] = { 67, 4, 50, 41, 108, 125, 82, 27, 46, 55, 121, 25 };
				static TCHAR BetaDecoder[] = { 1, 65, 102, 104, 76, 43, 23, 73, 125, 126, 54, 87, 33, 78, 0 };
				static TCHAR BetaDecoded[] = TEXT("            "); // gets replaced with "BETA VERSION"

				for(INT i=0; BetaDecoded[i]; i++)
						BetaDecoded[i] = BetaCypher[i] ^ BetaDecoder[i];
			
				Frame->Viewport->Canvas->Color = FColor(255,255,255);
				Frame->Viewport->Canvas->CurX=0;
				Frame->Viewport->Canvas->CurY=0;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
				Frame->Viewport->Canvas->CurX=Frame->Viewport->Canvas->ClipX - 72;
				Frame->Viewport->Canvas->CurY=0;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
				Frame->Viewport->Canvas->CurX=0;
				Frame->Viewport->Canvas->CurY=Frame->Viewport->Canvas->ClipY - 10;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
				Frame->Viewport->Canvas->CurX=Frame->Viewport->Canvas->ClipX - 72;
				Frame->Viewport->Canvas->CurY=Frame->Viewport->Canvas->ClipY - 10;
				Frame->Viewport->Canvas->WrappedPrintf( Frame->Viewport->Canvas->SmallFont, 0, BetaDecoded );
			}
		}
/* END BETA VERSION */
#endif

		Viewport->Canvas->Render = 0;
		Render->PostRender( Frame );
		if( bDrawDiag )
			debugf( NAME_Log, TEXT("XDRAW draw=%d postrender-done unlock-begin"), DrawDiagCount );
#if TARGET_XBOX
		const DOUBLE ProfileUnlockStart = ProfileView ? appSeconds() : 0.0;
#endif
		Viewport->Unlock( Blit );
#if TARGET_XBOX
		const DOUBLE ProfileUnlockEnd = ProfileView ? appSeconds() : 0.0;
#endif
		if( bDrawDiag )
			debugf( NAME_Log, TEXT("XDRAW draw=%d unlock-done finish-begin"), DrawDiagCount );
		Render->FinishMasterFrame();
#if TARGET_XBOX
		if( ProfileView )
		{
			static DOUBLE WindowStart=0.0, Pre=0.0, AudioTime=0.0, World=0.0, Hud=0.0, UnlockTime=0.0, Finish=0.0;
			static DOUBLE HudPlayer=0.0, HudConsole=0.0, HudNative=0.0, HudFlash=0.0;
			static DOUBLE CanvasSeconds=0.0;
			static INT CanvasCalls=0;
			static INT Views=0;
			const DOUBLE Now=appSeconds();
			if( WindowStart == 0.0 ) WindowStart=ProfileViewStart;
			Pre+=ProfileWorldStart-ProfileViewStart-ProfileAudioSeconds;
			AudioTime+=ProfileAudioSeconds; World+=ProfileWorldEnd-ProfileWorldStart;
			Hud+=ProfileUnlockStart-ProfileWorldEnd; UnlockTime+=ProfileUnlockEnd-ProfileUnlockStart;
			CanvasSeconds+=ProfileCanvasSeconds; CanvasCalls+=ProfileCanvasCalls;
			HudPlayer+=ProfileHudPlayerEnd-ProfileHudStart;
			HudConsole+=ProfileHudConsoleEnd-ProfileHudPlayerEnd;
			HudNative+=ProfileUnlockStart-ProfileHudConsoleEnd;
			HudFlash+=ProfileHudStart-ProfileWorldEnd;
			Finish+=Now-ProfileUnlockEnd; Views++;
			if( Now-WindowStart >= 5.0 )
			{
				debugf(NAME_Log,TEXT("XPROFILEVIEW views=%d preMS=%.3f audioMS=%.3f worldMS=%.3f hudMS=%.3f unlockMS=%.3f finishMS=%.3f"),
					Views,1000.0*Pre/Views,1000.0*AudioTime/Views,1000.0*World/Views,
					1000.0*Hud/Views,1000.0*UnlockTime/Views,1000.0*Finish/Views);
				debugf(NAME_Log,TEXT("XPROFILEHUD views=%d playerMS=%.3f consoleMS=%.3f nativeMS=%.3f flashMS=%.3f"),
					Views,1000.0*HudPlayer/Views,1000.0*HudConsole/Views,1000.0*HudNative/Views,1000.0*HudFlash/Views);
				debugf(NAME_Log,TEXT("XPROFILECANVAS views=%d calls=%d nativeMS=%.3f"),Views,CanvasCalls,1000.0*CanvasSeconds/Views);
				CanvasSeconds=0.0; CanvasCalls=0;
				WindowStart=Now; Pre=AudioTime=World=Hud=UnlockTime=Finish=0.0; Views=0;
				HudPlayer=HudConsole=HudNative=HudFlash=0.0;
			}
		}
#endif
		if( bDrawDiag )
			debugf( NAME_Log, TEXT("XDRAW draw=%d finish-done"), DrawDiagCount );
	}
	ViewActor->Level->LevelAction = SavedAction;
	if( bDrawDiag )
		debugf( NAME_Log, TEXT("XDRAW draw=%d level-action-restored precache=%d suspend=%d"), DrawDiagCount, Viewport->RenDev ? Viewport->RenDev->PrecacheOnFlip : 0, Viewport->bSuspendPrecaching );

	// Precache now if desired.
	if( Viewport->RenDev->PrecacheOnFlip && !Viewport->bSuspendPrecaching )
	{
		Viewport->RenDev->PrecacheOnFlip = 0;
		if ( !ViewActor->Level->bNeverPrecache )
		{
			if( bDrawDiag )
				debugf( NAME_Log, TEXT("XDRAW draw=%d precache-begin"), DrawDiagCount );
			Render->Precache( Viewport );
			if( bDrawDiag )
				debugf( NAME_Log, TEXT("XDRAW draw=%d precache-end"), DrawDiagCount );
		}
	}
	if( bDrawDiag )
		debugf( NAME_Log, TEXT("XDRAW draw=%d end"), DrawDiagCount );

	unguard;
}

void ExportTravel( FOutputDevice& Out, AActor* Actor )
{
	guard(ExportTravel);
	debugf( TEXT("Exporting travelling actor of class %s"), Actor->GetClass()->GetPathName() );//!!xyzzy
	check(Actor);
	if( !Actor->bTravel )
		return;
	Out.Logf( TEXT("Class=%s Name=%s\r\n{\r\n"), Actor->GetClass()->GetPathName(), Actor->GetName() );
	for( TFieldIterator<UProperty> It(Actor->GetClass()); It; ++It )
	{
		for( INT Index=0; Index<It->ArrayDim; Index++ )
		{
			TCHAR Value[1024];
			if
			(	(It->PropertyFlags & CPF_Travel)
			&&	It->ExportText( Index, Value, (BYTE*)Actor, &Actor->GetClass()->Defaults(0), 0 ) )
			{
				Out.Log( It->GetName() );
				if( It->ArrayDim!=1 )
					Out.Logf( TEXT("[%i]"), Index );
				Out.Log( TEXT("=") );
				UObjectProperty* Ref = Cast<UObjectProperty>( *It );
				if( Ref && Ref->PropertyClass->IsChildOf(AActor::StaticClass()) )
				{
					UObject* Obj = *(UObject**)( (BYTE*)Actor + It->Offset + Index*It->ElementSize );
					Out.Logf( TEXT("%s\r\n"), Obj ? Obj->GetName() : TEXT("None") );
				}
				Out.Logf( TEXT("%s\r\n"), Value );
			}
		}
	}
	Out.Logf( TEXT("}\r\n") );
	unguard;
}

//
// Jumping viewport.
//
void UGameEngine::SetClientTravel( UPlayer* Player, const TCHAR* NextURL, UBOOL bItems, ETravelType TravelType )
{
	guard(UGameEngine::SetClientTravel);
	check(Player);
	check(NextURL);

	// Xbox frontend cleanup can release menu-owned strings and other transient
	// state.  Preserve the requested URL before invoking it so callers may pass
	// either a stack buffer or an FString owned by that frontend state.
	FString RequestedURL = NextURL;

#if TARGET_XBOX
	if( Audio ) Audio->Exec( TEXT("XAUDIOSETLOADING 1") );
	XboxMenuPreClientTravelCleanup();
#endif
	UViewport* Viewport    = CastChecked<UViewport>( Player );
	Viewport->TravelURL    = RequestedURL;
	Viewport->TravelType   = TravelType;
	Viewport->bTravelItems = bItems;
#if TARGET_XBOX
	debugf( NAME_Log, TEXT("Xbox: client travel queued url=%s type=%i items=%i viewport=%08X"),
		*Viewport->TravelURL, TravelType, bItems, (DWORD)Viewport );
#endif

	unguard;
}

/*-----------------------------------------------------------------------------
	Tick.
-----------------------------------------------------------------------------*/

//
// Get tick rate limitor.
//
FLOAT UGameEngine::GetMaxTickRate()
{
	guard(UGameEngine::GetMaxTickRate);
	static UBOOL LanPlay = ParseParam(appCmdLine(),TEXT("lanplay"));
	if( GLevel && GLevel->NetDriver && !GIsClient )
		return Clamp( LanPlay ? GLevel->NetDriver->LanServerMaxTickRate : GLevel->NetDriver->NetServerMaxTickRate, 10, 120 );
	else if( GLevel && GLevel->NetDriver && GLevel->NetDriver->ServerConnection )
		return GLevel->NetDriver->ServerConnection->CurrentNetSpeed/64;
	else if( GLevel && GLevel->DemoRecDriver && !GLevel->DemoRecDriver->ServerConnection )
		return Clamp( LanPlay ? GLevel->NetDriver->LanServerMaxTickRate : GLevel->DemoRecDriver->NetServerMaxTickRate, 10, 120 );
	else
		return 0;
	unguard;
}

//
// Update everything.
//
void UGameEngine::Tick( FLOAT DeltaSeconds )
{
	guard(UGameEngine::Tick);
#if TARGET_XBOX
	static const UBOOL ProfileCombat = GetFileAttributesA("D:\\XboxSplitCombatBenchmark.ini") != 0xFFFFFFFF;
	const DOUBLE ProfileTickStart = ProfileCombat ? appSeconds() : 0.0;
	DOUBLE ProfileLevelSeconds = 0.0, ProfileClientSeconds = 0.0;
#endif
	static INT EngineTickDiagCount = 0;
	static const UBOOL GXboxVerboseEngineTickLog = 0;
	EngineTickDiagCount++;
	UBOOL bTickDiag = GXboxVerboseEngineTickLog && ((EngineTickDiagCount <= 3)
		|| (EngineTickDiagCount >= 80 && EngineTickDiagCount <= 140)
		|| (EngineTickDiagCount >= 180 && EngineTickDiagCount <= 280)
		|| (EngineTickDiagCount >= 300 && EngineTickDiagCount <= 360)
		|| ((EngineTickDiagCount % 300) == 0));
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d begin dt=%.4f client=%08X glevel=%08X"), EngineTickDiagCount, DeltaSeconds, (DWORD)Client, (DWORD)GLevel );
	INT LocalTickCycles=0;
	clock(LocalTickCycles);

	// If all viewports closed, time to exit.
	if( Client && Client->Viewports.Num()==0 )
	{
		debugf( TEXT("All Windows Closed") );
		appRequestExit( 0 );
		return;
	}

	// If game is paused, release the cursor.
	static UBOOL WasPaused=1;
	if
	(	Client
	&&	Client->Viewports.Num()==1
	&&	GLevel
	&&	!Client->Viewports(0)->IsFullscreen() )
	{
		UBOOL IsPaused
		=	GLevel->GetLevelInfo()->Pauser!=TEXT("")
		||	Client->Viewports(0)->Actor->bShowMenu
		||	Client->Viewports(0)->bShowWindowsMouse;
		if( IsPaused && !WasPaused )
			Client->Viewports(0)->SetMouseCapture( 0, 0, 0 );
		else if( WasPaused && !IsPaused && Client->CaptureMouse )
			Client->Viewports(0)->SetMouseCapture( 1, 1, 1 );
		WasPaused = IsPaused;
	}
	else WasPaused=0;

	// Update subsystems.
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d static-begin"), EngineTickDiagCount );
	UObject::StaticTick();				
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d static-end"), EngineTickDiagCount );
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d cache-begin"), EngineTickDiagCount );
	GCache.Tick();
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d cache-end"), EngineTickDiagCount );

	// Update the level.
	guard(TickLevel);
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d level-begin"), EngineTickDiagCount );
	GameCycles=0;
	clock(GameCycles);
#if TARGET_XBOX
	const DOUBLE ProfileLevelStart = ProfileCombat ? appSeconds() : 0.0;
#endif
	if( GLevel )
	{
		// Decide whether to drop high detail because of frame rate
		if ( Client )
		{
			GLevel->GetLevelInfo()->bDropDetail = (DeltaSeconds > 1.f/Clamp(Client->MinDesiredFrameRate,1.f,100.f));
			GLevel->GetLevelInfo()->bAggressiveLOD = (DeltaSeconds > 1.f/Clamp(Client->MinDesiredFrameRate - 5.f,1.f,100.f));;
		}
		// tick the level
		GLevel->Tick( LEVELTICK_All, DeltaSeconds );
	}
	if( GEntry && GEntry!=GLevel )
		GEntry->Tick( LEVELTICK_All, DeltaSeconds );
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->Actor->GetLevel()!=GLevel )
		Client->Viewports(0)->Actor->GetLevel()->Tick( LEVELTICK_All, DeltaSeconds );
	unclock(GameCycles);
#if TARGET_XBOX
	if( ProfileCombat ) ProfileLevelSeconds = appSeconds() - ProfileLevelStart;
#endif
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d level-end"), EngineTickDiagCount );
	unguard;

	// Handle server travelling.
	guard(ServerTravel);
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d servertravel-begin"), EngineTickDiagCount );
	if( GLevel && GLevel->GetLevelInfo()->NextURL!=TEXT("") )
	{
		if( (GLevel->GetLevelInfo()->NextSwitchCountdown-=DeltaSeconds) <= 0.0 )
		{
#if TARGET_XBOX
			if( XboxIsCityIntroLevel( GLevel ) && XboxIsCityIntroTravelString( GLevel->GetLevelInfo()->NextURL ) )
			{
				debugf( NAME_Init, TEXT("Xbox: intercepting CityIntro self-travel %s"), *GLevel->GetLevelInfo()->NextURL );
				if( XboxRestartCityIntroFlyby( GLevel, Client && Client->Viewports.Num() ? Client->Viewports(0) : NULL ) )
					return;
			}
#endif
			// Travel to new level, and exit.
			TMap<FString,FString> TravelInfo;
			if( GLevel->GetLevelInfo()->NextURL==TEXT("?RESTART") )
			{
				TravelInfo = GLevel->TravelInfo;
			}
			else if( GLevel->GetLevelInfo()->bNextItems )
			{
				TravelInfo = GLevel->TravelInfo;
				for( INT i=0; i<GLevel->Actors.Num(); i++ )
				{
					APlayerPawn* P = Cast<APlayerPawn>( GLevel->Actors(i) );
					if( P && P->Player )
					{
						// Export items and self.
						FStringOutputDevice PlayerTravelInfo;
						ExportTravel( PlayerTravelInfo, P );
						for( AActor* Inv=P->Inventory; Inv; Inv=Inv->Inventory )
							ExportTravel( PlayerTravelInfo, Inv );
						TravelInfo.Set( *P->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );

						// Prevent local ClientTravel from taking place, since it will happen automatically.
						if( Cast<UViewport>( P->Player ) )
							Cast<UViewport>( P->Player )->TravelURL = TEXT("");
					}
				}
			}
			debugf( TEXT("Server switch level: %s"), *GLevel->GetLevelInfo()->NextURL );
			FString Error;
			Browse( FURL(&LastURL,*GLevel->GetLevelInfo()->NextURL,TRAVEL_Relative), &TravelInfo, Error );
			GLevel->GetLevelInfo()->NextURL = TEXT("");
			return;
		}
	}
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d servertravel-end"), EngineTickDiagCount );
	unguard;

	// Handle client travelling.
	guard(ClientTravel);
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d clienttravel-begin"), EngineTickDiagCount );
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->TravelURL!=TEXT("") )
	{
		// Travel to new level, and exit.
		UViewport* Viewport = Client->Viewports( 0 );
		#if TARGET_XBOX
		debugf( NAME_Log, TEXT("Xbox: client travel consuming url=%s type=%i items=%i viewport=%08X"),
			*Viewport->TravelURL, Viewport->TravelType, Viewport->bTravelItems, (DWORD)Viewport );
		#endif
		TMap<FString,FString> TravelInfo;

		// Export items.
		if( appStricmp(*Viewport->TravelURL,TEXT("?RESTART"))==0 )
		{
			TravelInfo = GLevel->TravelInfo;
		}
		else if( Viewport->bTravelItems )
		{
			debugf( TEXT("Export travel for: %s"), *Viewport->Actor->PlayerReplicationInfo->PlayerName );
			FStringOutputDevice PlayerTravelInfo;
			ExportTravel( PlayerTravelInfo, Viewport->Actor );
			for( AActor* Inv=Viewport->Actor->Inventory; Inv; Inv=Inv->Inventory )
				ExportTravel( PlayerTravelInfo, Inv );
			TravelInfo.Set( *Viewport->Actor->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );
		}
		FString Error;
		Browse( FURL(&LastURL,*Viewport->TravelURL,Viewport->TravelType), &TravelInfo, Error );
		Viewport->TravelURL=TEXT("");
#if TARGET_XBOX
		// Browse can reject travel before reaching LoadMap's scope guard.
		if( Audio && !GPendingLevel ) Audio->Exec( TEXT("XAUDIOSETLOADING 0") );
#endif

		return;
	}
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d clienttravel-end"), EngineTickDiagCount );
	unguard;

	// Update the pending level.
	guard(TickPending);
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d pending-begin"), EngineTickDiagCount );
	if( GPendingLevel )
	{
		GPendingLevel->Tick( DeltaSeconds );
		if( GPendingLevel->Error!=TEXT("") )
		{
			// Pending connect failed.
			guard(PendingFailed);
#if TARGET_XBOX
			if( Audio ) Audio->Exec( TEXT("XAUDIOSETLOADING 0") );
#endif
			SetProgress( LocalizeError("ConnectionFailed"), *GPendingLevel->Error, 4.0 );
			debugf( NAME_Log, LocalizeError("Pending"), *GPendingLevel->URL.String(), *GPendingLevel->Error );
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
		else if( GPendingLevel->Success && !GPendingLevel->FilesNeeded && !GPendingLevel->SentJoin )
		{
			// Attempt to load the map.
			FString Error;
#if TARGET_XBOX
			if( Client && !GEntry && !GPendingLevel->LonePlayer )
			{
				guard(RestoreEntryForPendingClient);
				debugf( NAME_Log, TEXT("Xbox: restoring Entry holding level for pending network client") );
				FString EntryError;
				FURL EntryURL( TEXT("Entry") );
				EntryURL.AddOption( TEXT("Game=Engine.GameInfo") );
				EntryURL.AddOption( TEXT("Class=Engine.Spectator") );
				EntryURL.AddOption( TEXT("Team=255") );
				if( LoadMap( EntryURL, NULL, NULL, EntryError ) )
				{
					Exchange( GLevel, GEntry );
					if( GEntry )
						GEntry->GetLevelInfo()->LevelAction = LEVACT_Connecting;
					XboxMemMark( TEXT("TickPending restored Entry for net client") );
				}
				else
				{
					Error = EntryError.Len() ? EntryError : TEXT("Xbox failed to restore Entry for network client");
					debugf( NAME_Log, TEXT("Xbox: failed restoring Entry for pending network client: %s"), *Error );
				}
				unguard;
			}
#endif
			guard(AttemptLoadPending);
			if( Error==TEXT("") )
				LoadMap( GPendingLevel->URL, GPendingLevel, NULL, Error );
			if( Error!=TEXT("") )
			{
				SetProgress( LocalizeError("ConnectionFailed"), *Error, 4.0 );
			}
			else if( !GPendingLevel->LonePlayer )
			{
				// Show connecting message, cause precaching to occur.
				GLevel->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				if( GEntry )
					GEntry->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				else
					debugf( NAME_Log, TEXT("Xbox: pending network connect has no Entry level; using loaded level only") );
				if( Client )
					Client->Tick();

				// Send join.
				GPendingLevel->SendJoin();
				GPendingLevel->NetDriver = NULL;
				GPendingLevel->DemoRecDriver = NULL;
			}
			unguard;

			// Kill the pending level.
			guard(KillPending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
	}
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d pending-end"), EngineTickDiagCount );
	unguard;

#if TARGET_XBOX
	// Proof-only deterministic animation selection happens after game simulation
	// so bot AI cannot replace the requested state before the frame is rendered.
	XboxApplySkeletalStateProof( GLevel, DeltaSeconds );
	XboxApplySkaarjSkinProof( GLevel );
#endif

	// Render everything.
	guard(ClientTick);
	INT LocalClientCycles=0;
#if TARGET_XBOX
	const DOUBLE ProfileClientStart = ProfileCombat ? appSeconds() : 0.0;
#endif
	if( Client )
	{
		if( bTickDiag )
			debugf( NAME_Log, TEXT("XTICK tick=%d client-begin"), EngineTickDiagCount );
		clock(LocalClientCycles);
		Client->Tick();
		unclock(LocalClientCycles);
		if( bTickDiag )
			debugf( NAME_Log, TEXT("XTICK tick=%d client-end"), EngineTickDiagCount );
	}
	ClientCycles=LocalClientCycles;
#if TARGET_XBOX
	if( ProfileCombat )
	{
		ProfileClientSeconds = appSeconds() - ProfileClientStart;
		static DOUBLE WindowStart = 0.0, TotalSeconds = 0.0, LevelSeconds = 0.0, ClientSeconds = 0.0;
		static INT Samples = 0;
		const DOUBLE Now = appSeconds();
		if( WindowStart == 0.0 ) WindowStart = ProfileTickStart;
		TotalSeconds += Now - ProfileTickStart;
		LevelSeconds += ProfileLevelSeconds;
		ClientSeconds += ProfileClientSeconds;
		Samples++;
		if( Now - WindowStart >= 5.0 )
		{
			debugf(NAME_Log,TEXT("XPROFILE samples=%d totalMS=%.3f levelMS=%.3f clientMS=%.3f otherMS=%.3f"),
				Samples,1000.0*TotalSeconds/Samples,1000.0*LevelSeconds/Samples,
				1000.0*ClientSeconds/Samples,1000.0*(TotalSeconds-LevelSeconds-ClientSeconds)/Samples);
			WindowStart=Now; TotalSeconds=LevelSeconds=ClientSeconds=0.0; Samples=0;
		}
	}
#endif
	unguard;

	unclock(LocalTickCycles);
	TickCycles=LocalTickCycles;
	GTicks++;
	if( bTickDiag )
		debugf( NAME_Log, TEXT("XTICK tick=%d end gticks=%d"), EngineTickDiagCount, GTicks );
	unguard;
}

/*-----------------------------------------------------------------------------
	Saving the game.
-----------------------------------------------------------------------------*/

//
// Save the current game state to a file.
//
void UGameEngine::SaveGame( INT Position )
{
	guard(UGameEngine::SaveGame);

	TCHAR Filename[256];
	GFileManager->MakeDirectory( *GSys->SavePath, 0 );
	appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Save%i.usa"), *GSys->SavePath, Position );
	GLevel->GetLevelInfo()->LevelAction=LEVACT_Saving;
	PaintProgress();
	GWarn->BeginSlowTask( LocalizeProgress("Saving"), 1, 0 );
	if( GLevel->BrushTracker )
	{
		delete GLevel->BrushTracker;
		GLevel->BrushTracker = NULL;
	}
	GLevel->CleanupDestroyed( 1 );
	if( SavePackage( GLevel->GetOuter(), GLevel, 0, Filename, GLog ) )
	{
		// Copy the hub stack.
		for( INT i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
		{
			TCHAR Src[256], Dest[256];
			appSprintf( Src, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i );
			appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, Position, i );
			GFileManager->Copy( Src, Dest );
		}
		while( 1 )
		{
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, Position, i++ );
			if( GFileManager->FileSize(Filename)<=0 )
				break;
			GFileManager->Delete( Filename );
		}
	}
	for( INT i=0; i<GLevel->Actors.Num(); i++ )
		if( Cast<AMover>(GLevel->Actors(i)) )
			Cast<AMover>(GLevel->Actors(i))->SavedPos = FVector(-1,-1,-1);
	GLevel->BrushTracker = GNewBrushTracker( GLevel );
	GWarn->EndSlowTask();
	GLevel->GetLevelInfo()->LevelAction=LEVACT_None;
	GCache.Flush();

	unguard;
}

/*-----------------------------------------------------------------------------
	Mouse feedback.
-----------------------------------------------------------------------------*/

//
// Mouse delta while dragging.
//
void UGameEngine::MouseDelta( UViewport* Viewport, DWORD ClickFlags, FLOAT DX, FLOAT DY )
{
	guard(UGameEngine::MouseDelta);
	if
	(	(ClickFlags & MOUSE_FirstHit)
	&&	Client
	&&	Client->Viewports.Num()==1
	&&	GLevel
	&&	!Client->Viewports(0)->IsFullscreen()
	&&	GLevel->GetLevelInfo()->Pauser==TEXT("")
	&&	!Viewport->Actor->bShowMenu
	&&  !Viewport->bShowWindowsMouse )
	{
		Viewport->SetMouseCapture( 1, 1, 1 );
	}
	else if( (ClickFlags & MOUSE_LastRelease) && !Client->CaptureMouse )
	{
		Viewport->SetMouseCapture( 0, 0, 0 );
	}
	unguard;
}

//
// Absolute mouse position.
//
void UGameEngine::MousePosition( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::MousePosition);

	if( Viewport )
	{
		Viewport->WindowsMouseX = X;
		Viewport->WindowsMouseY = Y;
	}

	unguard;
}

//
// Mouse clicking.
//
void UGameEngine::Click( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::Click);
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
