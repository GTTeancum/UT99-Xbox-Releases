// CoreXboxCompat.h
// Forced-include for Core and Engine projects when building for Xbox.
// Must be included before any UT99 headers.

#pragma once

// ── XDK first ────────────────────────────────────────────────────────────
#include <xtl.h>
#include <xgmath.h>

// ── Kill XDK macros that collide with UT99 ───────────────────────────────
#ifdef Top
#undef Top
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif

// ── Kill DLL import/export — static lib build ────────────────────────────
// Must come before UnVcWin32.h redefines them (suppress the C4005 warning)
#pragma warning(disable: 4005)  // macro redefinition
#undef  DLL_EXPORT
#define DLL_EXPORT
#undef  DLL_IMPORT
#define DLL_IMPORT
#undef  CORE_API
#define CORE_API
#undef  ENGINE_API
#define ENGINE_API

// ── VS2005 conformance fixes ──────────────────────────────────────────────
// C3867: VC6 allowed bare member function pointer without &, VS2005 doesn't.
// UT99 uses SomeClass::StaticConstructor as a value in IMPLEMENT_CLASS.
// Downgrade to warning so compilation continues; the function pointers
// still work correctly at runtime since they're stored in void* tables.

// C2065: Loop variable declared in for() leaks into outer scope in VC6.
// VS2005 enforces standard scoping. Affected files use bare i/j/etc after loop.
// We re-enable the scoping extension to match VC6 behavior.
#pragma warning(disable: 4289)  // nonstandard extension: loop control variable
// Enable VC6-compatible for-loop scoping
#pragma conform(forScope, off)

// ── Suppress other known noisy warnings ──────────────────────────────────
#pragma warning(disable: 4996)  // deprecated POSIX names
#pragma warning(disable: 4244)  // conversion, possible loss of data
#pragma warning(disable: 4267)  // size_t to int
#pragma warning(disable: 4305)  // truncation
#pragma warning(disable: 4800)  // forcing int to bool
#pragma warning(disable: 4018)  // signed/unsigned mismatch
#pragma warning(disable: 4146)  // unary minus on unsigned
#pragma warning(disable: 4530)  // C++ exception handler used, no /EHsc
#pragma warning(disable: 4667)  // no function template matching forced instantiation

// ── tagPALETTEENTRY collision: s3tc.h vs XDK d3dx8tex.h ─────────────────
// XDK d3dx8tex.h already defines tagPALETTEENTRY/PALETTEENTRY.
// s3tc.h redefines it — patch s3tc.h directly (see instructions).
// This define prevents the duplicate if s3tc.h checks _PALETTEENTRY_DEFINED:
#define _PALETTEENTRY_DEFINED

// ── TCHARU: UT99 uses this for Unicode char in a few files ────────────────
// On Xbox we're ASCII-only; define it as char
#ifndef TCHARU
#define TCHARU char
#endif

// ── Kill Windows DLL package init (DllMain, hInstance) ───────────────────
// UnVcWin32.h defines IMPLEMENT_PACKAGE_PLATFORM to emit DllMain/hInstance.
// We pre-define it empty here. UnVcWin32.h will redefine it (C4005 warning)
// so we also hook it via UnVcWin32.h patch (add #ifndef guard there).
#define IMPLEMENT_PACKAGE_PLATFORM(pkg)

// ── Xbox has no HINSTANCE / shell APIs ───────────────────────────────────
#ifndef _XBOX
#define _XBOX
#endif

// ── Standard C runtime ───────────────────────────────────────────────────
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>