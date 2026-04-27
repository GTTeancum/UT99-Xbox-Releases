/*=============================================================================
	ALevelInfo.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	ALevelInfo() {}

	// AActor interface.
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, INT NumReps );

	// ALevelInfo interface
	void RemoveNavigationPoint( ANavigationPoint* Other);
	void RemovePawn( APawn* Other);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
