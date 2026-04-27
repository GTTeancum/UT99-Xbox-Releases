/*=============================================================================
	AInternetInfo.h: Class functions residing in the AInternetInfo class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
#if OLDUNREAL_BINARY_COMPAT
	NO_DEFAULT_CONSTRUCTOR(AInternetInfo)
#else
	AInternetInfo() {}
#endif

	// AInternetInfo interface.
	virtual void Destroyed() {}; //TODO: Move to AActor interface

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
