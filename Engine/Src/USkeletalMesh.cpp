/*=============================================================================
	USkeletalMesh.cpp: UE1 skeletal mesh compatibility for the Xbox port.
=============================================================================*/

#include "EnginePrivate.h"

struct FSkeletalExtWedge
{
	_WORD iVertex;
	FLOAT U;
	FLOAT V;

	friend FArchive& operator<<( FArchive& Ar, FSkeletalExtWedge& W )
	{
		return Ar << W.iVertex << W.U << W.V;
	}
};

enum { SKELETAL_AUDIT_SAMPLES = 32 };

#if TARGET_XBOX
static UBOOL SkelAuditEnabled()
{
	static UBOOL Initialized = 0;
	static UBOOL Enabled = 0;
	if( !Initialized )
	{
		Enabled
			= GetFileAttributesA( "D:\\XboxCharacterSoak.ini" ) != 0xFFFFFFFF
			|| GetFileAttributesA( "D:\\XboxSkeletalAudit.ini" ) != 0xFFFFFFFF;
		Initialized = 1;
	}
	return Enabled;
}
#endif

struct CFSkeletalHeader
{
	USkeletalMesh* CachedMesh;
	FLOAT          CachedFrame;
	FName          CachedSeq;
	INT            CachedLodVerts;
	UBOOL          CachedBonesValid;
	AActor*        CachedAnimOwner;
	UAnimation*    CachedAnimation;
	UBOOL          CachedLoop;
	FName          AuditSeq;
	FLOAT          AuditFrame;
	FVector        AuditMin;
	FVector        AuditMax;
	INT            AuditPoseCount;
	INT            AuditLodVerts;
	INT            AuditPreviousLodVerts;
	INT            AuditLodChanges;
	INT            AuditLodOscillations;
	INT            AuditSampleCount;
	FVector        AuditSamples[SKELETAL_AUDIT_SAMPLES];
	INT            AuditNormalSampleCount;
	FVector        AuditNormals[SKELETAL_AUDIT_SAMPLES];
	INT            AuditDegenerateFaces;
	UBOOL          AuditHasBounds;
};

static FArchive& operator<<( FArchive& Ar, FCoords& C )
{
	return Ar << C.Origin << C.XAxis << C.YAxis << C.ZAxis;
}

static FVector SkelPivotTransformVector( const FCoords& C, const FVector& V )
{
	return V.TransformVectorBy( C );
}

static FVector SkelPivotTransformPoint( const FCoords& C, const FVector& V )
{
	return C.Origin + SkelPivotTransformVector( C, V );
}

static FCoords SkelComposeCoords( const FCoords& Parent, const FCoords& Local )
{
	// FCoords::ApplyPivot from the shipped UE1 skeletal evaluator.
	return FCoords
	(
		Local.Origin.TransformVectorBy( Parent ) + Parent.Origin,
		Parent.XAxis.TransformVectorBy( Local ),
		Parent.YAxis.TransformVectorBy( Local ),
		Parent.ZAxis.TransformVectorBy( Local )
	);
}

static FCoords SkelPivotInverse( const FCoords& C )
{
	return FCoords
	(
		-C.Origin.TransformVectorBy( C ),
		FVector( C.XAxis.X, C.YAxis.X, C.ZAxis.X ),
		FVector( C.XAxis.Y, C.YAxis.Y, C.ZAxis.Y ),
		FVector( C.XAxis.Z, C.YAxis.Z, C.ZAxis.Z )
	);
}

static UBOOL SkelFiniteFloat( FLOAT Value )
{
	const DWORD Bits = *(DWORD*)&Value;
	return (Bits & 0x7f800000) != 0x7f800000;
}

static UBOOL SkelFiniteVector( const FVector& Value )
{
	return SkelFiniteFloat(Value.X) && SkelFiniteFloat(Value.Y) && SkelFiniteFloat(Value.Z);
}

static void SkelNormalizeQuat( FAnimationQuat& Q )
{
	const FLOAT SizeSquared = Q.X*Q.X + Q.Y*Q.Y + Q.Z*Q.Z + Q.W*Q.W;
	if( SizeSquared > 0.000001f )
	{
		const FLOAT Scale = 1.0f / appSqrt( SizeSquared );
		Q.X *= Scale;
		Q.Y *= Scale;
		Q.Z *= Scale;
		Q.W *= Scale;
	}
	else
	{
		Q.X = Q.Y = Q.Z = 0.0f;
		Q.W = 1.0f;
	}
}

static FAnimationQuat SkelSlerpQuat( const FAnimationQuat& A, const FAnimationQuat& B, FLOAT Alpha )
{
	FAnimationQuat Result;
	const FLOAT RawCosom = A.X*B.X + A.Y*B.Y + A.Z*B.Z + A.W*B.W;
	const FLOAT Cosom = Clamp( Abs(RawCosom), 0.0f, 1.0f );
	FLOAT ScaleA;
	FLOAT ScaleB;
	if( Cosom < 0.999999f )
	{
		const FLOAT Omega = (FLOAT)acos( Cosom );
		const FLOAT SinOmega = appSin( Omega );
		if( Abs(SinOmega) > 0.000001f )
		{
			const FLOAT InvSinOmega = 1.0f / SinOmega;
			ScaleA = appSin( (1.0f-Alpha) * Omega ) * InvSinOmega;
			ScaleB = appSin( Alpha * Omega ) * InvSinOmega;
		}
		else
		{
			ScaleA = 1.0f - Alpha;
			ScaleB = Alpha;
		}
	}
	else
	{
		ScaleA = 1.0f - Alpha;
		ScaleB = Alpha;
	}
	if( RawCosom < 0.0f )
		ScaleB = -ScaleB;

	Result.X = ScaleA*A.X + ScaleB*B.X;
	Result.Y = ScaleA*A.Y + ScaleB*B.Y;
	Result.Z = ScaleA*A.Z + ScaleB*B.Z;
	Result.W = ScaleA*A.W + ScaleB*B.W;
	SkelNormalizeQuat( Result );
	return Result;
}

static FCoords SkelCoordsFromQuat( FAnimationQuat Q, const FVector& Position )
{
	SkelNormalizeQuat( Q );

	const FLOAT X2 = Q.X * 2.0f;
	const FLOAT Y2 = Q.Y * 2.0f;
	const FLOAT Z2 = Q.Z * 2.0f;
	const FLOAT XX = Q.X * X2;
	const FLOAT XY = Q.X * Y2;
	const FLOAT XZ = Q.X * Z2;
	const FLOAT YY = Q.Y * Y2;
	const FLOAT YZ = Q.Y * Z2;
	const FLOAT ZZ = Q.Z * Z2;
	const FLOAT WX = Q.W * X2;
	const FLOAT WY = Q.W * Y2;
	const FLOAT WZ = Q.W * Z2;

	return FCoords
	(
		Position,
		FVector( 1.0f - (YY + ZZ), XY - WZ,          XZ + WY          ),
		FVector( XY + WZ,          1.0f - (XX + ZZ), YZ - WX          ),
		FVector( XZ - WY,          YZ + WX,          1.0f - (XX + YY) )
	);
}

static FLOAT SkelWrapFrame( FLOAT Frame, FLOAT Duration, UBOOL Loop )
{
	if( Duration <= 0.0f )
		return 0.0f;
	if( Loop )
	{
		while( Frame >= Duration )
			Frame -= Duration;
		while( Frame < 0.0f )
			Frame += Duration;
		return Frame;
	}
	return Clamp( Frame, 0.0f, Duration );
}

