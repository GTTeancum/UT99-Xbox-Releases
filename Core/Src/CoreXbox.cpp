// CoreXbox.cpp
// Replaces Core.cpp for Xbox builds.
// Core.cpp defines DllMain/hInstance for the Windows Core DLL.
// On Xbox we build as a static lib -- only IMPLEMENT_PACKAGE is needed.

#include "CorePrivate.h"

IMPLEMENT_PACKAGE(Core);
