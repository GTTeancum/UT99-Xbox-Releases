// EngineXbox.cpp
// Replaces Engine.cpp for Xbox builds.
// Engine.cpp defines DllMain/hInstance for Windows DLL -- not needed on Xbox.

#include "EnginePrivate.h"

IMPLEMENT_PACKAGE(Engine);

// Global engine memory objects -- defined here to match where Engine.cpp
// would define them in a Windows build.
FMemCache  GCache;
FMemStack  GEngineMem;