static void SkelGetUniformKeyParams
(
	INT                  KeyCount,
	FLOAT                Frame,
	FLOAT                Duration,
	UBOOL                Loop,
	INT&                 KeyA,
	INT&                 KeyB,
	FLOAT&               Alpha
)
{
	KeyA = KeyB = 0;
	Alpha = 0.0f;
	if( KeyCount <= 1 || Duration <= 0.0f )
		return;

	Frame = SkelWrapFrame( Frame, Duration, Loop );
	const FLOAT Position = Frame / Duration * KeyCount;
	KeyA = Min( appFloor(Position), KeyCount - 1 );
	KeyB = KeyA + 1;
	Alpha = Position - KeyA;
	if( KeyB >= KeyCount )
	{
		KeyB = Loop ? 0 : KeyCount - 1;
		if( !Loop )
			Alpha = 0.0f;
	}
	Alpha = Clamp( Alpha, 0.0f, 1.0f );
}

static void SkelGetKeyParams
(
	const FAnimationTrack& Track,
	FLOAT                  Frame,
	FLOAT                  Duration,
	UBOOL                  Loop,
	INT&                   KeyA,
	INT&                   KeyB,
	FLOAT&                 Alpha
)
{
	const INT KeyCount = Track.KeyTime.Num();
	if( KeyCount <= 0 )
	{
		SkelGetUniformKeyParams( Max(Track.KeyQuat.Num(),Track.KeyPos.Num()), Frame, Duration, Loop, KeyA, KeyB, Alpha );
		return;
	}

	KeyA = KeyB = 0;
	Alpha = 0.0f;
	if( KeyCount == 1 || Duration <= 0.0f )
		return;

	Frame = SkelWrapFrame( Frame, Duration, Loop );
	KeyA = KeyCount - 1;
	for( INT KeyIndex=1; KeyIndex<KeyCount; KeyIndex++ )
	{
		if( Frame < Track.KeyTime(KeyIndex) )
		{
			KeyA = KeyIndex - 1;
			break;
		}
	}

	const UBOOL WrapKey = KeyA >= KeyCount - 1;
	// One-shot tracks hold their final key. Only a looping track may blend
	// its last key back to the first during the remaining duration.
	if( WrapKey && !Loop )
	{
		KeyB = KeyA;
		return;
	}
	KeyB = WrapKey ? 0 : KeyA + 1;
	const FLOAT Interval = WrapKey
		? Duration - Track.KeyTime(KeyA)
		: Abs( Track.KeyTime(KeyB) - Track.KeyTime(KeyA) );
	if( Interval > 0.000001f )
		Alpha = (Frame - Track.KeyTime(KeyA)) / Interval;
	Alpha = Clamp( Alpha, 0.0f, 1.0f );
}

static void SkelSampleTrack
(
	const FAnimationTrack& Track,
	FLOAT                  Frame,
	FLOAT                  Duration,
	UBOOL                  Loop,
	FVector&               Position,
	FAnimationQuat&        Orientation
)
{
	INT A, B;
	FLOAT Alpha;
	SkelGetKeyParams( Track, Frame, Duration, Loop, A, B, Alpha );

	if( Track.KeyPos.Num() )
	{
		const INT PosA = Min( A, Track.KeyPos.Num()-1 );
		const INT PosB = Min( B, Track.KeyPos.Num()-1 );
		Position = Track.KeyPos(PosA) + (Track.KeyPos(PosB) - Track.KeyPos(PosA)) * Alpha;
	}

	if( Track.KeyQuat.Num() )
	{
		const INT QuatA = Min( A, Track.KeyQuat.Num()-1 );
		const INT QuatB = Min( B, Track.KeyQuat.Num()-1 );
		Orientation = Alpha > 0.0f
			? SkelSlerpQuat( Track.KeyQuat(QuatA), Track.KeyQuat(QuatB), Alpha )
			: Track.KeyQuat(QuatA);
	}
}

static INT SkelFindSequenceIndex( const UAnimation* Animation, FName Sequence )
{
	if( !Animation )
		return INDEX_NONE;
	for( INT Index=0; Index<Animation->AnimSeqs.Num(); Index++ )
		if( Animation->AnimSeqs(Index).Name == Sequence )
			return Index;
	return INDEX_NONE;
}

static void SkelSampleLocalPose
(
	const USkeletalMesh* Mesh,
	INT                  SequenceIndex,
	FLOAT                NormalizedFrame,
	UBOOL                Loop,
	FVector*             Positions,
	FAnimationQuat*      Orientations
)
{
	const INT BoneCount = Mesh->RefSkeleton.Num();
	const FAnimationMotionChunk* Move = NULL;
	FLOAT Duration = 1.0f;
	FLOAT Frame = 0.0f;

	if
	(
		Mesh->Animation
	&&	SequenceIndex >= 0
	&&	SequenceIndex < Mesh->Animation->Moves.Num()
	&&	SequenceIndex < Mesh->Animation->AnimSeqs.Num()
	)
	{
		Move = &Mesh->Animation->Moves(SequenceIndex);
		const FMeshAnimSeq& Seq = Mesh->Animation->AnimSeqs(SequenceIndex);
		Duration = Move->TrackTime > 0.0f ? Move->TrackTime : Max( (FLOAT)Seq.NumFrames, 1.0f );
		Frame = SkelWrapFrame( Max(NormalizedFrame,0.0f) * Duration, Duration, Loop );
	}

	for( INT BoneIndex=0; BoneIndex<BoneCount; BoneIndex++ )
	{
		const FSkeletalBone& Bone = Mesh->RefSkeleton(BoneIndex);
		Positions[BoneIndex] = Bone.BonePos.Position;
		Orientations[BoneIndex] = Bone.BonePos.Orientation;
		const INT AnimBoneIndex = BoneIndex < Mesh->AnimBoneMap.Num()
			? Mesh->AnimBoneMap(BoneIndex)
			: INDEX_NONE;

		if( Move && AnimBoneIndex >= 0 && AnimBoneIndex < Move->AnimTracks.Num() )
			SkelSampleTrack
			(
				Move->AnimTracks(AnimBoneIndex),
				Frame,
				Duration,
				Loop,
				Positions[BoneIndex],
				Orientations[BoneIndex]
			);
		SkelNormalizeQuat( Orientations[BoneIndex] );
	}
}

static void SkelBuildPose
(
	const USkeletalMesh* Mesh,
	const FVector*      Positions,
	const FAnimationQuat* Orientations,
	FCoords*             PosedBases
)
{
	for( INT BoneIndex=0; BoneIndex<Mesh->RefSkeleton.Num(); BoneIndex++ )
	{
		const FSkeletalBone& Bone = Mesh->RefSkeleton(BoneIndex);
		const FCoords Local = SkelCoordsFromQuat( Orientations[BoneIndex], Positions[BoneIndex] );
		const INT ParentIndex = Bone.ParentIndex;
		PosedBases[BoneIndex] = BoneIndex > 0 && ParentIndex >= 0 && ParentIndex < BoneIndex
			? SkelComposeCoords( PosedBases[ParentIndex], Local )
			: Local;
	}
}

static FCoords SkelApplyPivot( const FCoords& Pivot, const FCoords& Coords )
{
	FCoords Result;
	Result.Origin = Pivot.Origin.TransformVectorBy( Coords ) + Coords.Origin;
	Result.XAxis = Coords.XAxis.TransformVectorBy( Pivot );
	Result.YAxis = Coords.YAxis.TransformVectorBy( Pivot );
	Result.ZAxis = Coords.ZAxis.TransformVectorBy( Pivot );
	return Result;
}

