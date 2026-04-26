// EngineForceLinks.cpp
//
// Forces the VS2005 static-lib linker to pull in all Engine class .obj files
// from UT99Engine.lib.  In a static-lib build every IMPLEMENT_CLASS(X) creates
//   extern "C" UClass* autoclassX;
// but because DLL_EXPORT is empty nothing else references those symbols, so the
// linker silently discards the .obj and UObject::ProcessRegistrants() never sees
// the class — causing VerifyImport failures for Engine.Font and every other
// native Engine class.
//
// ForceEngineClassLinks() takes the address of every autoclass symbol; the
// linker must resolve each one, dragging the corresponding .obj into the image
// and running its global-constructor (which chains the class into GAutoRegister).
// Call this from main() before appInit().

#include "XboxLaunchPrivate.h"

extern "C" UClass* autoclassAActor;
extern "C" UClass* autoclassALight;
extern "C" UClass* autoclassAWeapon;
extern "C" UClass* autoclassALevelInfo;
extern "C" UClass* autoclassAGameInfo;
extern "C" UClass* autoclassACamera;
extern "C" UClass* autoclassAZoneInfo;
extern "C" UClass* autoclassASkyZoneInfo;
extern "C" UClass* autoclassAPathNode;
extern "C" UClass* autoclassANavigationPoint;
extern "C" UClass* autoclassAScout;
extern "C" UClass* autoclassAInterpolationPoint;
extern "C" UClass* autoclassADecoration;
extern "C" UClass* autoclassAProjectile;
extern "C" UClass* autoclassAWarpZoneInfo;
extern "C" UClass* autoclassATeleporter;
extern "C" UClass* autoclassAPlayerStart;
extern "C" UClass* autoclassAKeypoint;
extern "C" UClass* autoclassAInventory;
extern "C" UClass* autoclassAInventorySpot;
extern "C" UClass* autoclassATriggers;
extern "C" UClass* autoclassATrigger;
extern "C" UClass* autoclassATriggerMarker;
extern "C" UClass* autoclassAButtonMarker;
extern "C" UClass* autoclassAWarpZoneMarker;
extern "C" UClass* autoclassAHUD;
extern "C" UClass* autoclassAMenu;
extern "C" UClass* autoclassASavedMove;
extern "C" UClass* autoclassACarcass;
extern "C" UClass* autoclassALiftCenter;
extern "C" UClass* autoclassALiftExit;
extern "C" UClass* autoclassAInfo;
extern "C" UClass* autoclassAReplicationInfo;
extern "C" UClass* autoclassAPlayerReplicationInfo;
extern "C" UClass* autoclassAInternetInfo;
extern "C" UClass* autoclassAStatLog;
extern "C" UClass* autoclassAStatLogFile;
extern "C" UClass* autoclassAGameReplicationInfo;
extern "C" UClass* autoclassULevelSummary;
extern "C" UClass* autoclassAlocationid;
extern "C" UClass* autoclassADecal;
extern "C" UClass* autoclassASpawnNotify;
extern "C" UClass* autoclassAMutator;
extern "C" UClass* autoclassAMover;
extern "C" UClass* autoclassABrush;
extern "C" UClass* autoclassAPawn;
extern "C" UClass* autoclassAPlayerPawn;
extern "C" UClass* autoclassUViewport;
extern "C" UClass* autoclassUChannel;
extern "C" UClass* autoclassUControlChannel;
extern "C" UClass* autoclassUActorChannel;
extern "C" UClass* autoclassUFileChannel;
extern "C" UClass* autoclassUSound;
extern "C" UClass* autoclassUMusic;
extern "C" UClass* autoclassUAudioSubsystem;
extern "C" UClass* autoclassUConsole;
extern "C" UClass* autoclassUNetConnection;
extern "C" UClass* autoclassUClient;
extern "C" UClass* autoclassUCanvas;
extern "C" UClass* autoclassUDemoPlayPendingLevel;
extern "C" UClass* autoclassUDemoRecConnection;
extern "C" UClass* autoclassUDemoRecDriver;
extern "C" UClass* autoclassUFont;
extern "C" UClass* autoclassUEngine;
extern "C" UClass* autoclassURenderBase;
extern "C" UClass* autoclassURenderDevice;
extern "C" UClass* autoclassURenderIterator;
extern "C" UClass* autoclassUServerCommandlet;
extern "C" UClass* autoclassUPolys;
extern "C" UClass* autoclassUGameEngine;
extern "C" UClass* autoclassUInput;
extern "C" UClass* autoclassULevelBase;
extern "C" UClass* autoclassULevel;
extern "C" UClass* autoclassULodMesh;
extern "C" UClass* autoclassUMesh;
extern "C" UClass* autoclassUBspNodes;
extern "C" UClass* autoclassUBspSurfs;
extern "C" UClass* autoclassUVectors;
extern "C" UClass* autoclassUVerts;
extern "C" UClass* autoclassUModel;
extern "C" UClass* autoclassUPackageMapLevel;
extern "C" UClass* autoclassUNetDriver;
extern "C" UClass* autoclassUPendingLevel;
extern "C" UClass* autoclassUNetPendingLevel;
extern "C" UClass* autoclassUPrimitive;
extern "C" UClass* autoclassUPlayer;
extern "C" UClass* autoclassUScriptedTexture;
extern "C" UClass* autoclassUBitmap;
extern "C" UClass* autoclassUTexture;
extern "C" UClass* autoclassUPalette;

