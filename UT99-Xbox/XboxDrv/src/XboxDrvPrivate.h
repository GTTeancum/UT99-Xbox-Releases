// XboxDrvPrivate.h
// Forced-include for XboxDrv module.
// Must fire before any other header to kill XDK macro collisions.

#pragma once

// XDK first
#include <XTL.h>
#include <XGMath.h>

// Kill XDK macros that collide with UT99 names
#ifdef Top
#undef Top
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif

// Now safe to pull in our main header (which includes Engine.h etc.)
#include "XboxDrv.h"
