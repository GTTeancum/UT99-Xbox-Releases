/*=============================================================================
	UnAnimationStub.cpp: Minimal UAnimation native class stub.

	v436 GOTY's Engine.u imports `Engine.Animation` as the PropertyClass of
	AActor::SkelAnim. v436 binary contains no Animation class export, so the
	import only resolves if a native UAnimation is registered with the engine.
	This stub provides that registration. The class is otherwise inert — no
	fields, no methods. UAnimation* references will resolve to a valid UClass*
	at link time; tagged-property serialization succeeds; nothing else uses it.

	Was originally added in the v469 migration commit (`9bf1b7e`); kept here
	after the migration revert because v436 also references the import.
=============================================================================*/

#include "EnginePrivate.h"

class ENGINE_API UAnimation : public UObject
{
	DECLARE_CLASS(UAnimation,UObject,0)
	NO_DEFAULT_CONSTRUCTOR(UAnimation)
};

IMPLEMENT_CLASS(UAnimation);
