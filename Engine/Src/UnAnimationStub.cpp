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
		INT TimedTracks = 0;
		INT NonMonotonicTracks = 0;
		INT MismatchedTimeTracks = 0;
		INT EmptyRotationTracks = 0;
		INT EmptyPositionTracks = 0;
		INT TotalTracks = 0;

		for( INT MoveIndex=0; MoveIndex<Moves.Num(); MoveIndex++ )
		{
			FAnimationMotionChunk& Move = Moves(MoveIndex);
			for( INT TrackIndex=0; TrackIndex<Move.AnimTracks.Num(); TrackIndex++ )
			{
				FAnimationTrack& Track = Move.AnimTracks(TrackIndex);
				TotalTracks++;
				if( !Track.KeyQuat.Num() )
					EmptyRotationTracks++;
				if( !Track.KeyPos.Num() )
					EmptyPositionTracks++;

				if( Track.KeyTime.Num() )
				{
					TimedTracks++;
					const INT ExpectedKeys = Max( Track.KeyQuat.Num(), Track.KeyPos.Num() );
					if( Track.KeyTime.Num() != ExpectedKeys )
						MismatchedTimeTracks++;
					for( INT KeyIndex=1; KeyIndex<Track.KeyTime.Num(); KeyIndex++ )
						if( Track.KeyTime(KeyIndex) < Track.KeyTime(KeyIndex-1) )
						{
							NonMonotonicTracks++;
							break;
						}
				}

				// UE1 skeletal packages carry unreliable time tracks. Preserve their
				// native basis; this engine consumes UE1 data directly.
				Track.KeyTime.Empty();
			}
		}

		debugf
		(
			NAME_Log,
			TEXT("XSKELAUDIT animation=%s bones=%i moves=%i seqs=%i tracks=%i timed=%i nonmonotonic=%i mismatched=%i emptyrot=%i emptypos=%i timing=uniform basis=ue1-native"),
			GetFullName(),
			RefBones.Num(),
			Moves.Num(),
			AnimSeqs.Num(),
			TotalTracks,
			TimedTracks,
			NonMonotonicTracks,
			MismatchedTimeTracks,
			EmptyRotationTracks,
			EmptyPositionTracks
		);
	}
#endif

	unguardobj;
}
