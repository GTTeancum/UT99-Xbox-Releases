/*=============================================================================
	V469VirtualStubs.cpp: Empty/default implementations for virtual methods that
	v469 SDK headers declare but our v400 source base never implemented.

	These are needed because v469's per-class headers (AActor.h, ANavigationPoint.h,
	APawn.h, etc.) declare new virtual methods (GetAnim, UpdateFrequency, etc.) as
	non-pure abstracts.  v400's .cpp files don't implement them — they didn't exist
	then.  Without stubs the linker complains about unresolved externals.

	We provide minimal implementations: virtuals that return values give the
	zero/default value, void virtuals are no-ops.  Most of these are only invoked
	from new v469 engine code paths (replication, animation, networking) that our
	Xbox port doesn't exercise.  If a path is later found to need real behavior,
	replace the stub with the real implementation.
=============================================================================*/

#include "EnginePrivate.h"

// AActor: GetAnim — looks up an animation sequence by name.  Returns NULL meaning
// "no animation found"; the caller checks for NULL and skips the operation.
FMeshAnimSeq* AActor::GetAnim( FName SequenceName )
{
	return NULL;
}

// AActor: UpdateFrequency — returns the desired network update rate for this actor
// when seen by Viewer at ViewPos looking ViewDir.  Returning 1.0 (= "every tick")
// matches v400 default behavior since v400 had no per-actor frequency throttling.
FLOAT AActor::UpdateFrequency( AActor* Viewer, FVector& ViewDir, FVector& ViewPos )
{
	return 1.0f;
}

// ACarcass overrides UpdateFrequency in v469.  Same default.
FLOAT ACarcass::UpdateFrequency( AActor* Viewer, FVector& ViewDir, FVector& ViewPos )
{
	return AActor::UpdateFrequency( Viewer, ViewDir, ViewPos );
}

// AInventory overrides UpdateFrequency in v469.
FLOAT AInventory::UpdateFrequency( AActor* Viewer, FVector& ViewDir, FVector& ViewPos )
{
	return AActor::UpdateFrequency( Viewer, ViewDir, ViewPos );
}

// AInventory: NoVariablesToReplicate — returns nonzero if nothing has changed since
// OldVer.  v400 has no diff-tracking, so always say "we may have changed" → 0.
UBOOL AInventory::NoVariablesToReplicate( AActor* OldVer )
{
	return 0;
}

// ALevelInfo / AWeapon: 5-arg GetOptimizedRepList just defers to AActor's impl.
INT* ALevelInfo::GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, INT NumReps )
{
	return AActor::GetOptimizedRepList( InDefault, Retire, Ptr, Map, NumReps );
}
INT* AWeapon::GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, INT NumReps )
{
	return AActor::GetOptimizedRepList( InDefault, Retire, Ptr, Map, NumReps );
}

// Empty net-receive / lifecycle hooks — all no-ops on Xbox (no networking yet).
void APlayerReplicationInfo::PreNetReceive() {}
void APlayerReplicationInfo::PostNetReceive() {}
void APlayerPawn::PostNetReceive() {}

void AMutator::Destroyed() {}
void APawn::Destroyed() {}
void ANavigationPoint::Destroyed() {}
void ANavigationPoint::Spawned() {}
void ANavigationPoint::PostEditMove() {}
void AMover::PreNetReceive() {}
void AMover::PostNetReceive() {}

// New v469 script-native functions (exec*) — empty bodies so scripts that call
// these get a no-op rather than a link error.  Add real implementations later
// if any of these are exercised by gameplay we want to support.
void AActor::execLinkSkelAnim( FFrame& Stack, RESULT_DECL )      { Stack.Code += Stack.ReadInt(); }
void AActor::execGetCacheEntry( FFrame& Stack, RESULT_DECL )     { Stack.Code += Stack.ReadInt(); }
void AActor::execMoveCacheEntry( FFrame& Stack, RESULT_DECL )    { Stack.Code += Stack.ReadInt(); }
void AActor::execAddToPackageMap( FFrame& Stack, RESULT_DECL )   { Stack.Code += Stack.ReadInt(); }
void AActor::execIsInPackageMap( FFrame& Stack, RESULT_DECL )    { Stack.Code += Stack.ReadInt(); }
void APawn::execCheckValidSkinPackage( FFrame& Stack, RESULT_DECL ) { Stack.Code += Stack.ReadInt(); }
void AGameInfo::execGetMutatorList( FFrame& Stack, RESULT_DECL ) { Stack.Code += Stack.ReadInt(); }
