// XboxRender.h
// Xbox D3D8 render device for UT99.
// Implements URenderDevice using Xbox Direct3D 8.

#pragma once

// Include ordering for Xbox D3D8:
//
// xtl.h guards its own D3D8 block with #ifndef NOD3D (not _D3D8_H_).
// We must get Windows base types (windef.h / winbase.h) before D3D8.h because
// D3D8Types.h uses DWORD/LONG/WORD/BYTE. The sequence:
//
// 1. Define NOD3D so xtl.h skips its implicit <d3d8.h>/<d3dx8.h> block.
// 2. Include <xtl.h> to get windef.h, winbase.h, xbox.h.
// 3. Include <D3D8.h> from C:\XDK_5558\XDK\xbox\include. This is the real
//    5558 Xbox header: D3D_SDK_VERSION=0 and D3DPRESENT_PARAMETERS ends with
//    BufferSurfaces[3] + DepthStencilSurface, matching CXBX-R and OpenJKDF2.
//    Do not include 5849's D3D8-Xbox.h here; our 5849 fallback tree contains a
//    fabricated d3d8types-xbox.h whose last 16 bytes are the wrong fields.
// 4. Include <xgraphics.h> for D3DXSurf helpers after D3D8 types are live.
#define NOD3D
#define NODSOUND
#include <xtl.h>
#undef  NOD3D
#undef  NODSOUND
#include <D3D8.h>
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

// Camera-space lit vertices. Used by world BSP so the Xbox fixed-function
// pipeline performs the perspective divide and perspective-correct UVs.
struct FXboxWorldVertex
{
    FLOAT x, y, z;
    DWORD color;
    FLOAT u, v;
};
#define XBOX_FVF_WORLDVERTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

struct FXboxWorldVertex2
{
    FLOAT x, y, z;
    DWORD color;
    FLOAT u0, v0;
    FLOAT u1, v1;
};
#define XBOX_FVF_WORLDVERTEX2 (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2)

// ============================================================================
// Texture cache
// ============================================================================
enum { XBOX_TEX_CACHE_SIZE = 4096 };
enum { XBOX_TEX_RESIDENT_LIMIT = 768 };

struct FXboxTexCacheEntry
{
    QWORD                 CacheID;
    IDirect3DTexture8*    pTexture;
    FLOAT                 UScale;
    FLOAT                 VScale;
    INT                   USize;
    INT                   VSize;
    INT                   NumMips;
    INT                   FirstMip;
    INT                   UIndex;
    INT                   VIndex;
    D3DFORMAT             Format;
    INT                   Bytes;
    INT                   FrameCounter;
    FXboxTexCacheEntry*   HashNext;
};

// ============================================================================
// Max verts per draw call (stack buffer)
// ============================================================================
enum { XBOX_MAX_VERTS = 512 };
enum { XBOX_DRAW_VB_BYTES = 512 * 1024 };

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
    IDirect3DVertexBuffer8* DrawVertexBuffer;
    UINT                DrawVBBytes;
    UINT                DrawVBOffset;
    UBOOL               DeviceCreated;
    UBOOL               SceneOpen;

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

    // Actual backbuffer dimensions (set in Init from GetBackBuffer/GetDesc).
    // CXBX-R returns a host-chosen surface size (e.g. 480x518) that differs
    // from what we asked for in present params; the engine still drives the
    // viewport at 640x480 logical, so we have to honour what really exists.
    UINT                ActualBackBufferW;
    UINT                ActualBackBufferH;

    // Texture cache
    FXboxTexCacheEntry* TexCache[XBOX_TEX_CACHE_SIZE];
    FXboxTexCacheEntry  TexPool[XBOX_TEX_CACHE_SIZE]; // pre-allocated pool
    INT                 TexPoolNext;                   // next free in pool
    INT                 TexLiveBytes;

    // Current texture stage state (for early-out)
    QWORD               BoundCacheID[2];
    FLOAT               StageUScale[2];
    FLOAT               StageVScale[2];
    INT                 StageUIndex[2];
    INT                 StageVIndex[2];

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
    void  EndSceneForTextureUpload( const char* Reason );
    void  ResumeSceneAfterTextureUpload( const char* Reason );
    HRESULT DrawPrimitiveVB( D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* Vertices, UINT Stride, const char* OpName );
    HRESULT DrawPrimitiveVBWorld( D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* Vertices, UINT Stride, const char* OpName );
    void  FlushDGPBatch( const char* Reason );
    void  RestoreDefaultTextureStages();
    void  DrawPerfOverlay();
    void  FlushTexCache();
    void  ReleaseDrawVertexBuffer();
};