static FCoords SkelBuildClassicWeaponCoords
(
	const FCoords& WeaponCoords,
	const FVector& MeshOrigin,
	const FCoords& MeshCoords
)
{
	const FVector Pivot = (WeaponCoords.Origin - MeshOrigin).TransformPointBy( MeshCoords );
	// Posed bases transform local points using dot products against their rows,
	// exactly as SkelPivotTransformPoint does for the skin. A local unit axis
	// therefore transforms to a COLUMN of that matrix, not one of its rows.
	// Reading the rows inverted the bone rotation and moved the weapon's own
	// grip offset away from the animated hand as the arm turned.
	const FVector WeaponX = SkelPivotTransformVector( WeaponCoords, FVector(1,0,0) );
	const FVector WeaponY = SkelPivotTransformVector( WeaponCoords, FVector(0,1,0) );
	const FVector XPoint = (WeaponCoords.Origin + WeaponX - MeshOrigin).TransformPointBy( MeshCoords );
	const FVector YPoint = (WeaponCoords.Origin + WeaponY - MeshOrigin).TransformPointBy( MeshCoords );

	FCoords Attachment;
	Attachment.Origin = Pivot;
	Attachment.XAxis = (XPoint - Pivot).SafeNormal();
	const FVector CrossAxis = (Attachment.XAxis ^ (YPoint - Pivot)).SafeNormal();
	Attachment.YAxis = CrossAxis * -1.0f;
	Attachment.ZAxis = Attachment.XAxis ^ CrossAxis;
	return GMath.UnitCoords * Attachment;
}

static void SkelSkinVertices
(
	const USkeletalMesh* Mesh,
	FVector*             Destination,
	INT                  VertexCount,
	const FCoords*       PosedBases
)
{
	const INT BoneCount = Mesh->RefSkeleton.Num();
	if
	(
		!BoneCount
	||	Mesh->LocalPoints.Num() != Mesh->BoneInfluences.Num()
	)
	{
		for( INT VertexIndex=0; VertexIndex<VertexCount; VertexIndex++ )
			Destination[VertexIndex] = Mesh->SkeletalPoints(VertexIndex);
		return;
	}

	FLOAT* WeightSums = (FLOAT*)appAlloca( VertexCount * sizeof(FLOAT) );
	appMemzero( WeightSums, VertexCount * sizeof(FLOAT) );
	appMemzero( Destination, VertexCount * sizeof(FVector) );

	const INT IndexedBoneCount = Min( BoneCount, Mesh->BoneInfluenceIndices.Num() );
	for( INT BoneIndex=0; BoneIndex<IndexedBoneCount; BoneIndex++ )
	{
		const FSkeletalBoneInfluenceIndex& Index = Mesh->BoneInfluenceIndices(BoneIndex);
		const INT First = Index.WeightIndex;
		const INT Last = Min( First + (INT)Index.Number, Mesh->BoneInfluences.Num() );
		for( INT InfluenceIndex=First; InfluenceIndex<Last; InfluenceIndex++ )
		{
			const FSkeletalBoneInfluence& Influence = Mesh->BoneInfluences(InfluenceIndex);
			const INT PointIndex = Influence.PointIndex;
			if( PointIndex >= VertexCount )
				continue;
			const FLOAT Weight = Influence.BoneWeight / 65535.0f;
			Destination[PointIndex] += SkelPivotTransformPoint( PosedBases[BoneIndex], Mesh->LocalPoints(InfluenceIndex) ) * Weight;
			WeightSums[PointIndex] += Weight;
		}
	}

	for( INT VertexIndex=0; VertexIndex<VertexCount; VertexIndex++ )
	{
		if( WeightSums[VertexIndex] <= 0.000001f )
			Destination[VertexIndex] = Mesh->SkeletalPoints(VertexIndex);
	}
}

