/*=============================================================================
	Engine.cpp: Unreal engine package.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

// Global subsystems in the engine.
ENGINE_API FMemStack			GEngineMem;
ENGINE_API FMemCache			GCache;

/*-----------------------------------------------------------------------------
	Package implementation.
-----------------------------------------------------------------------------*/

// On Xbox static lib builds, GPackage is already defined by Core.
// Redefine IMPLEMENT_PACKAGE to skip GPackage for Engine.
#undef IMPLEMENT_PACKAGE
#define IMPLEMENT_PACKAGE(pkg) IMPLEMENT_PACKAGE_PLATFORM(pkg)
IMPLEMENT_PACKAGE(Engine);

/*-----------------------------------------------------------------------------
	The end.
-----------------------------------------------------------------------------*/