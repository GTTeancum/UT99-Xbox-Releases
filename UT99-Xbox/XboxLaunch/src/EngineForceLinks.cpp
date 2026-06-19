// EngineForceLinks.cpp
//
// Forces the VS2005 static-lib linker to pull in all Engine class .obj files
// from UT99Engine.lib.  In a static-lib build every IMPLEMENT_CLASS(X) creates
//   extern "C" UClass* autoclassX;
// but because DLL_EXPORT is empty nothing else references those symbols, so the
// linker silently discards the .obj and UObject::ProcessRegistrants() never sees
// the class — causing VerifyImport failures for Engine.Font and every other
// native Engine class that has no other callers.
//
// The address-of approach (sink = &autoclass) is eliminated by /O2 because the
// local sink pointer is never read.  #pragma comment(linker,"/include:_symbol")
// is optimizer-independent — it is a direct linker directive.
//
// x86 MSVC extern "C" globals get a leading underscore: autoclassUFont -> _autoclassUFont.
// IMPLEMENT_CLASS(UFont) -> autoclassUFont (no separator between "autoclass" and "UFont").

#include "XboxLaunchPrivate.h"

// ── XDK display-library parity with OpenJKDF2 ───────────────────────────────
// OpenJKDF2's known-good XBE is not just linked against these libraries; it
// contains live D3DX/DSOUND sections and imagebld emits D3DX8 + DSOUND library
// records. UT had d3dx8.lib/dsound.lib on the link line, but no references, so
// LINK discarded both and the XBE stayed structurally different from the
// baseline CXBX-R is known to display. Force one data symbol from each library.
#pragma comment(linker, "/include:_D3DX8BuildNumber")
#pragma comment(linker, "/include:_D3DXCreateMatrixStack@8")
#pragma comment(linker, "/include:_DSoundBuildNumber")

// ── UnScript.cpp ─────────────────────────────────────────────────────────────
// UnScript.cpp has no IMPLEMENT_CLASS — only 3 IMPLEMENT_FUNCTION calls for
// AActor's polling natives (EPOLL_Sleep, EPOLL_FinishAnim, EPOLL_FinishInterpolation).
// Without a force-link, the linker strips the .obj and GNatives[EPOLL_*] stays at
// execUndefined.  The intro level's camera interpolation hits FinishInterpolation.
#pragma comment(linker, "/include:_intAActorexecPollSleep")

// ── AStatLog.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassAMutator")

// ── ULodMesh.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassULodMesh")

// ── UnActor.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassAActor")
#pragma comment(linker, "/include:_autoclassALight")
#pragma comment(linker, "/include:_autoclassAWeapon")
#pragma comment(linker, "/include:_autoclassALevelInfo")
#pragma comment(linker, "/include:_autoclassAGameInfo")
#pragma comment(linker, "/include:_autoclassACamera")
#pragma comment(linker, "/include:_autoclassAZoneInfo")
#pragma comment(linker, "/include:_autoclassASkyZoneInfo")
#pragma comment(linker, "/include:_autoclassAPathNode")
#pragma comment(linker, "/include:_autoclassANavigationPoint")
#pragma comment(linker, "/include:_autoclassAScout")
#pragma comment(linker, "/include:_autoclassAInterpolationPoint")
#pragma comment(linker, "/include:_autoclassADecoration")
#pragma comment(linker, "/include:_autoclassAProjectile")
#pragma comment(linker, "/include:_autoclassAWarpZoneInfo")
#pragma comment(linker, "/include:_autoclassATeleporter")
#pragma comment(linker, "/include:_autoclassAPlayerStart")
#pragma comment(linker, "/include:_autoclassAKeypoint")
#pragma comment(linker, "/include:_autoclassAInventory")
#pragma comment(linker, "/include:_autoclassAPickup")
#pragma comment(linker, "/include:_autoclassAAmmo")
#pragma comment(linker, "/include:_autoclassAInventorySpot")
#pragma comment(linker, "/include:_autoclassATriggers")
#pragma comment(linker, "/include:_autoclassATrigger")
#pragma comment(linker, "/include:_autoclassATriggerMarker")
#pragma comment(linker, "/include:_autoclassAButtonMarker")
#pragma comment(linker, "/include:_autoclassAWarpZoneMarker")
#pragma comment(linker, "/include:_autoclassAHUD")
#pragma comment(linker, "/include:_autoclassAMenu")
#pragma comment(linker, "/include:_autoclassASavedMove")
#pragma comment(linker, "/include:_autoclassACarcass")
#pragma comment(linker, "/include:_autoclassALiftCenter")
#pragma comment(linker, "/include:_autoclassALiftExit")
#pragma comment(linker, "/include:_autoclassAInfo")
#pragma comment(linker, "/include:_autoclassAReplicationInfo")
#pragma comment(linker, "/include:_autoclassAPlayerReplicationInfo")
#pragma comment(linker, "/include:_autoclassAInternetInfo")
#pragma comment(linker, "/include:_autoclassAStatLog")
#pragma comment(linker, "/include:_autoclassAStatLogFile")
#pragma comment(linker, "/include:_autoclassAGameReplicationInfo")
#pragma comment(linker, "/include:_autoclassULevelSummary")
#pragma comment(linker, "/include:_autoclassAlocationid")
#pragma comment(linker, "/include:_autoclassADecal")
#pragma comment(linker, "/include:_autoclassASpawnNotify")