static void SkelAuditLoadedMesh( USkeletalMesh* Mesh, const FCoords* RefBases, INT ExtraWedgeCount, INT ReplacedWedges )
{
#if TARGET_XBOX
	const INT PointCount = Mesh->SkeletalPoints.Num();
	const INT BoneCount = Mesh->RefSkeleton.Num();
	INT InvalidParents = 0;
	INT UnmappedBones = 0;
	INT InvalidInfluenceSpans = 0;
	INT InvalidInfluencePoints = 0;
	INT UnweightedPoints = 0;
	INT WeightMismatchPoints = 0;
	INT InvalidWedges = 0;
	INT InvalidFaces = 0;
	INT InvalidMaterials = 0;
	INT InvalidCollapses = 0;
	INT RadialNegativeFaces = 0;
	INT RadialPositiveFaces = 0;
	INT RadialFlatFaces = 0;
	FLOAT MinWeight = 999999.0f;
	FLOAT MaxWeight = -999999.0f;
	FLOAT MaxBindError = 0.0f;
	FLOAT MaxBasisError = 0.0f;

	for( INT BoneIndex=0; BoneIndex<BoneCount; BoneIndex++ )
	{
		const FSkeletalBone& Bone = Mesh->RefSkeleton(BoneIndex);
		if( BoneIndex > 0 && (Bone.ParentIndex < 0 || Bone.ParentIndex >= BoneIndex) )
			InvalidParents++;
		if( BoneIndex >= Mesh->AnimBoneMap.Num() || Mesh->AnimBoneMap(BoneIndex) == INDEX_NONE )
			UnmappedBones++;

		if( RefBases )
		{
			const FCoords& Basis = RefBases[BoneIndex];
			const FLOAT BasisError = Max
			(
				Max( Abs(Basis.XAxis.SizeSquared()-1.0f), Abs(Basis.YAxis.SizeSquared()-1.0f) ),
				Max
				(
					Abs(Basis.ZAxis.SizeSquared()-1.0f),
					Max( Abs(Basis.XAxis|Basis.YAxis), Max(Abs(Basis.XAxis|Basis.ZAxis),Abs(Basis.YAxis|Basis.ZAxis)) )
				)
			);
			MaxBasisError = Max( MaxBasisError, BasisError );
		}
	}

	FLOAT* WeightSums = PointCount ? (FLOAT*)appAlloca( PointCount * sizeof(FLOAT) ) : NULL;
	if( WeightSums )
		appMemzero( WeightSums, PointCount * sizeof(FLOAT) );
	for( INT BoneIndex=0; BoneIndex<Mesh->BoneInfluenceIndices.Num(); BoneIndex++ )
	{
		const FSkeletalBoneInfluenceIndex& Index = Mesh->BoneInfluenceIndices(BoneIndex);
		const INT First = Index.WeightIndex;
		const INT Count = Index.Number;
		if( First < 0 || First > Mesh->BoneInfluences.Num() || Count < 0 || First + Count > Mesh->BoneInfluences.Num() )
			InvalidInfluenceSpans++;
		const INT Last = Min( Max(First,0) + Max(Count,0), Mesh->BoneInfluences.Num() );
		for( INT InfluenceIndex=Max(First,0); InfluenceIndex<Last; InfluenceIndex++ )
		{
			const FSkeletalBoneInfluence& Influence = Mesh->BoneInfluences(InfluenceIndex);
			if( Influence.PointIndex >= PointCount )
			{
				InvalidInfluencePoints++;
				continue;
			}
			WeightSums[Influence.PointIndex] += Influence.BoneWeight / 65535.0f;
		}
	}
	for( INT PointIndex=0; PointIndex<PointCount; PointIndex++ )
	{
		const FLOAT Weight = WeightSums[PointIndex];
		if( Weight <= 0.000001f )
			UnweightedPoints++;
		else
		{
			MinWeight = Min( MinWeight, Weight );
			MaxWeight = Max( MaxWeight, Weight );
			if( Abs(Weight-1.0f) > 0.02f )
				WeightMismatchPoints++;
		}
	}
	if( MinWeight == 999999.0f )
		MinWeight = 0.0f;
	if( MaxWeight == -999999.0f )
		MaxWeight = 0.0f;

	for( INT WedgeIndex=0; WedgeIndex<Mesh->Wedges.Num(); WedgeIndex++ )
		if( Mesh->Wedges(WedgeIndex).iVertex >= PointCount )
			InvalidWedges++;
	FVector MeshCenter(0,0,0);
	for( INT PointIndex=0; PointIndex<PointCount; PointIndex++ )
		MeshCenter += Mesh->SkeletalPoints(PointIndex);
	if( PointCount )
		MeshCenter /= PointCount;
	for( INT FaceIndex=0; FaceIndex<Mesh->Faces.Num(); FaceIndex++ )
	{
		const FMeshFace& Face = Mesh->Faces(FaceIndex);
		if
		(
			Face.iWedge[0] >= Mesh->Wedges.Num()
		||	Face.iWedge[1] >= Mesh->Wedges.Num()
		||	Face.iWedge[2] >= Mesh->Wedges.Num()
		||	Face.MaterialIndex >= Mesh->Materials.Num()
		)
			InvalidFaces++;
		else
		{
			const INT Vertex0 = Mesh->Wedges(Face.iWedge[0]).iVertex;
			const INT Vertex1 = Mesh->Wedges(Face.iWedge[1]).iVertex;
			const INT Vertex2 = Mesh->Wedges(Face.iWedge[2]).iVertex;
			if( Vertex0 >= PointCount || Vertex1 >= PointCount || Vertex2 >= PointCount )
				continue;
			const FVector& V0 = Mesh->SkeletalPoints(Vertex0);
			const FVector& V1 = Mesh->SkeletalPoints(Vertex1);
			const FVector& V2 = Mesh->SkeletalPoints(Vertex2);
			const FVector FaceNormal = (V0-V1) ^ (V2-V0);
			const FLOAT RadialSign = FaceNormal | ((V0+V1+V2)/3.0f-MeshCenter);
			if( RadialSign < -0.001f )
				RadialNegativeFaces++;
			else if( RadialSign > 0.001f )
				RadialPositiveFaces++;
			else
				RadialFlatFaces++;
		}
	}
	for( INT MaterialIndex=0; MaterialIndex<Mesh->Materials.Num(); MaterialIndex++ )
		if( Mesh->Materials(MaterialIndex).TextureIndex < 0 || Mesh->Materials(MaterialIndex).TextureIndex >= Mesh->Textures.Num() )
			InvalidMaterials++;
	for( INT CollapseIndex=0; CollapseIndex<Mesh->CollapsePointThus.Num(); CollapseIndex++ )
		if( Mesh->CollapsePointThus(CollapseIndex) >= PointCount )
			InvalidCollapses++;

	if( PointCount )
	{
		FVector* BindVerts = (FVector*)appAlloca( PointCount * sizeof(FVector) );
		SkelSkinVertices( Mesh, BindVerts, PointCount, RefBases );
		for( INT PointIndex=0; PointIndex<PointCount; PointIndex++ )
		{
			const FLOAT Error = (BindVerts[PointIndex] - Mesh->SkeletalPoints(PointIndex)).Size();
			MaxBindError = Max( MaxBindError, Error );
		}
	}

	INT TrackCountMismatches = 0;
	INT BoneIndexMapMismatches = 0;
	if( Mesh->Animation )
	{
		for( INT MoveIndex=0; MoveIndex<Mesh->Animation->Moves.Num(); MoveIndex++ )
		{
			const FAnimationMotionChunk& Move = Mesh->Animation->Moves(MoveIndex);
			if( Move.AnimTracks.Num() != Mesh->Animation->RefBones.Num() )
				TrackCountMismatches++;
			if( Move.BoneIndices.Num() && Move.BoneIndices.Num() != Mesh->Animation->RefBones.Num() )
				BoneIndexMapMismatches++;
		}
	}

	debugf
	(
		NAME_Log,
		TEXT("XSKELAUDIT mesh=%s points=%i modelverts=%i special=%i bones=%i mapped=%i badparents=%i influences=%i badspans=%i badpoints=%i unweighted=%i weightmismatch=%i weights=%.4f..%.4f wedges=%i extwedges=%i replaceduv=%i badwedges=%i faces=%i badfaces=%i radialsign=%i/%i/%i materials=%i badmaterials=%i badcollapse=%i moves=%i seqs=%i trackmismatch=%i bonemapmismatch=%i binderror=%.6f basiserror=%.6f"),
		Mesh->GetFullName(),
		PointCount,
		Mesh->ModelVerts,
		Mesh->SpecialVerts,
		BoneCount,
		BoneCount-UnmappedBones,
		InvalidParents,
		Mesh->BoneInfluences.Num(),
		InvalidInfluenceSpans,
		InvalidInfluencePoints,
		UnweightedPoints,
		WeightMismatchPoints,
		MinWeight,
		MaxWeight,
		Mesh->Wedges.Num(),
		ExtraWedgeCount,
		ReplacedWedges,
		InvalidWedges,
		Mesh->Faces.Num(),
		InvalidFaces,
		RadialNegativeFaces,
		RadialPositiveFaces,
		RadialFlatFaces,
		Mesh->Materials.Num(),
		InvalidMaterials,
		InvalidCollapses,
		Mesh->Animation ? Mesh->Animation->Moves.Num() : 0,
		Mesh->AnimSeqs.Num(),
		TrackCountMismatches,
		BoneIndexMapMismatches,
		MaxBindError,
		MaxBasisError
	);
#endif
}

