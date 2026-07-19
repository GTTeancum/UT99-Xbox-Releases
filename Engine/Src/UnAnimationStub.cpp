/*=============================================================================
	UnAnimationStub.cpp: UE1 UAnimation native compatibility.

	v436 GOTY's Engine.u imports Engine.Animation as the PropertyClass of
	AActor::SkelAnim. v436 binary contains no Animation class export, so the
	import only resolves if a native UAnimation is registered with the engine.
	PS2/DC skeletal character packages also contain real Engine.Animation
	exports, so this class consumes their UE1 animation payload.
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UAnimation);

void UAnimation::Serialize( FArchive& Ar )
{
	guard(UAnimation::Serialize);

	Super::Serialize( Ar );
	Ar << RefBones;
	Ar << Moves;
	Ar << AnimSeqs;

#if TARGET_XBOX
	if( Ar.IsLoading() )
	{
		const INT LoadedBoneCount = RefBones.Num();
		const INT LoadedMoveCount = Moves.Num();

		debugf
		(
			NAME_Log,
			TEXT("XAnimation load %s bones=%i moves=%i seqs=%i"),
			GetFullName(),
			RefBones.Num(),
			Moves.Num(),
			AnimSeqs.Num()
		);

		// Xbox skeletal meshes render their bind pose and retain only sequence metadata.
		RefBones.Empty();
		Moves.Empty();

		debugf
		(
			NAME_Log,
			TEXT("XAnimation discard %s bones=%i moves=%i retainedSeqs=%i"),
			GetFullName(),
			LoadedBoneCount,
			LoadedMoveCount,
			AnimSeqs.Num()
		);
	}
#endif

	unguardobj;
}