// ── UnCamera.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUViewport")

// ── UnChan.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUChannel")
#pragma comment(linker, "/include:_autoclassUControlChannel")
#pragma comment(linker, "/include:_autoclassUActorChannel")
#pragma comment(linker, "/include:_autoclassUFileChannel")

// ── UnAudio.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUSound")
#pragma comment(linker, "/include:_autoclassUMusic")
#pragma comment(linker, "/include:_autoclassUAudioSubsystem")

// ── UnCon.cpp ────────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUConsole")

// ── UnConn.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUNetConnection")

// ── UnCamMgr.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUClient")

// ── UnCanvas.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUCanvas")

// ── UnDemoPenLev.cpp ─────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUDemoPlayPendingLevel")

// ── UnDemoRec.cpp ────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUDemoRecConnection")
#pragma comment(linker, "/include:_autoclassUDemoRecDriver")

// ── UnDynBsp.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassABrush")

// ── UnFont.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUFont")

// ── UnAnimationStub.cpp ──────────────────────────────────────────────────────
// Minimal native class so Engine.u's "Engine.Animation" import (PropertyClass
// of AActor::SkelAnim) resolves at load time.  v436 binary has the import but
// no exporting package, so a native registration is required.
#pragma comment(linker, "/include:_autoclassUAnimation")

// ── Fire/Src/UnFractal.cpp ──────────────────────────────────────────────────
// Procedural texture native classes from Fire.u — UFireTexture, UFractalTexture,
// UWaterTexture, UWaveTexture, UWetTexture, UIceTexture.  Without these, Fire.u
// loads as pure-script and texture serialization mismatches due to native
// fields (RenderTable[1028], Sparks TArray, etc.) that the script-only path
// can't handle correctly.
#pragma comment(linker, "/include:_autoclassUFractalTexture")
#pragma comment(linker, "/include:_autoclassUFireTexture")
#pragma comment(linker, "/include:_autoclassUWaterTexture")
#pragma comment(linker, "/include:_autoclassUWaveTexture")
#pragma comment(linker, "/include:_autoclassUWetTexture")
#pragma comment(linker, "/include:_autoclassUIceTexture")

// ── UnEngine.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUEngine")
#pragma comment(linker, "/include:_autoclassURenderBase")
#pragma comment(linker, "/include:_autoclassURenderDevice")
#pragma comment(linker, "/include:_autoclassURenderIterator")
#pragma comment(linker, "/include:_autoclassUServerCommandlet")

// ── UnFPoly.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUPolys")

// ── UnGame.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUGameEngine")

// ── UnIn.cpp ─────────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUInput")

// ── UnLevel.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassULevelBase")
#pragma comment(linker, "/include:_autoclassULevel")