static void SkelAuditRuntimePose
(
	USkeletalMesh*    Mesh,
	AActor*           Owner,
	CFSkeletalHeader* Header,
	FVector*          Verts,
	INT               VertexCount,
	FName             Sequence,
	FLOAT             Frame
)
{
#if TARGET_XBOX
	if( VertexCount <= 0 )
		return;

	FVector PoseMin = Verts[0];
	FVector PoseMax = Verts[0];
	INT NonFinite = 0;
	FLOAT MaxDisplacement = 0.0f;
	for( INT VertexIndex=0; VertexIndex<VertexCount; VertexIndex++ )
	{
		FVector& Vertex = Verts[VertexIndex];
		if( !SkelFiniteVector(Vertex) )
		{
			NonFinite++;
			Vertex = Mesh->SkeletalPoints(VertexIndex);
		}
		PoseMin.X = Min( PoseMin.X, Vertex.X );
		PoseMin.Y = Min( PoseMin.Y, Vertex.Y );
		PoseMin.Z = Min( PoseMin.Z, Vertex.Z );
		PoseMax.X = Max( PoseMax.X, Vertex.X );
		PoseMax.Y = Max( PoseMax.Y, Vertex.Y );
		PoseMax.Z = Max( PoseMax.Z, Vertex.Z );
		MaxDisplacement = Max( MaxDisplacement, (Vertex-Mesh->SkeletalPoints(VertexIndex)).Size() );
	}

	const FVector PoseSize = PoseMax - PoseMin;
	const FVector BindSize = Mesh->SkeletalBindMax - Mesh->SkeletalBindMin;
	const FLOAT MaxPoseAxis = Max( PoseSize.X, Max(PoseSize.Y,PoseSize.Z) );
	const FLOAT MaxBindAxis = Max( BindSize.X, Max(BindSize.Y,BindSize.Z) );
	const UBOOL Exploded = MaxBindAxis > 0.0f && MaxPoseAxis > MaxBindAxis * 4.0f;
	const UBOOL SequenceChanged = Header->AuditSeq != Sequence;
	const UBOOL LodChanged = Header->AuditHasBounds && Header->AuditLodVerts != VertexCount;
	const UBOOL LodOscillation
		= LodChanged
		&& Header->AuditPreviousLodVerts == VertexCount
		&& Header->AuditLodVerts != VertexCount;
	if( LodChanged )
		Header->AuditLodChanges++;
	if( LodOscillation )
		Header->AuditLodOscillations++;

	const INT SampleCount = Min( VertexCount, (INT)SKELETAL_AUDIT_SAMPLES );
	FVector CurrentSamples[SKELETAL_AUDIT_SAMPLES];
	for( INT SampleIndex=0; SampleIndex<SampleCount; SampleIndex++ )
	{
		const INT VertexIndex = SampleCount > 1
			? SampleIndex * (VertexCount-1) / (SampleCount-1)
			: 0;
		CurrentSamples[SampleIndex] = Verts[VertexIndex];
	}

	const INT NormalSampleCount = Min( Mesh->Faces.Num(), (INT)SKELETAL_AUDIT_SAMPLES );
	FVector CurrentNormals[SKELETAL_AUDIT_SAMPLES];
	INT DegenerateFaces = 0;
	for( INT NormalIndex=0; NormalIndex<NormalSampleCount; NormalIndex++ )
	{
		CurrentNormals[NormalIndex] = FVector(0,0,0);
		const INT FaceIndex = NormalSampleCount > 1
			? NormalIndex * (Mesh->Faces.Num()-1) / (NormalSampleCount-1)
			: 0;
		const FMeshFace& Face = Mesh->Faces(FaceIndex);
		if
		(
			Face.iWedge[0] >= Mesh->Wedges.Num()
		||	Face.iWedge[1] >= Mesh->Wedges.Num()
		||	Face.iWedge[2] >= Mesh->Wedges.Num()
		)
			continue;
		const INT Vertex0 = Mesh->Wedges(Face.iWedge[0]).iVertex;
		const INT Vertex1 = Mesh->Wedges(Face.iWedge[1]).iVertex;
		const INT Vertex2 = Mesh->Wedges(Face.iWedge[2]).iVertex;
		if( Vertex0 >= VertexCount || Vertex1 >= VertexCount || Vertex2 >= VertexCount )
			continue;
		FVector Normal = (Verts[Vertex0]-Verts[Vertex1]) ^ (Verts[Vertex2]-Verts[Vertex0]);
		const FLOAT NormalSizeSquared = Normal.SizeSquared();
		if( NormalSizeSquared <= 0.001f )
		{
			DegenerateFaces++;
			continue;
		}
		CurrentNormals[NormalIndex] = Normal * (1.0f / appSqrt(NormalSizeSquared));
	}

	const FLOAT FrameDelta = Frame - Header->AuditFrame;
	const UBOOL ComparablePose
		= Header->AuditHasBounds
		&& !SequenceChanged
		&& !LodChanged
		&& FrameDelta >= 0.0f
		&& FrameDelta <= 0.25f
		&& Header->AuditSampleCount == SampleCount;
	FLOAT MaxStep = 0.0f;
	FLOAT AverageStep = 0.0f;
	INT NormalFlips = 0;
	INT ComparedNormals = 0;
	if( ComparablePose )
	{
		for( INT SampleIndex=0; SampleIndex<SampleCount; SampleIndex++ )
		{
			const FLOAT Step = (CurrentSamples[SampleIndex]-Header->AuditSamples[SampleIndex]).Size();
			MaxStep = Max( MaxStep, Step );
			AverageStep += Step;
		}
		if( SampleCount )
			AverageStep /= SampleCount;

		const INT CompareNormalCount = Min( NormalSampleCount, Header->AuditNormalSampleCount );
		for( INT NormalIndex=0; NormalIndex<CompareNormalCount; NormalIndex++ )
		{
			const FVector& PreviousNormal = Header->AuditNormals[NormalIndex];
			const FVector& CurrentNormal = CurrentNormals[NormalIndex];
			if( PreviousNormal.SizeSquared() > 0.5f && CurrentNormal.SizeSquared() > 0.5f )
			{
				ComparedNormals++;
				if( (PreviousNormal|CurrentNormal) < -0.25f )
					NormalFlips++;
			}
		}
	}

	const FLOAT StepThreshold = MaxBindAxis * (0.35f + 4.0f*Clamp(FrameDelta,0.0f,0.25f));
	const UBOOL TransitionPose = Frame < 0.0f || Header->AuditFrame < 0.0f;
	const UBOOL LargePoseJump
		= ComparablePose
		&& !TransitionPose
		&& MaxBindAxis > 0.0f
		&& MaxStep > StepThreshold;
	const UBOOL NormalFlipBurst
		= ComparablePose
		&& !TransitionPose
		&& FrameDelta <= 0.10f
		&& NormalFlips >= Max( 2, ComparedNormals/4 );
	const UBOOL DegenerateBurst
		= ComparablePose
		&& !TransitionPose
		&& DegenerateFaces >= 3
		&& DegenerateFaces > Header->AuditDegenerateFaces + 1;
	const UBOOL Flicker = LargePoseJump || NormalFlipBurst || DegenerateBurst || LodOscillation;

	if( Flicker )
	{
		debugf
		(
			NAME_Warning,
			TEXT("XSKELFLICKER kind=pose mesh=%s owner=%s seq=%s frame=%.4f->%.4f lod=%i->%i maxstep=%.3f threshold=%.3f avgstep=%.3f normalflips=%i/%i degenerate=%i->%i lodosc=%i"),
			Mesh->GetFullName(),
			Owner ? Owner->GetFullName() : TEXT("None"),
			*Sequence,
			Header->AuditFrame,
			Frame,
			Header->AuditLodVerts,
			VertexCount,
			MaxStep,
			StepThreshold,
			AverageStep,
			NormalFlips,
			ComparedNormals,
			Header->AuditDegenerateFaces,
			DegenerateFaces,
			LodOscillation
		);
	}

	if( SequenceChanged || NonFinite || Exploded || Flicker || (Header->AuditPoseCount && Header->AuditPoseCount%120 == 0) )
	{
		debugf
		(
			NonFinite || Exploded ? NAME_Warning : NAME_Log,
			TEXT("XSKELPOSE mesh=%s owner=%s seq=%s frame=%.4f lodverts=%i bounds=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) maxdisp=%.2f maxstep=%.3f normalflips=%i/%i degenerate=%i lodchanges=%i lodosc=%i nonfinite=%i exploded=%i"),
			Mesh->GetFullName(),
			Owner ? Owner->GetFullName() : TEXT("None"),
			*Sequence,
			Frame,
			VertexCount,
			PoseMin.X, PoseMin.Y, PoseMin.Z,
			PoseMax.X, PoseMax.Y, PoseMax.Z,
			MaxDisplacement,
			MaxStep,
			NormalFlips,
			ComparedNormals,
			DegenerateFaces,
			Header->AuditLodChanges,
			Header->AuditLodOscillations,
			NonFinite,
			Exploded
		);
	}

	Header->AuditPreviousLodVerts = Header->AuditLodVerts;
	Header->AuditLodVerts = VertexCount;
	Header->AuditSeq = Sequence;
	Header->AuditFrame = Frame;
	Header->AuditMin = PoseMin;
	Header->AuditMax = PoseMax;
	Header->AuditSampleCount = SampleCount;
	for( INT SampleIndex=0; SampleIndex<SampleCount; SampleIndex++ )
		Header->AuditSamples[SampleIndex] = CurrentSamples[SampleIndex];
	Header->AuditNormalSampleCount = NormalSampleCount;
	for( INT NormalIndex=0; NormalIndex<NormalSampleCount; NormalIndex++ )
		Header->AuditNormals[NormalIndex] = CurrentNormals[NormalIndex];
	Header->AuditDegenerateFaces = DegenerateFaces;
	Header->AuditPoseCount++;
	Header->AuditHasBounds = 1;
#endif
}