void ForceEngineClassLinks()
{
    // Taking the address of each extern forces the linker to resolve the symbol
    // and include the defining .obj from UT99Engine.lib.  volatile prevents the
    // compiler from removing the stores as dead code.
    volatile UClass** sink = NULL;
    sink = (volatile UClass**)&autoclassAActor;
    sink = (volatile UClass**)&autoclassALight;
    sink = (volatile UClass**)&autoclassAWeapon;
    sink = (volatile UClass**)&autoclassALevelInfo;
    sink = (volatile UClass**)&autoclassAGameInfo;
    sink = (volatile UClass**)&autoclassACamera;
    sink = (volatile UClass**)&autoclassAZoneInfo;
    sink = (volatile UClass**)&autoclassASkyZoneInfo;
    sink = (volatile UClass**)&autoclassAPathNode;
    sink = (volatile UClass**)&autoclassANavigationPoint;
    sink = (volatile UClass**)&autoclassAScout;
    sink = (volatile UClass**)&autoclassAInterpolationPoint;
    sink = (volatile UClass**)&autoclassADecoration;
    sink = (volatile UClass**)&autoclassAProjectile;
    sink = (volatile UClass**)&autoclassAWarpZoneInfo;
    sink = (volatile UClass**)&autoclassATeleporter;
    sink = (volatile UClass**)&autoclassAPlayerStart;
    sink = (volatile UClass**)&autoclassAKeypoint;
    sink = (volatile UClass**)&autoclassAInventory;
    sink = (volatile UClass**)&autoclassAInventorySpot;
    sink = (volatile UClass**)&autoclassATriggers;
    sink = (volatile UClass**)&autoclassATrigger;
    sink = (volatile UClass**)&autoclassATriggerMarker;
    sink = (volatile UClass**)&autoclassAButtonMarker;
    sink = (volatile UClass**)&autoclassAWarpZoneMarker;
    sink = (volatile UClass**)&autoclassAHUD;
    sink = (volatile UClass**)&autoclassAMenu;
    sink = (volatile UClass**)&autoclassASavedMove;
    sink = (volatile UClass**)&autoclassACarcass;
    sink = (volatile UClass**)&autoclassALiftCenter;
    sink = (volatile UClass**)&autoclassALiftExit;
    sink = (volatile UClass**)&autoclassAInfo;
    sink = (volatile UClass**)&autoclassAReplicationInfo;
    sink = (volatile UClass**)&autoclassAPlayerReplicationInfo;
    sink = (volatile UClass**)&autoclassAInternetInfo;
    sink = (volatile UClass**)&autoclassAStatLog;
    sink = (volatile UClass**)&autoclassAStatLogFile;
    sink = (volatile UClass**)&autoclassAGameReplicationInfo;
    sink = (volatile UClass**)&autoclassULevelSummary;
    sink = (volatile UClass**)&autoclassAlocationid;
    sink = (volatile UClass**)&autoclassADecal;
    sink = (volatile UClass**)&autoclassASpawnNotify;
    sink = (volatile UClass**)&autoclassAMutator;
    sink = (volatile UClass**)&autoclassAMover;
    sink = (volatile UClass**)&autoclassABrush;
    sink = (volatile UClass**)&autoclassAPawn;
    sink = (volatile UClass**)&autoclassAPlayerPawn;
    sink = (volatile UClass**)&autoclassUViewport;
    sink = (volatile UClass**)&autoclassUChannel;
    sink = (volatile UClass**)&autoclassUControlChannel;
    sink = (volatile UClass**)&autoclassUActorChannel;
    sink = (volatile UClass**)&autoclassUFileChannel;
    sink = (volatile UClass**)&autoclassUSound;
    sink = (volatile UClass**)&autoclassUMusic;
    sink = (volatile UClass**)&autoclassUAudioSubsystem;
    sink = (volatile UClass**)&autoclassUConsole;
    sink = (volatile UClass**)&autoclassUNetConnection;
    sink = (volatile UClass**)&autoclassUClient;
    sink = (volatile UClass**)&autoclassUCanvas;
    sink = (volatile UClass**)&autoclassUDemoPlayPendingLevel;
    sink = (volatile UClass**)&autoclassUDemoRecConnection;
    sink = (volatile UClass**)&autoclassUDemoRecDriver;
    sink = (volatile UClass**)&autoclassUFont;
    sink = (volatile UClass**)&autoclassUEngine;
    sink = (volatile UClass**)&autoclassURenderBase;
    sink = (volatile UClass**)&autoclassURenderDevice;
    sink = (volatile UClass**)&autoclassURenderIterator;
    sink = (volatile UClass**)&autoclassUServerCommandlet;
    sink = (volatile UClass**)&autoclassUPolys;
    sink = (volatile UClass**)&autoclassUGameEngine;
    sink = (volatile UClass**)&autoclassUInput;
    sink = (volatile UClass**)&autoclassULevelBase;
    sink = (volatile UClass**)&autoclassULevel;
    sink = (volatile UClass**)&autoclassULodMesh;
    sink = (volatile UClass**)&autoclassUMesh;
    sink = (volatile UClass**)&autoclassUBspNodes;
    sink = (volatile UClass**)&autoclassUBspSurfs;
    sink = (volatile UClass**)&autoclassUVectors;
    sink = (volatile UClass**)&autoclassUVerts;
    sink = (volatile UClass**)&autoclassUModel;
    sink = (volatile UClass**)&autoclassUPackageMapLevel;
    sink = (volatile UClass**)&autoclassUNetDriver;
    sink = (volatile UClass**)&autoclassUPendingLevel;
    sink = (volatile UClass**)&autoclassUNetPendingLevel;
    sink = (volatile UClass**)&autoclassUPrimitive;
    sink = (volatile UClass**)&autoclassUPlayer;
    sink = (volatile UClass**)&autoclassUScriptedTexture;
    sink = (volatile UClass**)&autoclassUBitmap;
    sink = (volatile UClass**)&autoclassUTexture;
    sink = (volatile UClass**)&autoclassUPalette;
}