// ── UnMesh.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUMesh")

// ── UnModel.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUBspNodes")
#pragma comment(linker, "/include:_autoclassUBspSurfs")
#pragma comment(linker, "/include:_autoclassUVectors")
#pragma comment(linker, "/include:_autoclassUVerts")
#pragma comment(linker, "/include:_autoclassUModel")

// ── UnNetDrv.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUPackageMapLevel")
#pragma comment(linker, "/include:_autoclassUNetDriver")

// System Link final travel uses NetworkDevice=IpDrv.TcpNetDriver. Force the
// static net package/class objects in so StaticLoadClass can resolve it.
#pragma comment(linker, "/include:_autoclassUTcpNetDriver")
#pragma comment(linker, "/include:_autoclassUTcpipConnection")
#pragma comment(linker, "/include:_autoclassAInternetLink")
#pragma comment(linker, "/include:_autoclassAUdpLink")
#pragma comment(linker, "/include:_autoclassATcpLink")

// ── UnMover.cpp ──────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassAMover")

// ── UnPawn.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassAPawn")
#pragma comment(linker, "/include:_autoclassAPlayerPawn")

// ── UnPenLev.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUPendingLevel")
#pragma comment(linker, "/include:_autoclassUNetPendingLevel")

// ── UnPrim.cpp ───────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUPrimitive")

// ── UnPlayer.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUPlayer")

// ── UnScrTex.cpp ─────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUScriptedTexture")

// ── UnTex.cpp ────────────────────────────────────────────────────────────────
#pragma comment(linker, "/include:_autoclassUBitmap")
#pragma comment(linker, "/include:_autoclassUTexture")
#pragma comment(linker, "/include:_autoclassUPalette")

// ── Render.lib (UT99Render.lib) ──────────────────────────────────────────────
// Render package — engine config Render=Render.Render needs URender registered.
#pragma comment(linker, "/include:_autoclassURender")

// ── XboxDrv.lib ──────────────────────────────────────────────────────────────
// XboxDrv package — engine config ViewportManager=XboxDrv.XboxClient.
// Without these the XboxDrv UPackage never exists in memory and the engine
// falls back to a disk search for "XboxDrv.u" which doesn't exist.
// Verified: log line "Failed to load 'XboxDrv'", and XboxDrv.obj symbol dump
// confirms _autoclassUXboxClient/_autoclassUXboxViewport defined but unreferenced.
#pragma comment(linker, "/include:_autoclassUXboxClient")
#pragma comment(linker, "/include:_autoclassUXboxViewport")

// ── XboxRender.cpp ────────────────────────────────────────────────────────────
// Same situation as XboxDrv: IMPLEMENT_CLASS(UXboxRenderDevice) and
// IMPLEMENT_PACKAGE(XboxRender) static initializers only run if the .obj is
// pulled into the link. Without this, ResolveName("XboxRender.XboxRenderDevice")
// can't find the package or class, falls through to .u file search, and dies.
#pragma comment(linker, "/include:_autoclassUXboxRenderDevice")

// ── XboxAudio.cpp ─────────────────────────────────────────────────────────────
// Silent-stub UAudioSubsystem; Init() returns 0 so engine skips audio cleanly.
// Default.ini's AudioDevice must point at XboxAudio.XboxAudioDevice for this
// to be picked up; the vanilla "Galaxy.GalaxyAudioSubsystem" string crashes.
#pragma comment(linker, "/include:_autoclassUXboxAudioDevice")

// ── Script-only native-classes (Botpack, UMenu, UTMenu, UWindow, plus a few
//    Engine.u classes like Spectator/MessagingSpectator/MapList) intentionally
//    omitted.  These classes are flagged CLASS_Native in their .u files but
//    have no C++ implementation in this port.  UClass::Bind now falls through
//    to the nearest real native ancestor's ClassConstructor when GetDllExport
//    returns NULL, so no fake C++ stubs are needed.

// ForceEngineClassLinks() is called from main() before appInit() as a guard
// against future link-order changes stripping these pragmas' effect.
void ForceEngineClassLinks()
{
}