IMPLEMENT_CLASS(USkeletalMesh);

void USkeletalMesh::Serialize( FArchive& Ar )
{
	guard(USkeletalMesh::Serialize);

	if( Ar.IsLoading() )
		debugf( NAME_Log, TEXT("XSkeletal load begin %s"), GetFullName() );

	Super::Serialize( Ar );

	TArray<FSkeletalExtWedge> ExtraWedges;
	Ar << ExtraWedges;
	Ar << SkeletalPoints;
	Ar << RefSkeleton;
	Ar << BoneInfluenceIndices;
	Ar << BoneInfluences;
	Ar << LocalPoints;
	Ar << SkeletalDepth;
	Ar << Animation;
	Ar << WeaponBoneIndex;
	Ar << WeaponAdjust;

	INT ReplacedWedges = 0;
	if( Ar.IsLoading() && ExtraWedges.Num() == Wedges.Num() && ExtraWedges.Num() )
	{
		FLOAT MinU = ExtraWedges(0).U;
		FLOAT MaxU = ExtraWedges(0).U;
		FLOAT MinV = ExtraWedges(0).V;
		FLOAT MaxV = ExtraWedges(0).V;
		for( INT WedgeIndex=1; WedgeIndex<ExtraWedges.Num(); WedgeIndex++ )
		{
			MinU = Min( MinU, ExtraWedges(WedgeIndex).U );
			MaxU = Max( MaxU, ExtraWedges(WedgeIndex).U );
			MinV = Min( MinV, ExtraWedges(WedgeIndex).V );
			MaxV = Max( MaxV, ExtraWedges(WedgeIndex).V );
		}
		const UBOOL NormalizedUVs = Max( Max(Abs(MinU),Abs(MaxU)), Max(Abs(MinV),Abs(MaxV)) ) <= 2.0f;
		const FLOAT UVScale = NormalizedUVs ? 255.0f : 1.0f;
		for( INT WedgeIndex=0; WedgeIndex<ExtraWedges.Num(); WedgeIndex++ )
		{
			const FSkeletalExtWedge& Source = ExtraWedges(WedgeIndex);
			FMeshWedge& Destination = Wedges(WedgeIndex);
			const BYTE U = (BYTE)Clamp( appRound(Source.U * UVScale), 0, 255 );
			const BYTE V = (BYTE)Clamp( appRound(Source.V * UVScale), 0, 255 );
			if( Destination.iVertex != Source.iVertex || Destination.TexUV.U != U || Destination.TexUV.V != V )
				ReplacedWedges++;
			Destination.iVertex = Source.iVertex;
			Destination.TexUV.U = U;
			Destination.TexUV.V = V;
		}
#if TARGET_XBOX
		if( SkelAuditEnabled() )
			debugf
			(
				NAME_Log,
				TEXT("XSKELAUDIT uv mesh=%s extwedges=%i range=(%.4f..%.4f,%.4f..%.4f) scale=%.1f replaced=%i"),
				GetFullName(),
				ExtraWedges.Num(),
				MinU, MaxU, MinV, MaxV,
				UVScale,
				ReplacedWedges
			);
#endif
	}

	if( Ar.IsLoading() && Animation )
		Ar.Preload( Animation );

	if( Ar.IsLoading() && Animation && Animation->AnimSeqs.Num() )
		AnimSeqs = Animation->AnimSeqs;

	if( Ar.IsLoading() )
	{
		const INT SerializedYaw = RotOrigin.Yaw;

#if TARGET_XBOX
		if( SkelAuditEnabled() )
			debugf
			(
				NAME_Log,
				TEXT("XSKELAUDIT basis mesh=%s points=%i localpoints=%i faces=%i bones=%i yaw=%i->%i conversion=native-ue1-pose winding=serialized"),
				GetFullName(),
				SkeletalPoints.Num(),
				LocalPoints.Num(),
				Faces.Num(),
				RefSkeleton.Num(),
				SerializedYaw,
				RotOrigin.Yaw
			);
#endif
	}

	if( Ar.IsLoading() && SkeletalPoints.Num() )
	{
		Verts.Load();
		Verts.Empty();
		Verts.Add( SkeletalPoints.Num() );
		for( INT PointIndex=0; PointIndex<SkeletalPoints.Num(); PointIndex++ )
			Verts(PointIndex) = FMeshVert( SkeletalPoints(PointIndex) );

		FrameVerts = SkeletalPoints.Num();
		AnimFrames = 1;
		ModelVerts = Min( ModelVerts, FrameVerts );

		SkeletalBindMin = SkeletalPoints(0);
		SkeletalBindMax = SkeletalPoints(0);
		for( INT PointIndex=1; PointIndex<SkeletalPoints.Num(); PointIndex++ )
		{
			const FVector& Point = SkeletalPoints(PointIndex);
			SkeletalBindMin.X = Min( SkeletalBindMin.X, Point.X );
			SkeletalBindMin.Y = Min( SkeletalBindMin.Y, Point.Y );
			SkeletalBindMin.Z = Min( SkeletalBindMin.Z, Point.Z );
			SkeletalBindMax.X = Max( SkeletalBindMax.X, Point.X );
			SkeletalBindMax.Y = Max( SkeletalBindMax.Y, Point.Y );
			SkeletalBindMax.Z = Max( SkeletalBindMax.Z, Point.Z );
		}
	}

	if( Ar.IsLoading() )
	{
		const INT BoneCount = RefSkeleton.Num();
		InvRefBases.Empty();
		InvRefBases.Add( BoneCount );
		AnimBoneMap.Empty();
		AnimBoneMap.Add( BoneCount );
		FCoords* RefBases = BoneCount ? (FCoords*)appAlloca( BoneCount * sizeof(FCoords) ) : NULL;

		for( INT BoneIndex=0; BoneIndex<BoneCount; BoneIndex++ )
		{
			const FSkeletalBone& Bone = RefSkeleton(BoneIndex);
			FVector RefPosition = Bone.BonePos.Position;
			FAnimationQuat RefOrientation = Bone.BonePos.Orientation;
			const FCoords Local = SkelCoordsFromQuat( RefOrientation, RefPosition );
			const INT ParentIndex = Bone.ParentIndex;
			RefBases[BoneIndex] = BoneIndex > 0 && ParentIndex >= 0 && ParentIndex < BoneIndex
				? SkelComposeCoords( RefBases[ParentIndex], Local )
				: Local;
			InvRefBases(BoneIndex) = SkelPivotInverse( RefBases[BoneIndex] );

			AnimBoneMap(BoneIndex) = INDEX_NONE;
			if( Animation )
				for( INT AnimBoneIndex=0; AnimBoneIndex<Animation->RefBones.Num(); AnimBoneIndex++ )
					if( Animation->RefBones(AnimBoneIndex).Name == Bone.Name )
					{
						AnimBoneMap(BoneIndex) = AnimBoneIndex;
						break;
				}
		}

#if TARGET_XBOX
		if( SkelAuditEnabled() )
#endif
			SkelAuditLoadedMesh( this, RefBases, ExtraWedges.Num(), ReplacedWedges );

		debugf
		(
			NAME_Log,
			TEXT("XSkeletal ready %s points=%i bones=%i influences=%i anim=%s moves=%i seqs=%i"),
			GetFullName(),
			SkeletalPoints.Num(),
			RefSkeleton.Num(),
			BoneInfluences.Num(),
			Animation ? Animation->GetFullName() : TEXT("None"),
			Animation ? Animation->Moves.Num() : 0,
			AnimSeqs.Num()
		);
#if TARGET_XBOX
		if( SkelAuditEnabled() )
			debugf
			(
				NAME_Log,
				TEXT("XSKELATTACH mesh=%s bone=%i valid=%i adjustOrigin=(%.3f,%.3f,%.3f) adjustX=(%.3f,%.3f,%.3f) adjustY=(%.3f,%.3f,%.3f) adjustZ=(%.3f,%.3f,%.3f)"),
				GetFullName(),
				WeaponBoneIndex,
				WeaponBoneIndex >= 0 && WeaponBoneIndex < RefSkeleton.Num(),
				WeaponAdjust.Origin.X, WeaponAdjust.Origin.Y, WeaponAdjust.Origin.Z,
				WeaponAdjust.XAxis.X, WeaponAdjust.XAxis.Y, WeaponAdjust.XAxis.Z,
				WeaponAdjust.YAxis.X, WeaponAdjust.YAxis.Y, WeaponAdjust.YAxis.Z,
				WeaponAdjust.ZAxis.X, WeaponAdjust.ZAxis.Y, WeaponAdjust.ZAxis.Z
			);
#endif
	}

	unguardobj;
}

