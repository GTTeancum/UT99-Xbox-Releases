/*=============================================================================
	UnAnimationStub.cpp: Minimal native stub for Engine.Animation.

	The shipped UT99 binary Engine.u was built against a source tree that
	included the licensee-only skeletal animation classes (UAnimation lives
	with USkeletalMesh in the unreleased UE1 source — see SurrealEngine's
	UMesh.h for a reference layout: RefBones[] + Moves[]).  Those classes
	were stripped from the UT99 v1.40 public source release, but Engine.u
	still has `Class Engine.Animation` in its import table (verified in
	hardware boot log import[45]).  Without a registered native UAnimation
	class, ULinkerLoad::VerifyImport falls through to appThrowf("FailedImport")
	and the engine fails to load Engine.u.

	This file provides only what the import resolver needs: a UClass named
	"Animation" in the Engine package with flags RF_Public|RF_Native|RF_Transient.
	Nothing instantiates or serializes UAnimation in our scripted code path,
	so an empty body is sufficient.  If a subsequent import or serialization
	attempts to read UAnimation instance data, that will surface as a new,
	specific failure in the boot log and can be addressed then.
=============================================================================*/

#include "EnginePrivate.h"

class UAnimation : public UObject
{
	DECLARE_CLASS(UAnimation,UObject,0)
	UAnimation() {}
};

IMPLEMENT_CLASS(UAnimation);
