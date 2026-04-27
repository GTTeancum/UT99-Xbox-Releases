/*=============================================================================
	APlayerPawn.h: A player pawn.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// AActor interface.
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, INT NumReps );

	// APlayerPawn interface.
	void SetPlayer( UPlayer* Player );
	void PostNetReceive();

	// Forced crosshair scaling
#if OLDUNREAL_USCRIPT_HOOKING
	void OverrideDrawCrosshair(BOOL Enable);
	void ResetDrawCrosshair();
#endif

	// PlayerPawn Render Control Interface (RCI).
	// Override the operations in the game-specific PlayerPawn class to control rendering
	virtual bool ClearScreen();
	virtual bool RecomputeLighting();
	virtual bool CanSee( const AActor* Actor );
	virtual INT GetViewZone( INT iViewZone, const UModel* Model );
	virtual bool IsZoneVisible( INT iZone );
	virtual bool IsSurfVisible( const FBspNode* Node, INT iZone, const FBspSurf* Poly );
	virtual bool IsActorVisible( const AActor* Actor );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