FBox USkeletalMesh::GetRenderBoundingBox( const AActor* Owner, UBOOL Exact )
{
	guard(USkeletalMesh::GetRenderBoundingBox);

	const FLOAT DrawScale = Owner->bParticles ? 1.5f : Owner->DrawScale;
	FBox Bound
	(
		Scale * DrawScale * (BoundingBox.Min - Origin),
		Scale * DrawScale * (BoundingBox.Max - Origin)
	);
	Bound = Bound.ExpandBy( 1.0f );

	FCoords Coords = GMath.UnitCoords / RotOrigin / Owner->Rotation;
	Coords.Origin = Owner->Location + Owner->PrePivot;
	return Bound.TransformBy( Coords.Transpose() );

	unguardobj;
}

void USkeletalMesh::GetFrame
(
	FVector* ResultVerts,
	INT      Size,
	FCoords  Coords,
	AActor*  Owner
)
{
	INT LODRequest = ModelVerts;
	GetFrame( ResultVerts, Size, Coords, Owner, LODRequest );
}

void USkeletalMesh::GetFrame
(
	FVector* ResultVerts,
	INT      Size,
	FCoords  Coords,
	AActor*  Owner,
	INT&     LODRequest
)
{
	guard(USkeletalMesh::GetFrame);

	if( !SkeletalPoints.Num() || SkeletalPoints.Num() < FrameVerts )
	{
		ULodMesh::GetFrame( ResultVerts, Size, Coords, Owner, LODRequest );
		return;
	}

	AActor* AnimOwner = Owner->bAnimByOwner && Owner->Owner ? Owner->Owner : Owner;
#if TARGET_XBOX
	if( SkelAuditEnabled() )
	{
		APawn* PlaybackPawn = Cast<APawn>(AnimOwner);
		static INT LastPlaybackLogTick = -1;
		if( PlaybackPawn && PlaybackPawn->bViewTarget && (INT)GTicks-LastPlaybackLogTick >= 15 )
		{
			const FMeshAnimSeq* Playback = GetAnimSeq(AnimOwner->AnimSequence);
			debugf(NAME_Log,TEXT("XSKELPLAY tick=%i actor=%s seq=%s frame=%.4f normRate=%.4f numframes=%i seqRate=%.4f loop=%i"),
				(INT)GTicks,AnimOwner->GetFullName(),*AnimOwner->AnimSequence,AnimOwner->AnimFrame,
				AnimOwner->AnimRate,Playback ? Playback->NumFrames : 0,Playback ? Playback->Rate : 0.0f,(INT)AnimOwner->bAnimLoop);
			LastPlaybackLogTick = (INT)GTicks;
		}
	}
#endif
	const INT VertsRequested = Min( LODRequest + SpecialVerts, FrameVerts );
	INT VertexCount = VertsRequested;
	LODRequest = Max( 0, VertexCount - SpecialVerts );
	const INT BoneCount = RefSkeleton.Num();

	FCacheItem* Item = NULL;
	const QWORD CacheID = MakeCacheID( CID_TweenAnim, Owner, NULL );
	BYTE* Mem = GCache.Get( CacheID, Item );
	CFSkeletalHeader* Header = (CFSkeletalHeader*)Mem;
	if( !Mem || Header->CachedMesh != this )
	{
		if( Mem )
		{
			Item->Unlock();
			GCache.Flush( CacheID );
		}
		Mem = GCache.Create
		(
			CacheID,
			Item,
			sizeof(CFSkeletalHeader)
			+ FrameVerts * sizeof(FVector)
			+ BoneCount * sizeof(FVector)
			+ BoneCount * sizeof(FAnimationQuat)
			+ BoneCount * sizeof(FCoords)
		);
		Header = (CFSkeletalHeader*)Mem;
		Header->CachedMesh = this;
		Header->CachedFrame = 0.0f;
		Header->CachedSeq = NAME_None;
		Header->CachedLodVerts = 0;
		Header->CachedBonesValid = 0;
		Header->CachedAnimOwner = NULL;
		Header->CachedAnimation = NULL;
		Header->CachedLoop = 0;
		Header->AuditSeq = NAME_None;
		Header->AuditFrame = 0.0f;
		Header->AuditMin = FVector(0,0,0);
		Header->AuditMax = FVector(0,0,0);
		Header->AuditPoseCount = 0;
		Header->AuditLodVerts = 0;
		Header->AuditPreviousLodVerts = 0;
		Header->AuditLodChanges = 0;
		Header->AuditLodOscillations = 0;
		Header->AuditSampleCount = 0;
		Header->AuditNormalSampleCount = 0;
		Header->AuditDegenerateFaces = 0;
		Header->AuditHasBounds = 0;
	}

	FVector* CachedVerts = (FVector*)(Mem + sizeof(CFSkeletalHeader));
	FVector* CachedPositions = CachedVerts + FrameVerts;
	FAnimationQuat* CachedOrientations = (FAnimationQuat*)(CachedPositions + BoneCount);
	FCoords* PosedBases = (FCoords*)(CachedOrientations + BoneCount);

	// Local-space vertices and bone bases do not depend on the viewing player.
	// Retain both in the existing per-owner cache; viewport transforms and the
	// weapon attachment transform below still run for every view. A closer
	// view may request more LOD vertices and must populate those before reuse.
	if( BoneCount && (!Header->CachedBonesValid
		|| Header->CachedAnimOwner != AnimOwner
		|| Header->CachedAnimation != Animation
		|| Header->CachedLoop != (UBOOL)AnimOwner->bAnimLoop
		|| Header->CachedSeq != AnimOwner->AnimSequence
		|| Header->CachedFrame != AnimOwner->AnimFrame
		|| Header->CachedLodVerts < VertexCount) )
	{
		const INT SequenceIndex = SkelFindSequenceIndex( Animation, AnimOwner->AnimSequence );
		const FMeshAnimSeq* Seq = GetAnimSeq( AnimOwner->AnimSequence );
		if( AnimOwner->AnimFrame >= 0.0f || !Header->CachedBonesValid )
		{
			SkelSampleLocalPose
			(
				this,
				SequenceIndex,
				Max(AnimOwner->AnimFrame,0.0f),
				AnimOwner->bAnimLoop,
				CachedPositions,
				CachedOrientations
			);
			Header->CachedBonesValid = 1;
			Header->CachedFrame = AnimOwner->AnimFrame;
			Header->CachedSeq = AnimOwner->AnimSequence;
		}
		else
		{
			FVector* TargetPositions = (FVector*)appAlloca( BoneCount * sizeof(FVector) );
			FAnimationQuat* TargetOrientations
				= (FAnimationQuat*)appAlloca( BoneCount * sizeof(FAnimationQuat) );
			SkelSampleLocalPose
			(
				this,
				SequenceIndex,
				0.0f,
				AnimOwner->bAnimLoop,
				TargetPositions,
				TargetOrientations
			);

			const FLOAT StartFrame = Seq && Seq->NumFrames > 0
				? -1.0f / Seq->NumFrames
				: 0.0f;
			FLOAT Alpha = Header->CachedFrame != 0.0f
				? 1.0f - AnimOwner->AnimFrame / Header->CachedFrame
				: 0.0f;
			if
			(
				Header->CachedSeq != AnimOwner->AnimSequence
			||	Alpha < 0.0f
			||	Alpha > 1.0f
			)
			{
				Header->CachedFrame = StartFrame;
				Header->CachedSeq = AnimOwner->AnimSequence;
				Alpha = 0.0f;
			}

			if( Alpha > 0.0f )
			{
				for( INT BoneIndex=0; BoneIndex<BoneCount; BoneIndex++ )
				{
					CachedPositions[BoneIndex]
						+= (TargetPositions[BoneIndex] - CachedPositions[BoneIndex]) * Alpha;
					CachedOrientations[BoneIndex] = SkelSlerpQuat
					(
						CachedOrientations[BoneIndex],
						TargetOrientations[BoneIndex],
						Alpha
					);
				}
			}
			Header->CachedFrame = AnimOwner->AnimFrame;
		}

		SkelBuildPose( this, CachedPositions, CachedOrientations, PosedBases );
		SkelSkinVertices( this, CachedVerts, VertexCount, PosedBases );
		Header->CachedLodVerts = VertexCount;
		Header->CachedAnimOwner = AnimOwner;
		Header->CachedAnimation = Animation;
		Header->CachedLoop = AnimOwner->bAnimLoop;
	}
	else if( !BoneCount )
	{
		for( INT VertexIndex=0; VertexIndex<VertexCount; VertexIndex++ )
			CachedVerts[VertexIndex] = SkeletalPoints(VertexIndex);
	}

#if TARGET_XBOX
	if( SkelAuditEnabled() )
#endif
		SkelAuditRuntimePose
		(
			this,
			AnimOwner,
			Header,
			CachedVerts,
			VertexCount,
			AnimOwner->AnimSequence,
			AnimOwner->AnimFrame
		);

	const FLOAT DrawScale = AnimOwner->bParticles ? 1.0f : Owner->DrawScale;
	FVector SkeletalScale = Scale * DrawScale;
	// PS2 skeletal imports use the opposite Y handedness. Keep this correction
	// local to USkeletalMesh; stock ULodMesh geometry must retain native winding.
	SkeletalScale.Y *= -1.0f;
	Coords = Coords * (Owner->Location + Owner->PrePivot) * Owner->Rotation * RotOrigin * FScale(SkeletalScale,0.0,SHEER_None);

	if( WeaponBoneIndex >= 0 && WeaponBoneIndex < RefSkeleton.Num() )
	{
		const FCoords AdjustedWeaponBone = SkelApplyPivot( WeaponAdjust, PosedBases[WeaponBoneIndex] );
		ClassicWeaponCoords = SkelBuildClassicWeaponCoords( AdjustedWeaponBone, Origin, Coords );
#if TARGET_XBOX
		if( SkelAuditEnabled() && Header->AuditPoseCount%120 == 1 )
		{
			const FCoords WeaponPivot = ClassicWeaponCoords.Inverse();
			const FVector SocketX = SkelPivotTransformVector(AdjustedWeaponBone,FVector(1,0,0)).TransformVectorBy(Coords).SafeNormal();
			const FVector SocketY = SkelPivotTransformVector(AdjustedWeaponBone,FVector(0,1,0)).TransformVectorBy(Coords).SafeNormal();
			const FVector SocketZ = SkelPivotTransformVector(AdjustedWeaponBone,FVector(0,0,1)).TransformVectorBy(Coords).SafeNormal();
			debugf
			(
				NAME_Log,
				TEXT("XSKELATTACHPOSE mesh=%s owner=%s bone=%i seq=%s frame=%.4f pivot=(%.2f,%.2f,%.2f)"),
				GetFullName(),
				Owner->GetFullName(),
				WeaponBoneIndex,
				*AnimOwner->AnimSequence,
				AnimOwner->AnimFrame,
				WeaponPivot.Origin.X,
				WeaponPivot.Origin.Y,
				WeaponPivot.Origin.Z
			);
			debugf(NAME_Log,TEXT("XSKELSOCKET axes camera x=(%.3f,%.3f,%.3f) y=(%.3f,%.3f,%.3f) z=(%.3f,%.3f,%.3f)"),
				SocketX.X,SocketX.Y,SocketX.Z,SocketY.X,SocketY.Y,SocketY.Z,SocketZ.X,SocketZ.Y,SocketZ.Z);
		}
#endif
	}

#if TARGET_XBOX
	if( SkelAuditEnabled() && Owner->IsA(AWeapon::StaticClass()) && BoneCount && Header->AuditPoseCount%120 == 1 )
	{
		for( INT BoneIndex=0; BoneIndex<BoneCount; BoneIndex++ )
		{
			if( appStricmp(*RefSkeleton(BoneIndex).Name,TEXT("Bone_Flash")) == 0 )
			{
				const FVector Muzzle = (PosedBases[BoneIndex].Origin-Origin).TransformPointBy(Coords);
				const FVector Root = (PosedBases[0].Origin-Origin).TransformPointBy(Coords);
				const FVector Direction = (Muzzle-Root).SafeNormal();
				debugf(NAME_Log,TEXT("XSKELMUZZLE mesh=%s seq=%s directionCamera=(%.3f,%.3f,%.3f) root=(%.2f,%.2f,%.2f) muzzle=(%.2f,%.2f,%.2f)"),
					GetFullName(),*AnimOwner->AnimSequence,Direction.X,Direction.Y,Direction.Z,
					Root.X,Root.Y,Root.Z,Muzzle.X,Muzzle.Y,Muzzle.Z);
				break;
			}
		}
	}
#endif
	for( INT VertexIndex=0; VertexIndex<VertexCount; VertexIndex++ )
	{
		*ResultVerts = (CachedVerts[VertexIndex] - Origin).TransformPointBy( Coords );
		*(BYTE**)&ResultVerts += Size;
	}

	Item->Unlock();
	unguardobj;
}
