// XboxLaunchPrivate.h
// Forced-include for XboxLaunch module.

#pragma once

// XDK first -- before anything that might define conflicting macros
#include <XTL.h>
#include <XGMath.h>

// Kill XDK macros that collide with UT99
#ifdef Top
#undef Top
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif

// Must empty CORE_API and ENGINE_API before including UT99 headers.
// XboxLaunch links against static libs -- dllimport decoration must not appear.
#pragma warning(disable: 4005)
#undef  CORE_API
#define CORE_API
#undef  ENGINE_API
#define ENGINE_API
#undef  DLL_EXPORT
#define DLL_EXPORT
#undef  DLL_IMPORT
#define DLL_IMPORT

// VS2005 conformance fixes (match CoreXboxCompat.h settings)
#pragma warning(disable: 4996)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#pragma warning(disable: 4305)
#pragma warning(disable: 4800)
#pragma warning(disable: 4018)
#pragma conform(forScope, off)

// ── Static-lib IMPLEMENT_PACKAGE override ──────────────────────────────────
// Without this, IMPLEMENT_PACKAGE(Xxx) in the package stubs expands to
// `TCHAR GPackage[]=TEXT("Xxx")` + DllMain + hInstance (the Win32 DLL form).
// XboxLaunch.cpp already defines GPackage_Engine etc., so each stub's
// IMPLEMENT_PACKAGE collides on _GPackage_Engine and _DllMain.
//
// IMPLEMENT_PACKAGE_XBOX collapses IMPLEMENT_PACKAGE to a bare extern
// declaration (see UnObjBas.h:360-364) — no definition, no DllMain.
// Also kill IMPLEMENT_PACKAGE_PLATFORM so any direct uses don't emit DllMain.
#define IMPLEMENT_PACKAGE_XBOX        1
#define IMPLEMENT_PACKAGE_PLATFORM(pkg)

// UT99 core -- now safe to include
#include "Core.h"
#include "Engine.h"

// Xbox platform objects
#include "FXboxLogger.h"
#include "FMallocXbox.h"
#include "FOutputDeviceXboxError.h"
#include "FFeedbackContextXbox.h"
#include "FFileManagerXbox.h"
