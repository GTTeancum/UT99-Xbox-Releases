/*=============================================================================
	USkeletalMesh.cpp: UE1 skeletal mesh compatibility for the Xbox port.
=============================================================================*/

#include "EnginePrivate.h"

struct FSkeletalQuat
{
	FLOAT X, Y, Z, W;
	friend FArchive& operator<<( FArchive& Ar, FSkeletalQuat& Q )
	{
		return Ar << Q.X << Q.Y << Q.Z << Q.W;
	}
};

struct FSkeletalJointPos
{
	FSkeletalQuat Orientation;
	FVector       Position;
	FLOAT         Length;
	FVector       Size;

	friend FArchive& operator<<( FArchive& Ar, FSkeletalJointPos& P )
	{
		return Ar << P.Orientation << P.Position << P.Length << P.Size;
	}
};

struct FSkeletalBone
{
	FName              Name;
	DWORD              Flags;
	FSkeletalJointPos  BonePos;
	INT                NumChildren;
	INT                ParentIndex;

	friend FArchive& operator<<( FArchive& Ar, FSkeletalBone& B )
	{
		return Ar << B.Name << B.Flags << B.BonePos << B.NumChildren << B.ParentIndex;
	}
};

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

struct FSkeletalBoneInfluenceIndex
{
	_WORD WeightIndex;
	_WORD Number;
	_WORD DetailA;
	_WORD DetailB;

	friend FArchive& operator<<( FArchive& Ar, FSkeletalBoneInfluenceIndex& I )
	{
		return Ar << I.WeightIndex << I.Number << I.DetailA << I.DetailB;
	}
};

struct FSkeletalBoneInfluence
{
	_WORD PointIndex;
	_WORD BoneWeight;

	friend FArchive& operator<<( FArchive& Ar, FSkeletalBoneInfluence& I )
	{
		return Ar << I.PointIndex << I.BoneWeight;
	}
};

static FArchive& operator<<( FArchive& Ar, FCoords& C )
{
	return Ar << C.Origin << C.XAxis << C.YAxis << C.ZAxis;
}

IMPLEMENT_CLASS(USkeletalMesh);

void USkeletalMesh::Serialize( FArchive& Ar )
{
	guard(USkeletalMesh::Serialize);

	if( Ar.IsLoading() )
		debugf( NAME_Log, TEXT("XSkeletal load begin %s"), GetFullName() );

	Super::Serialize( Ar );

	if( Ar.IsLoading() )
		debugf
		(
			NAME_Log,
			TEXT("XSkeletal after lod %s frameVerts=%i animFrames=%i modelVerts=%i faces=%i wedges=%i"),
			GetFullName(),
			FrameVerts,
			AnimFrames,
			ModelVerts,
			Faces.Num(),
			Wedges.Num()
		);

	TArray<FSkeletalExtWedge>            ExtraWedges;
	TArray<FVector>                      Points;
	TArray<FSkeletalBone>                RefSkeleton;
	TArray<FSkeletalBoneInfluenceIndex>  BoneInfluenceIndices;
	TArray<FSkeletalBoneInfluence>       BoneInfluences;
	TArray<FVector>                      LocalPoints;
	INT                                  SkeletalDepth;
	UObject*                             Animation;
	INT                                  WeaponBoneIndex;
	FCoords                              WeaponAdjust;

	Ar << ExtraWedges;
	Ar << Points;
	Ar << RefSkeleton;
	Ar << BoneInfluenceIndices;
	Ar << BoneInfluences;
	Ar << LocalPoints;
	Ar << SkeletalDepth;
	Ar << Animation;
	Ar << WeaponBoneIndex;
	Ar << WeaponAdjust;

	if( Ar.IsLoading() )
		debugf
		(
			NAME_Log,
			TEXT("XSkeletal tail %s extraWedges=%i points=%i bones=%i influences=%i localPoints=%i depth=%i anim=%s weaponBone=%i"),
			GetFullName(),
			ExtraWedges.Num(),
			Points.Num(),
			RefSkeleton.Num(),
			BoneInfluences.Num(),
			LocalPoints.Num(),
			SkeletalDepth,
			Animation ? Animation->GetFullName() : TEXT("None"),
			WeaponBoneIndex
		);

	if( Ar.IsLoading() && Animation && Animation->IsA(UAnimation::StaticClass()) )
	{
		Ar.Preload( Animation );
		UAnimation* MeshAnimation = (UAnimation*)Animation;
		if( MeshAnimation->AnimSeqs.Num() )
		{
			AnimSeqs = MeshAnimation->AnimSeqs;
			for( INT i=0; i<AnimSeqs.Num(); i++ )
			{
				AnimSeqs(i).StartFrame = 0;
				AnimSeqs(i).NumFrames  = 1;
			}
			debugf
			(
				NAME_Log,
				TEXT("XSkeletal animseqs %s animation=%s seqs=%i"),
				GetFullName(),
				Animation->GetFullName(),
				AnimSeqs.Num()
			);
		}
	}

	if( Ar.IsLoading() && Points.Num() )
	{
		Verts.Load();
		Verts.Empty();
		Verts.Add( Points.Num() );
		for( INT i=0; i<Points.Num(); i++ )
			Verts(i) = FMeshVert( Points(i) );

		FrameVerts = Points.Num();
		AnimFrames = 1;
		ModelVerts = Min( ModelVerts, FrameVerts );

		debugf
		(
			NAME_Log,
			TEXT("XSkeletal bindpose %s frameVerts=%i animFrames=%i modelVerts=%i verts=%i"),
			GetFullName(),
			FrameVerts,
			AnimFrames,
			ModelVerts,
			Verts.Num()
		);
	}

	unguardobj;
}
