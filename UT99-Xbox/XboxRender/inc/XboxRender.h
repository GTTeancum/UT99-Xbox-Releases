// XboxRender.h
// Xbox D3D8 render device for UT99.
// Implements URenderDevice using Xbox Direct3D 8.

#pragma once

// XDK first
#include <xtl.h>
#include <xgraphics.h>

// Kill XDK macros that collide with UT99
#ifdef Top
#undef Top
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif

// Static-lib GPackage fix: redirect to unique symbol for this package.
#undef  GPackage
#define GPackage GPackage_XboxRender
#ifndef IMPLEMENT_PACKAGE_XBOX
#define IMPLEMENT_PACKAGE_XBOX 1
#endif

// Kill DLL decoration
#pragma warning(disable: 4005)
#undef  DLL_EXPORT
#define DLL_EXPORT
#undef  DLL_IMPORT
#define DLL_IMPORT
#undef  CORE_API
#define CORE_API
#undef  ENGINE_API
#define ENGINE_API

// VS2005 conformance
#pragma warning(disable: 4996)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#pragma warning(disable: 4305)
#pragma warning(disable: 4800)
#pragma warning(disable: 4018)
#pragma conform(forScope, off)

#include "Engine.h"
#include "UnRender.h"
#include "FXboxLogger.h"

// ============================================================================
// Pre-transformed lit vertex (XYZRHW + Diffuse + 1 tex coord set)
// ============================================================================
struct FXboxTLVertex
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u, v;
};
#define XBOX_FVF_TLVERTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// Pre-transformed lit vertex with 2 tex coord sets (for multitexture BSP)
struct FXboxTLVertex2
{
    FLOAT x, y, z, rhw;
    DWORD color;
    FLOAT u0, v0;
    FLOAT u1, v1;
};
#define XBOX_FVF_TLVERTEX2 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX2)

// ============================================================================
// Texture cache
// ============================================================================
enum { XBOX_TEX_CACHE_SIZE = 4096 };

struct FXboxTexCacheEntry
{
    QWORD                 CacheID;
    IDirect3DTexture8*    pTexture;
    FLOAT                 UScale;
    FLOAT                 VScale;
    INT                   FrameCounter;
    FXboxTexCacheEntry*   HashNext;
};

// ============================================================================
// Max verts per draw call (stack buffer)
// ============================================================================
enum { XBOX_MAX_VERTS = 512 };

// ============================================================================
// Render device
// ============================================================================
class UXboxRenderDevice : public URenderDevice
{
    DECLARE_CLASS(UXboxRenderDevice, URenderDevice, CLASS_Config)
    NO_DEFAULT_CONSTRUCTOR(UXboxRenderDevice)
public:
    // D3D objects
    IDirect3D8*         Direct3D;
    IDirect3DDevice8*   Device;
    IDirect3DSurface8*  BackBuffer;
    IDirect3DSurface8*  DepthBuffer;
    UBOOL               DeviceCreated;

    // Scene state
    FSceneNode*         CurrentFrame;
    FLOAT               RProjZ;       // Frame->RProj.Z
    FLOAT               zNear;
    FLOAT               zFar;
    FLOAT               ProjZRatio;   // zFar / (zFar - zNear)
    FLOAT               ProjZOffset;  // -ProjZRatio * zNear

    // Blending state
    DWORD               CurrentPolyFlags;

    // Flash state (stored from Lock)
    FPlane              FlashScale;
    FPlane              FlashFog;

    // Frame counter
    INT                 FrameCounter;

    // Texture cache
    FXboxTexCacheEntry* TexCache[XBOX_TEX_CACHE_SIZE];
    FXboxTexCacheEntry  TexPool[XBOX_TEX_CACHE_SIZE]; // pre-allocated pool
    INT                 TexPoolNext;                   // next free in pool

    // Current texture stage state (for early-out)
    QWORD               BoundCacheID[2];
    FLOAT               StageUScale[2];
    FLOAT               StageVScale[2];

    // URenderDevice interface
    void  StaticConstructor();
    UBOOL Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen );
    UBOOL SetRes( INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen );
    void  Exit();
    void  Flush( UBOOL AllowPrecache );
    UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
    void  Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize );
    void  Unlock( UBOOL Blit );
    void  DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet );
    void  DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Info, FTransTexture** Pts, int NumPts, DWORD PolyFlags, FSpanBuffer* Span );
    void  DrawTile( FSceneNode* Frame, FTextureInfo& Info, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, class FSpanBuffer* Span, FLOAT Z, FPlane Color, FPlane Fog, DWORD PolyFlags );
    void  Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 );
    void  Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z );
    void  ClearZ( FSceneNode* Frame );
    void  PushHit( const BYTE* Data, INT Count );
    void  PopHit( INT Count, UBOOL bForce );
    void  GetStats( TCHAR* Result );
    void  ReadPixels( FColor* Pixels );
    void  EndFlash();
    void  SetSceneNode( FSceneNode* Frame );

    // Private helpers
    void  SetBlending( DWORD PolyFlags );
    void  SetTextureD3D( INT Stage, FTextureInfo& Info, DWORD PolyFlags );
    void  FlushTexCache();
};
