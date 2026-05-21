// XboxRender.cpp
// Xbox D3D8 render device implementation.
// Ported from the D3D7 reference renderer (D3DDrv/Src/Direct3D7.cpp).

#include "XboxRender.h"

IMPLEMENT_CLASS(UXboxRenderDevice);
IMPLEMENT_PACKAGE(XboxRender);

extern "C" UBOOL XboxSplitShouldClearRenderLock();

#ifndef D3DLOCK_NOOVERWRITE
#define D3DLOCK_NOOVERWRITE 0x00001000L
#endif

// ============================================================================
// Helpers
// ============================================================================

static inline DWORD FPlaneTo_ARGB( const FPlane& P )
{
    return D3DCOLOR_ARGB(
        Clamp( appFloor(P.W * 255.f), 0, 255 ),
        Clamp( appFloor(P.X * 255.f), 0, 255 ),
        Clamp( appFloor(P.Y * 255.f), 0, 255 ),
        Clamp( appFloor(P.Z * 255.f), 0, 255 )
    );
}

// Compact render diagnostics. Keep this sparse: the log is useful only if the
// final few lines point at the last successful subsystem before CXBX-R exits.
static INT   GRD_FrameDCS        = 0;
static INT   GRD_FrameDGP        = 0;
static INT   GRD_FrameDT         = 0;
static INT   GRD_FramePrims      = 0;
static INT   GRD_FrameVerts      = 0;
static INT   GRD_FrameTexBinds   = 0;
static INT   GRD_FrameTexUploads = 0;
static INT   GRD_FrameTexCreates = 0;
static INT   GRD_FrameTexSkipped = 0;
static INT   GRD_FrameTexUploadSeq = 0;
static INT   GRD_FrameBadDraws   = 0;
static INT   GRD_FrameBadVerts   = 0;
static INT   GRD_FrameSceneSplits = 0;
static INT   GRD_FrameVBLocks    = 0;
static INT   GRD_FrameVBWraps    = 0;
static INT   GRD_FrameVBBytes    = 0;
static INT   GRD_FrameUPDraws    = 0;
static INT   GRD_FrameUPFails    = 0;
static INT   GRD_FrameDGPBatches = 0;
static INT   GRD_FrameDGPBatchedPolys = 0;
static INT   GRD_FrameDTBatches  = 0;
static INT   GRD_FrameDTBatchedTiles = 0;
static INT   GRD_FrameTexDeferred = 0;
static INT   GRD_FrameStateSets  = 0;
static INT   GRD_FrameStateSkips = 0;
static INT   GRD_HotTraceBudget  = 0;
static INT   GRD_TotalTexUploads = 0;
static INT   GRD_TotalTexCreates = 0;
static INT   GRD_TotalTexSkipped = 0;
static INT   GRD_TotalBadDraws   = 0;
static INT   GRD_TotalSceneSplits = 0;
static INT   GRD_TotalVBLocks    = 0;
static INT   GRD_TotalVBWraps    = 0;
static INT   GRD_TotalTexDeferred = 0;
static INT   GRD_TotalStateSets  = 0;
static INT   GRD_TotalStateSkips = 0;
static INT   GRD_BadDrawLogCount = 0;
static INT   GRD_DrawFailLogCount = 0;
static INT   GRD_TotalTexBytes   = 0;
static INT   GRD_TallTexLogCount = 0;
static INT   GRD_ClampPadLogCount = 0;
static INT   GRD_Rgba7MaxLogCount = 0;
static INT   GRD_DxtUnexpectedLogCount = 0;
static INT   GRD_MaxPolyVerts    = 0;
static DWORD GRD_LastPolyFlags   = 0;
static DWORD GRD_LastTextureIDLo = 0;
static DWORD GRD_LastTextureIDHi = 0;
static const char* GRD_LastOp    = "boot";
static const UBOOL GWireframeNoTextureProbe = 0;
static const UBOOL GUseXboxBspMultitexture = 1;
static const UBOOL GVerboseRenderPerfLog = 0;
static const UBOOL GDeferMidSceneRealtimeTextureUpdates = 1;
static const UBOOL GUseDrawPrimitiveUP = 1;
static DOUBLE GRD_FrameStartSeconds = 0.0;
static DOUBLE GRD_LastFrameStartSeconds = 0.0;
static DOUBLE GRD_LastPerfLogSeconds = 0.0;
static DOUBLE GRD_FpsWindowSeconds = 0.0;
static INT    GRD_FpsWindowFrames = 0;
static FLOAT  GRD_DisplayFPS = 0.0f;
static FLOAT  GRD_LastFrameMS = 0.0f;
static FLOAT  GRD_LastRenderMS = 0.0f;
static FLOAT  GRD_LastPresentMS = 0.0f;

enum { XBOX_SAFE_TRI_BATCH_VERTS = (XBOX_MAX_DRAW_VERTS / 3) * 3 };
enum { XBOX_DGP_BATCH_VERTS = XBOX_SAFE_TRI_BATCH_VERTS };
static FXboxWorldVertex GRD_DGPBatch[XBOX_DGP_BATCH_VERTS];
static INT   GRD_DGPBatchVerts = 0;
static INT   GRD_DGPBatchPolys = 0;
static QWORD GRD_DGPBatchCacheID = 0;
static DWORD GRD_DGPBatchPolyFlags = 0;
static UBOOL GRD_DGPBatchActive = 0;

enum { XBOX_DT_BATCH_VERTS = XBOX_SAFE_TRI_BATCH_VERTS };
static FXboxTLVertex GRD_DTBatch[XBOX_DT_BATCH_VERTS];
static INT   GRD_DTBatchVerts = 0;
static INT   GRD_DTBatchTiles = 0;
static QWORD GRD_DTBatchCacheID = 0;
static DWORD GRD_DTBatchPolyFlags = 0;
static UBOOL GRD_DTBatchActive = 0;
static UBOOL GRD_MenuTextMode = 0;
static INT   GRD_MenuTextSerial = 0;
static INT   GRD_MenuTextTiles = 0;
static INT   GRD_MenuTextLogBudget = 0;
static char  GRD_MenuTextLabel[64] = {0};
static UBOOL GRD_DrawVBStreamBound = 0;
static UINT  GRD_DrawVBStreamStride = 0;

static void RenderDiagPrim( INT NumVerts, DWORD PolyFlags, const char* Op )
{
    GRD_FramePrims++;
    GRD_FrameVerts += NumVerts;
    if( NumVerts > GRD_MaxPolyVerts )
        GRD_MaxPolyVerts = NumVerts;
    GRD_LastPolyFlags = PolyFlags;
    GRD_LastOp = Op;
}

static UBOOL RenderHotTrace()
{
    if( GRD_HotTraceBudget > 0 )
    {
        GRD_HotTraceBudget--;
        return 1;
    }
    return 0;
}

static UBOOL RenderHotFrame( INT Frame )
{
    return 0;
}

static UBOOL RenderBoundaryFrame( INT Frame )
{
    return 0;
}

static UBOOL RenderFrameSummaryLog( INT Frame )
{
    return GVerboseRenderPerfLog && (Frame <= 3 || (Frame % 300) == 0 || RenderHotFrame( Frame ) || RenderBoundaryFrame( Frame ));
}

static UBOOL RenderShouldLogDrawFailure( INT Frame )
{
    if( GRD_DrawFailLogCount < 24 || RenderBoundaryFrame( Frame ) )
    {
        GRD_DrawFailLogCount++;
        return 1;
    }
    GRD_DrawFailLogCount++;
    return 0;
}

static DWORD RenderAvailPhysKB()
{
    MEMORYSTATUS Status;
    appMemzero( &Status, sizeof(Status) );
    Status.dwLength = sizeof(Status);
    GlobalMemoryStatus( &Status );
    return Status.dwAvailPhys / 1024;
}

static UBOOL RenderFiniteFloat( FLOAT V )
{
    return V == V && V > -1000000000.0f && V < 1000000000.0f;
}

static UBOOL RenderSafeScreenFloat( FLOAT V )
{
    return V == V && V > -32768.0f && V < 32768.0f;
}

static UBOOL RenderSafeRHW( FLOAT V )
{
    return V == V && V > 0.000001f && V < 1000000.0f;
}

static UBOOL RenderValidateTLVertices( const void* Vertices, UINT VertexCount, UINT Stride, const char* OpName, D3DPRIMITIVETYPE PrimitiveType, INT FrameCounter )
{
    if( Stride < 16 )
        return 1;

    const BYTE* Base = (const BYTE*)Vertices;
    for( UINT i = 0; i < VertexCount; i++ )
    {
        const FLOAT* F = (const FLOAT*)(Base + i * Stride);
        FLOAT X   = F[0];
        FLOAT Y   = F[1];
        FLOAT Z   = F[2];
        FLOAT RHW = F[3];

        UBOOL bBad =
            !RenderSafeScreenFloat( X ) ||
            !RenderSafeScreenFloat( Y ) ||
            !RenderFiniteFloat( Z ) ||
            Z < -1000.0f ||
            Z > 1000.0f ||
            !RenderSafeRHW( RHW );

        if( !bBad && Stride >= sizeof(FXboxTLVertex) )
        {
            FLOAT U = F[5];
            FLOAT V = F[6];
            bBad = !RenderFiniteFloat( U ) || !RenderFiniteFloat( V );
        }

        if( !bBad && Stride >= sizeof(FXboxTLVertex2) )
        {
            FLOAT U1 = F[7];
            FLOAT V1 = F[8];
            bBad = !RenderFiniteFloat( U1 ) || !RenderFiniteFloat( V1 );
        }

        if( bBad )
        {
            GRD_FrameBadDraws++;
            GRD_FrameBadVerts++;
            GRD_TotalBadDraws++;
            GRD_LastOp = "BadTL";
            if( GRD_BadDrawLogCount < 48 || RenderBoundaryFrame( FrameCounter ) )
            {
                GRD_BadDrawLogCount++;
                GXboxLog.Write( "RDRAW BADTL op=%s f=%d type=%d vert=%u/%u stride=%u x=%.6f y=%.6f z=%.6f rhw=%.6f",
                    OpName ? OpName : "?", FrameCounter, (INT)PrimitiveType,
                    (unsigned)i, (unsigned)VertexCount, (unsigned)Stride, X, Y, Z, RHW );
                if( Stride >= sizeof(FXboxTLVertex) )
                    GXboxLog.Write( "RDRAW BADTL uv op=%s f=%d u0=%.6f v0=%.6f",
                        OpName ? OpName : "?", FrameCounter, F[5], F[6] );
                if( Stride >= sizeof(FXboxTLVertex2) )
                    GXboxLog.Write( "RDRAW BADTL uv1 op=%s f=%d u1=%.6f v1=%.6f",
                        OpName ? OpName : "?", FrameCounter, F[7], F[8] );
            }
            return 0;
        }
    }

    return 1;
}

static UBOOL RenderTextureHotTrace( INT Pool, INT TotalCreates )
{
    return Pool >= (XBOX_TEX_RESIDENT_LIMIT - 8);
}

static void RenderBlockAndReleaseTexture( IDirect3DTexture8*& Texture )
{
    if( Texture )
    {
        Texture->BlockUntilNotBusy();
        Texture->Release();
        Texture = NULL;
    }
}

static inline void SetUV( FXboxTLVertex& Vert, INT Stage, FLOAT U, FLOAT V, const FLOAT* UScale, const FLOAT* VScale, const INT* UIndex, const INT* VIndex )
{
    FLOAT T[2];
    T[UIndex[Stage]] = U * UScale[Stage];
    T[VIndex[Stage]] = V * VScale[Stage];
    Vert.u = T[0];
    Vert.v = T[1];
}

static inline void SetUV0( FXboxTLVertex2& Vert, FLOAT U, FLOAT V, const FLOAT* UScale, const FLOAT* VScale, const INT* UIndex, const INT* VIndex )
{
    FLOAT T[2];
    T[UIndex[0]] = U * UScale[0];
    T[VIndex[0]] = V * VScale[0];
    Vert.u0 = T[0];
    Vert.v0 = T[1];
}

static inline void SetUV1( FXboxTLVertex2& Vert, FLOAT U, FLOAT V, const FLOAT* UScale, const FLOAT* VScale, const INT* UIndex, const INT* VIndex )
{
    FLOAT T[2];
    T[UIndex[1]] = U * UScale[1];
    T[VIndex[1]] = V * VScale[1];
    Vert.u1 = T[0];
    Vert.v1 = T[1];
}

static inline void SetUV( FXboxWorldVertex& Vert, INT Stage, FLOAT U, FLOAT V, const FLOAT* UScale, const FLOAT* VScale, const INT* UIndex, const INT* VIndex )
{
    FLOAT T[2];
    T[UIndex[Stage]] = U * UScale[Stage];
    T[VIndex[Stage]] = V * VScale[Stage];
    Vert.u = T[0];
    Vert.v = T[1];
}

static inline void SetUV0( FXboxWorldVertex2& Vert, FLOAT U, FLOAT V, const FLOAT* UScale, const FLOAT* VScale, const INT* UIndex, const INT* VIndex )
{
    FLOAT T[2];
    T[UIndex[0]] = U * UScale[0];
    T[VIndex[0]] = V * VScale[0];
    Vert.u0 = T[0];
    Vert.v0 = T[1];
}

static inline void SetUV1( FXboxWorldVertex2& Vert, FLOAT U, FLOAT V, const FLOAT* UScale, const FLOAT* VScale, const INT* UIndex, const INT* VIndex )
{
    FLOAT T[2];
    T[UIndex[1]] = U * UScale[1];
    T[VIndex[1]] = V * VScale[1];
    Vert.u1 = T[0];
    Vert.v1 = T[1];
}

static inline INT RenderMipClampSize( INT BaseClamp, INT MipIndex, INT MipSize )
{
    if( BaseClamp <= 0 )
        return MipSize;
    INT Result = BaseClamp >> MipIndex;
    if( Result < 1 )
        Result = 1;
    return Clamp( Result, 1, MipSize );
}

// ============================================================================
// StaticConstructor
// ============================================================================
void UXboxRenderDevice::StaticConstructor()
{
    guard(UXboxRenderDevice::StaticConstructor);

    SpanBased            = 0;
    FullscreenOnly       = 1;
    SupportsFogMaps      = 0;
    SupportsDistanceFog  = 1;
    VolumetricLighting   = 0;
    ShinySurfaces        = 0;
    Coronas              = 1;
    HighDetailActors     = 0;
    SupportsTC           = 0;
    PrecacheOnFlip       = 0;
    SupportsLazyTextures = 0;
    PrefersDeferredLoad  = 0;
    DetailTextures       = 0;

    unguard;
}

// ============================================================================
// Init
// ============================================================================
UBOOL UXboxRenderDevice::Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
    guard(UXboxRenderDevice::Init);

    // Verify the 5558 Xbox D3D8 header won the include-guard race.
    // The final 16 bytes must be app-provided surface pointers; CXBX-R reads
    // those fields during CreateDevice and a wrong fallback header silently
    // sends it down the bogus 480x518 host-surface path.
    // VC71 has no C++11 static_assert; use the classic typedef-array trick.
    typedef int _xbox_d3dpp_size_check[ sizeof(D3DPRESENT_PARAMETERS) == 68 ? 1 : -1 ];
    typedef int _xbox_d3dpp_tail_check[ sizeof(((D3DPRESENT_PARAMETERS*)0)->BufferSurfaces) == (sizeof(void*) * 3) ? 1 : -1 ];
    typedef int _xbox_d3d_sdk_check[ D3D_SDK_VERSION == 0 ? 1 : -1 ];

    GXboxLog.Write( "XboxRender::Init: entered (%dx%d, %dbpp) sizeof(D3DPP)=%d",
        NewX, NewY, NewColorBytes, (int)sizeof(D3DPRESENT_PARAMETERS) );
    GXboxLog.Write( "XboxRender::Init: render-audit build marker %s %s", __DATE__, __TIME__ );
    GXboxLog.Write( "XboxRender::Init: fog maps disabled; DXT/S3TC disabled until tall compressed textures are rotated safely" );
    if( GWireframeNoTextureProbe )
        GXboxLog.Write( "RWIRE probe active: no Info.Load, no texture create/lock/upload/bind, no light/detail/fog/macro passes, forced white wireframe" );

    Viewport      = InViewport;
    Direct3D      = NULL;
    Device        = NULL;
    BackBuffer    = NULL;
    DepthBuffer   = NULL;
    DrawVertexBuffer = NULL;
    DrawVBBytes   = 0;
    DrawVBOffset  = 0;
    DeviceCreated = 0;
    SceneOpen     = 0;
    CurrentFrame  = NULL;
    CurrentPolyFlags = 0;
    FrameCounter  = 0;
    TexPoolNext   = 0;
    TexLiveBytes  = 0;
    appMemzero( TexCache, sizeof(TexCache) );
    appMemzero( TexPool,  sizeof(TexPool) );
    BoundCacheID[0] = 0;
    BoundCacheID[1] = 0;
    StageUIndex[0] = StageUIndex[1] = 0;
    StageVIndex[0] = StageVIndex[1] = 1;
    appMemzero( CachedRenderState, sizeof(CachedRenderState) );
    appMemzero( CachedRenderStateValid, sizeof(CachedRenderStateValid) );
    appMemzero( CachedTextureStageState, sizeof(CachedTextureStageState) );
    appMemzero( CachedTextureStageStateValid, sizeof(CachedTextureStageStateValid) );
    CachedVertexShader = 0;
    CachedVertexShaderValid = 0;
    GRD_TotalTexUploads = 0;
    GRD_TotalTexCreates = 0;
    GRD_TotalTexSkipped = 0;
    GRD_TotalSceneSplits = 0;
    GRD_TotalVBLocks    = 0;
    GRD_TotalVBWraps    = 0;
    GRD_TotalStateSets  = 0;
    GRD_TotalStateSkips = 0;
    GRD_TotalTexBytes   = 0;
    GRD_TallTexLogCount = 0;
    GRD_ClampPadLogCount = 0;
    GRD_Rgba7MaxLogCount = 0;
    GRD_DxtUnexpectedLogCount = 0;
    GRD_LastOp          = "Init";

    // Z-buffer formula: SZ = ProjZRatio + ProjZOffset * RHW
    zNear      = 1.f;
    zFar       = 32767.f;
    ProjZRatio  = zFar / (zFar - zNear);
    ProjZOffset = -ProjZRatio * zNear;

    // Create Direct3D. Pass 0 NOT D3D_SDK_VERSION ??? per TFE's research
    // (renderBackend_xbox.cpp:181-186): in this XDK <d3d8.h> resolves to the
    // PC header where D3D_SDK_VERSION=120, but Xbox / CXBX-R HLE expect 0.
    // Passing 120 makes CXBX-R fall back to a non-Xbox surface allocator
    // that returns 480x518 LIN_X8R8G8B8 instead of the requested 640x480.
    GXboxLog.Write( "XboxRender::Init: calling Direct3DCreate8(0)" );
    Direct3D = Direct3DCreate8( 0 );
    if( !Direct3D )
    {
        GXboxLog.Write( "XboxRender::Init: Direct3DCreate8 FAILED" );
        debugf( NAME_Init, TEXT("XboxRender: Direct3DCreate8 failed") );
        return 0;
    }
    GXboxLog.Write( "XboxRender::Init: Direct3DCreate8 OK (ptr=0x%08X)", (DWORD)Direct3D );

    // Setup present parameters
    D3DPRESENT_PARAMETERS PP;
    appMemzero( &PP, sizeof(PP) );
    // Aligned to xQuake/OpenJKDF2/TheForceEngine ??? all 3 Xbox D3D8 ports use these:
    //   swizzled X8R8G8B8 backbuffer (CRTC requires swizzled ??? LIN_ broke sample),
    //   D24S8 depth+stencil (required for the always-clear-all-three-targets rule
    //   below; partial Clear crashes NV20 HLE per xQuake gl_draw.c:1829),
    //   IMMEDIATE present, single backbuffer, DISCARD swap.
    // Canonical Xbox D3D8 PP ??? identical to OpenJKDF2 fakeglx.cpp:1579-1591
    // and xQuake gl_fakegl.cpp:1579-1591. Earlier diagnostic values
    // (BackBufferCount=2 + INTERVAL_ONE) were left over from when we thought
    // scanout was the issue; with CXBX-R HLE now properly engaged the
    // canonical 1 + IMMEDIATE is what the hooked Swap implementation expects.
    PP.BackBufferWidth  = 640;
    PP.BackBufferHeight = 480;
    PP.BackBufferFormat = D3DFMT_X8R8G8B8;
    PP.BackBufferCount  = 1;
    PP.MultiSampleType  = D3DMULTISAMPLE_NONE;
    PP.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    PP.hDeviceWindow    = NULL;
    PP.Windowed         = FALSE;
    PP.EnableAutoDepthStencil = TRUE;
    PP.AutoDepthStencilFormat = D3DFMT_D24S8;
    PP.FullScreen_RefreshRateInHz      = 60;
    PP.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    // xQuake (MS official sample) does NOT call SetPushBufferSize ??? it relies on
    // the runtime defaults. Earlier we called it explicitly with custom sizes;
    // those sizes appear to collide with the auto-depth-stencil allocation,
    // leaving the stencil plane uninitialized so the Xbox D3D8 lib rejects our
    // subsequent Clear(D3DCLEAR_STENCIL) with "Invalid flags passed to Clear".

    // Create device
    GXboxLog.Write( "XboxRender::Init: calling CreateDevice()" );
    HRESULT hr = Direct3D->CreateDevice(
        0,
        D3DDEVTYPE_HAL,
        NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
        &PP,
        &Device
    );
    if( FAILED(hr) )
    {
        GXboxLog.Write( "XboxRender::Init: CreateDevice FAILED (hr=0x%08X)", hr );
        debugf( NAME_Init, TEXT("XboxRender: CreateDevice failed (0x%08X)"), hr );
        // B9: Direct3DCreate8 returns a sentinel on Xbox ??? NOT a COM object.
        // Calling Release() on it crashes. Just null the pointer.
        Direct3D = NULL;
        return 0;
    }
    GXboxLog.Write( "XboxRender::Init: CreateDevice OK (ptr=0x%08X)", (DWORD)Device );

    // B6: Reduce scan-line flicker on composite/RF output.
    D3DDevice_SetFlickerFilter( 5 );
    GXboxLog.Write( "XboxRender::Init: FlickerFilter(5) set" );

    DeviceCreated = 1;

    // Back-buffer warm-up (TFE renderBackend_xbox.cpp:215-231): query the
    // back buffer's desc and release. This forces the device to fully realize
    // the back buffer AND its auto-allocated depth-stencil surface before any
    // subsequent rendering ??? without it the first Clear can hit the Xbox D3D8
    // lib's "Invalid flags" path because the DS surface isn't ready yet.
    ActualBackBufferW = 640;  // fall-back if GetDesc fails
    ActualBackBufferH = 480;
    {
        IDirect3DSurface8* pBackBuffer = NULL;
        if( SUCCEEDED(Device->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer)) && pBackBuffer )
        {
            D3DSURFACE_DESC desc;
            pBackBuffer->GetDesc(&desc);
            ActualBackBufferW = desc.Width;
            ActualBackBufferH = desc.Height;
            GXboxLog.Write( "XboxRender::Init: warm-up backbuffer %ux%u fmt=%d",
                (unsigned)desc.Width, (unsigned)desc.Height, (int)desc.Format );
            pBackBuffer->Release();
        }
        IDirect3DSurface8* pDS = NULL;
        if( SUCCEEDED(Device->GetDepthStencilSurface(&pDS)) && pDS )
        {
            D3DSURFACE_DESC desc;
            pDS->GetDesc(&desc);
            GXboxLog.Write( "XboxRender::Init: warm-up depth-stencil %ux%u fmt=%d",
                (unsigned)desc.Width, (unsigned)desc.Height, (int)desc.Format );
            pDS->Release();
        }
    }
    BackBuffer  = NULL;  // We don't hold long-lived surface refs.
    DepthBuffer = NULL;

    // NOTE: render/texture-stage state is deliberately NOT set here. Every
    // working Xbox D3D8 reference (TFE renderBackend_xbox.cpp:426+, MS
    // AlphaFog AlphaFog.cpp:376+) sets state AFTER the first Clear, not
    // before. When we set state between CreateDevice and the first Clear,
    // the Xbox D3D8 lib's Clear validator rejects D3DCLEAR_STENCIL with
    // "Invalid flags passed to Clear" (despite a valid LIN_D24S8 auto-DS).
    // State setup is now performed inside Lock(), right after Clear.

    GXboxLog.Write( "XboxRender::Init: complete" );
    debugf( NAME_Init, TEXT("XboxRender: D3D8 device created (640x480)") );

    return 1;

    unguard;
}

// ============================================================================
// SetRes
// ============================================================================
UBOOL UXboxRenderDevice::SetRes( INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
    guard(UXboxRenderDevice::SetRes);
    // Xbox is always 640x480 fullscreen
    return 1;
    unguard;
}

// ============================================================================
// Exit
// ============================================================================
void UXboxRenderDevice::Exit()
{
    guard(UXboxRenderDevice::Exit);

    GXboxLog.Write( "XboxRender::Exit: releasing D3D objects" );

    FlushTexCache();

    ReleaseDrawVertexBuffer();
    if( DepthBuffer )  { DepthBuffer->Release();  DepthBuffer  = NULL; }
    if( BackBuffer )   { BackBuffer->Release();    BackBuffer   = NULL; }
    if( Device )       { Device->Release();        Device       = NULL; }
    // B9: Direct3DCreate8 returns a sentinel on Xbox (not a COM object) ??? do NOT Release it.
    Direct3D = NULL;
    DeviceCreated = 0;

    GXboxLog.Write( "XboxRender::Exit: done" );
    debugf( NAME_Init, TEXT("XboxRender: Device released") );

    unguard;
}

// ============================================================================
// Flush ??? release all cached textures
// ============================================================================
void UXboxRenderDevice::Flush( UBOOL AllowPrecache )
{
    guard(UXboxRenderDevice::Flush);
    FlushDGPBatch( "Flush" );
    FlushDTBatch( "Flush" );
    FlushTexCache();
    unguard;
}

void UXboxRenderDevice::FlushTexCache()
{
    FlushDGPBatch( "FlushTexCache" );
    FlushDTBatch( "FlushTexCache" );
    if( Device )
    {
        for( INT Stage = 0; Stage < 4; Stage++ )
            Device->SetTexture( Stage, NULL );
    }
    BoundCacheID[0] = 0;
    BoundCacheID[1] = 0;

    for( INT i = 0; i < XBOX_TEX_CACHE_SIZE; i++ )
    {
        FXboxTexCacheEntry* Entry = TexCache[i];
        while( Entry )
        {
            RenderBlockAndReleaseTexture( Entry->pTexture );
            Entry = Entry->HashNext;
        }
        TexCache[i] = NULL;
    }
    TexPoolNext = 0;
    TexLiveBytes = 0;
    appMemzero( TexPool, sizeof(TexPool) );
}

void UXboxRenderDevice::ReleaseDrawVertexBuffer()
{
    if( Device )
        Device->SetStreamSource( 0, NULL, 0 );
    GRD_DrawVBStreamBound = 0;
    GRD_DrawVBStreamStride = 0;

    if( DrawVertexBuffer )
    {
        DrawVertexBuffer->BlockUntilNotBusy();
        DrawVertexBuffer->Release();
        DrawVertexBuffer = NULL;
    }

    DrawVBBytes = 0;
    DrawVBOffset = 0;
}

void UXboxRenderDevice::EndSceneForTextureUpload( const char* Reason )
{
    if( !Device || !SceneOpen )
        return;

    GRD_LastOp = "TexEndScene";
    HRESULT hrEnd = Device->EndScene();
    SceneOpen = 0;
    GRD_FrameSceneSplits++;
    GRD_TotalSceneSplits++;
    if( FAILED(hrEnd) || (GVerboseRenderPerfLog && (RenderBoundaryFrame( FrameCounter ) || (GRD_TotalSceneSplits <= 16) || ((GRD_TotalSceneSplits % 256) == 0))) )
        GXboxLog.Write( "RSCENE tex-end f=%d reason=%s hr=0x%08X frameSplits=%d totalSplits=%d",
            FrameCounter, Reason ? Reason : "?", (DWORD)hrEnd, GRD_FrameSceneSplits, GRD_TotalSceneSplits );
}

void UXboxRenderDevice::ResumeSceneAfterTextureUpload( const char* Reason )
{
    if( !Device || SceneOpen )
        return;

    GRD_LastOp = "TexBeginScene";
    HRESULT hrBegin = Device->BeginScene();
    SceneOpen = SUCCEEDED(hrBegin);
    if( FAILED(hrBegin) || (GVerboseRenderPerfLog && (RenderBoundaryFrame( FrameCounter ) || (GRD_TotalSceneSplits <= 16) || ((GRD_TotalSceneSplits % 256) == 0))) )
        GXboxLog.Write( "RSCENE tex-begin f=%d reason=%s hr=0x%08X open=%d",
            FrameCounter, Reason ? Reason : "?", (DWORD)hrBegin, SceneOpen );
}

HRESULT UXboxRenderDevice::DrawPrimitiveVB( D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* Vertices, UINT Stride, const char* OpName )
{
    if( !Device || !Vertices || !Stride )
        return E_FAIL;

    UINT VertexCount = 0;
    switch( PrimitiveType )
    {
        case D3DPT_TRIANGLEFAN: VertexCount = PrimitiveCount + 2; break;
        case D3DPT_LINELIST:    VertexCount = PrimitiveCount * 2; break;
        case D3DPT_TRIANGLELIST:VertexCount = PrimitiveCount * 3; break;
        case D3DPT_QUADLIST:    VertexCount = PrimitiveCount * 4; break;
        default:                VertexCount = PrimitiveCount + 2; break;
    }

    if( VertexCount == 0 || VertexCount > XBOX_MAX_DRAW_VERTS )
    {
        if( RenderShouldLogDrawFailure( FrameCounter ) )
            GXboxLog.Write( "RDRAW VB reject op=%s f=%d type=%d prim=%u verts=%u stride=%u max=%u",
                OpName ? OpName : "?", FrameCounter, (INT)PrimitiveType,
                (unsigned)PrimitiveCount, (unsigned)VertexCount, (unsigned)Stride,
                (unsigned)XBOX_MAX_DRAW_VERTS );
        return E_FAIL;
    }

    if( Stride != sizeof(FXboxWorldVertex) && Stride != sizeof(FXboxWorldVertex2) &&
        !RenderValidateTLVertices( Vertices, VertexCount, Stride, OpName, PrimitiveType, FrameCounter ) )
        return E_FAIL;

    if( !SceneOpen )
        ResumeSceneAfterTextureUpload( OpName ? OpName : "draw" );
    if( !SceneOpen )
        return E_FAIL;

    if( GWireframeNoTextureProbe )
    {
        Device->SetTexture( 0, NULL );
        Device->SetTexture( 1, NULL );
        BoundCacheID[0] = 0;
        BoundCacheID[1] = 0;
        SetCachedRenderState( D3DRS_FILLMODE, D3DFILL_WIREFRAME );
        SetCachedRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
        SetCachedRenderState( D3DRS_ZWRITEENABLE, TRUE );
        SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
        SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
        SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
        SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
    }

    UINT VertexBytes = VertexCount * Stride;
    if( VertexBytes > XBOX_DRAW_VB_BYTES )
    {
        GXboxLog.Write( "RVB reject-size op=%s f=%d bytes=%u limit=%u verts=%u stride=%u",
            OpName ? OpName : "?", FrameCounter, (unsigned)VertexBytes, (unsigned)XBOX_DRAW_VB_BYTES,
            (unsigned)VertexCount, (unsigned)Stride );
        return E_FAIL;
    }

    if( GUseDrawPrimitiveUP )
    {
        GRD_LastOp = OpName ? OpName : "DrawUP";
        HRESULT hrUP = Device->DrawPrimitiveUP( PrimitiveType, PrimitiveCount, Vertices, Stride );
        GRD_FrameUPDraws++;
        GRD_FrameVBBytes += VertexBytes;
        if( SUCCEEDED(hrUP) )
            return hrUP;

        GRD_FrameUPFails++;
        if( RenderShouldLogDrawFailure( FrameCounter ) )
            GXboxLog.Write( "RUP draw-failed op=%s f=%d type=%d prim=%u verts=%u stride=%u hr=0x%08X; falling back to VB",
                OpName ? OpName : "?", FrameCounter, (INT)PrimitiveType,
                (unsigned)PrimitiveCount, (unsigned)VertexCount, (unsigned)Stride, (DWORD)hrUP );
    }

    if( !DrawVertexBuffer )
    {
        DrawVBBytes = XBOX_DRAW_VB_BYTES;
        DrawVBOffset = 0;
        GRD_DrawVBStreamBound = 0;
        GRD_DrawVBStreamStride = 0;
        GRD_LastOp = "VBCreate";
        HRESULT hrCreate = Device->CreateVertexBuffer( DrawVBBytes, 0, 0, D3DPOOL_DEFAULT, &DrawVertexBuffer );
        GXboxLog.Write( "RVB create f=%d bytes=%u hr=0x%08X",
            FrameCounter, (unsigned)DrawVBBytes, (DWORD)hrCreate );
        if( FAILED(hrCreate) || !DrawVertexBuffer )
            return FAILED(hrCreate) ? hrCreate : E_FAIL;
    }

    UINT VertexOffset = ((DrawVBOffset + Stride - 1) / Stride) * Stride;
    DWORD LockFlags = D3DLOCK_NOOVERWRITE;
    if( VertexOffset + VertexBytes > DrawVBBytes )
    {
        DrawVertexBuffer->BlockUntilNotBusy();
        VertexOffset = 0;
        LockFlags = 0;
        GRD_FrameVBWraps++;
        GRD_TotalVBWraps++;
        if( GVerboseRenderPerfLog && (GRD_TotalVBWraps <= 16 || RenderBoundaryFrame( FrameCounter ) || ((GRD_TotalVBWraps % 256) == 0)) )
            GXboxLog.Write( "RVB wrap f=%d bytes=%u totalWraps=%d",
                FrameCounter, (unsigned)DrawVBBytes, GRD_TotalVBWraps );
    }

    BYTE* Dest = NULL;
    GRD_LastOp = "VBLock";
    HRESULT hrLock = DrawVertexBuffer->Lock( VertexOffset, VertexBytes, &Dest, LockFlags );
    GRD_FrameVBLocks++;
    GRD_TotalVBLocks++;
    GRD_FrameVBBytes += VertexBytes;
    if( FAILED(hrLock) || !Dest )
    {
        GXboxLog.Write( "RVB lock-failed op=%s f=%d offset=%u bytes=%u flags=0x%08X hr=0x%08X",
            OpName ? OpName : "?", FrameCounter, (unsigned)VertexOffset, (unsigned)VertexBytes, LockFlags, (DWORD)hrLock );
        return FAILED(hrLock) ? hrLock : E_FAIL;
    }

    appMemcpy( Dest, Vertices, VertexBytes );

    GRD_LastOp = "VBUnlock";
    HRESULT hrUnlock = DrawVertexBuffer->Unlock();
    if( FAILED(hrUnlock) )
    {
        GXboxLog.Write( "RVB unlock-failed op=%s f=%d offset=%u bytes=%u hr=0x%08X",
            OpName ? OpName : "?", FrameCounter, (unsigned)VertexOffset, (unsigned)VertexBytes, (DWORD)hrUnlock );
        return hrUnlock;
    }

    if( !GRD_DrawVBStreamBound || GRD_DrawVBStreamStride != Stride )
    {
        HRESULT hrStream = Device->SetStreamSource( 0, DrawVertexBuffer, Stride );
        if( FAILED(hrStream) )
        {
            GXboxLog.Write( "RVB stream-failed op=%s f=%d stride=%u hr=0x%08X",
                OpName ? OpName : "?", FrameCounter, (unsigned)Stride, (DWORD)hrStream );
            GRD_DrawVBStreamBound = 0;
            GRD_DrawVBStreamStride = 0;
            return hrStream;
        }
        GRD_DrawVBStreamBound = 1;
        GRD_DrawVBStreamStride = Stride;
    }

    GRD_LastOp = OpName ? OpName : "DrawVB";
    HRESULT hrDraw = Device->DrawPrimitive( PrimitiveType, VertexOffset / Stride, PrimitiveCount );
    DrawVBOffset = VertexOffset + VertexBytes;

    if( FAILED(hrDraw) )
        GXboxLog.Write( "RVB draw op=%s f=%d type=%d prim=%u verts=%u stride=%u start=%u bytes=%u flags=0x%08X hr=0x%08X locks=%d wraps=%d",
            OpName ? OpName : "?", FrameCounter, (INT)PrimitiveType,
            (unsigned)PrimitiveCount, (unsigned)VertexCount, (unsigned)Stride,
            (unsigned)(VertexOffset / Stride), (unsigned)VertexBytes, LockFlags, (DWORD)hrDraw,
            GRD_FrameVBLocks, GRD_FrameVBWraps );

    return hrDraw;
}

HRESULT UXboxRenderDevice::DrawPrimitiveVBWorld( D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* Vertices, UINT Stride, const char* OpName )
{
    return DrawPrimitiveVB( PrimitiveType, PrimitiveCount, Vertices, Stride, OpName );
}

void UXboxRenderDevice::FlushDGPBatch( const char* Reason )
{
    guard(UXboxRenderDevice::FlushDGPBatch);

    if( !GRD_DGPBatchActive || GRD_DGPBatchVerts <= 0 )
    {
        GRD_DGPBatchActive = 0;
        GRD_DGPBatchVerts = 0;
        GRD_DGPBatchPolys = 0;
        return;
    }

    if( Device )
    {
        SetCachedVertexShader( XBOX_FVF_WORLDVERTEX );
        HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLELIST, GRD_DGPBatchVerts / 3, GRD_DGPBatch, sizeof(FXboxWorldVertex), "DGP-batch" );
        if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
            GXboxLog.Write( "RDRAW FAILED op=DGP-batch frame=%d reason=%s polys=%d verts=%d hr=0x%08X flags=0x%08X",
                FrameCounter, Reason ? Reason : "?", GRD_DGPBatchPolys, GRD_DGPBatchVerts, (DWORD)hrDraw, GRD_DGPBatchPolyFlags );
        GRD_FrameDGPBatches++;
        GRD_FrameDGPBatchedPolys += GRD_DGPBatchPolys;
    }

    GRD_DGPBatchActive = 0;
    GRD_DGPBatchVerts = 0;
    GRD_DGPBatchPolys = 0;

    unguard;
}

void UXboxRenderDevice::FlushDTBatch( const char* Reason )
{
    guard(UXboxRenderDevice::FlushDTBatch);

    if( !GRD_DTBatchActive || GRD_DTBatchVerts <= 0 )
    {
        GRD_DTBatchActive = 0;
        GRD_DTBatchVerts = 0;
        GRD_DTBatchTiles = 0;
        return;
    }

    if( Device )
    {
        SetCachedVertexShader( XBOX_FVF_TLVERTEX );
        HRESULT hrDraw = DrawPrimitiveVB( D3DPT_TRIANGLELIST, GRD_DTBatchVerts / 3, GRD_DTBatch, sizeof(FXboxTLVertex), "DT-batch" );
        if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
            GXboxLog.Write( "RDRAW FAILED op=DT-batch frame=%d reason=%s tiles=%d verts=%d hr=0x%08X flags=0x%08X",
                FrameCounter, Reason ? Reason : "?", GRD_DTBatchTiles, GRD_DTBatchVerts, (DWORD)hrDraw, GRD_DTBatchPolyFlags );
        GRD_FrameDTBatches++;
        GRD_FrameDTBatchedTiles += GRD_DTBatchTiles;
    }

    GRD_DTBatchActive = 0;
    GRD_DTBatchVerts = 0;
    GRD_DTBatchTiles = 0;

    unguard;
}

void UXboxRenderDevice::DisableStage1()
{
    guard(UXboxRenderDevice::DisableStage1);

    if( Device && (BoundCacheID[1] != 0 || (CurrentPolyFlags & PF_Memorized)) )
    {
        SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
        Device->SetTexture( 1, NULL );
        BoundCacheID[1] = 0;
        CurrentPolyFlags &= ~PF_Memorized;
    }

    unguard;
}

HRESULT UXboxRenderDevice::SetCachedRenderState( D3DRENDERSTATETYPE State, DWORD Value )
{
    UINT Index = (UINT)State;
    if( Index < XBOX_RS_CACHE_COUNT && CachedRenderStateValid[Index] && CachedRenderState[Index] == Value )
    {
        GRD_FrameStateSkips++;
        GRD_TotalStateSkips++;
        return S_OK;
    }

    HRESULT hr = Device ? Device->SetRenderState( State, Value ) : E_FAIL;
    GRD_FrameStateSets++;
    GRD_TotalStateSets++;
    if( SUCCEEDED(hr) && Index < XBOX_RS_CACHE_COUNT )
    {
        CachedRenderStateValid[Index] = 1;
        CachedRenderState[Index] = Value;
    }
    return hr;
}

HRESULT UXboxRenderDevice::SetCachedTextureStageState( DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value )
{
    UINT Index = (UINT)Type;
    if( Stage < 2 && Index < XBOX_TSS_CACHE_COUNT &&
        CachedTextureStageStateValid[Stage][Index] &&
        CachedTextureStageState[Stage][Index] == Value )
    {
        GRD_FrameStateSkips++;
        GRD_TotalStateSkips++;
        return S_OK;
    }

    HRESULT hr = Device ? Device->SetTextureStageState( Stage, Type, Value ) : E_FAIL;
    GRD_FrameStateSets++;
    GRD_TotalStateSets++;
    if( SUCCEEDED(hr) && Stage < 2 && Index < XBOX_TSS_CACHE_COUNT )
    {
        CachedTextureStageStateValid[Stage][Index] = 1;
        CachedTextureStageState[Stage][Index] = Value;
    }
    return hr;
}

HRESULT UXboxRenderDevice::SetCachedVertexShader( DWORD Shader )
{
    if( CachedVertexShaderValid && CachedVertexShader == Shader )
    {
        GRD_FrameStateSkips++;
        GRD_TotalStateSkips++;
        return S_OK;
    }

    HRESULT hr = Device ? Device->SetVertexShader( Shader ) : E_FAIL;
    GRD_FrameStateSets++;
    GRD_TotalStateSets++;
    if( SUCCEEDED(hr) )
    {
        CachedVertexShaderValid = 1;
        CachedVertexShader = Shader;
    }
    return hr;
}

// ============================================================================
// Exec
// ============================================================================
UBOOL UXboxRenderDevice::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
    guard(UXboxRenderDevice::Exec);
    return 0;
    unguard;
}

// ============================================================================
// Lock
// ============================================================================
void UXboxRenderDevice::Lock( FPlane InFlashScale, FPlane InFlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
    guard(UXboxRenderDevice::Lock);

    static UBOOL bFirstLock = 1;
    if( bFirstLock )
    {
        GXboxLog.Write( "XboxRender::Lock: FIRST CALL Device=0x%08X flags=0x%08X", (DWORD)Device, RenderLockFlags );
        bFirstLock = 0;
    }

    if( !Device )
        return;

    if( TexLiveBytes > (12 * 1024 * 1024) || TexPoolNext > XBOX_TEX_RESIDENT_LIMIT )
    {
        GXboxLog.Write( "RTEX frame-flush frame=%d liveKB=%d pool=%d",
            FrameCounter, TexLiveBytes / 1024, TexPoolNext );
        Device->SetTexture( 0, NULL );
        Device->SetTexture( 1, NULL );
        FlushTexCache();
    }

    FrameCounter++;
    DOUBLE NowSeconds = appSeconds();
    GRD_FrameStartSeconds = NowSeconds;
    if( GRD_LastFrameStartSeconds > 0.0 )
        GRD_LastFrameMS = (FLOAT)((NowSeconds - GRD_LastFrameStartSeconds) * 1000.0);
    GRD_LastFrameStartSeconds = NowSeconds;

    FlashScale = InFlashScale;
    FlashFog   = InFlashFog;

    if( RenderFrameSummaryLog( FrameCounter ) )
        GXboxLog.Write( "RBEGIN f=%d liveKB=%d pool=%d availKB=%u flags=0x%08X",
            FrameCounter, TexLiveBytes / 1024, TexPoolNext, (unsigned)RenderAvailPhysKB(), RenderLockFlags );

    // Clear runs before BeginScene ??? MS official PolynomialTextureMaps.cpp:289
    // and xQuake gl_fakegl.cpp:1752-1760 both follow this pattern.
    HRESULT hrClear = S_OK;
    if( XboxSplitShouldClearRenderLock() )
    {
        hrClear = Device->Clear( 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
            0x00000000,
            1.0f, 0 );
    }

    HRESULT hrBegin = Device->BeginScene();
    SceneOpen = SUCCEEDED(hrBegin);

    if( RenderFrameSummaryLog( FrameCounter ) || FAILED(hrClear) || FAILED(hrBegin) )
        GXboxLog.Write( "RLOCK f=%d clear=0x%08X begin=0x%08X",
            FrameCounter, (DWORD)hrClear, (DWORD)hrBegin );

    GRD_FrameDCS        = 0;
    GRD_FrameDGP        = 0;
    GRD_FrameDT         = 0;
    GRD_FramePrims      = 0;
    GRD_FrameVerts      = 0;
    GRD_FrameTexBinds   = 0;
    GRD_FrameTexUploads = 0;
    GRD_FrameTexCreates = 0;
    GRD_FrameTexSkipped = 0;
    GRD_FrameTexUploadSeq = 0;
    GRD_FrameBadDraws   = 0;
    GRD_FrameBadVerts   = 0;
    GRD_FrameSceneSplits = 0;
    GRD_FrameVBLocks    = 0;
    GRD_FrameVBWraps    = 0;
    GRD_FrameVBBytes    = 0;
    GRD_FrameUPDraws    = 0;
    GRD_FrameUPFails    = 0;
    GRD_FrameDGPBatches = 0;
    GRD_FrameDGPBatchedPolys = 0;
    GRD_FrameDTBatches  = 0;
    GRD_FrameDTBatchedTiles = 0;
    GRD_FrameTexDeferred = 0;
    GRD_FrameStateSets  = 0;
    GRD_FrameStateSkips = 0;
    GRD_DGPBatchActive  = 0;
    GRD_DGPBatchVerts   = 0;
    GRD_DGPBatchPolys   = 0;
    GRD_DTBatchActive   = 0;
    GRD_DTBatchVerts    = 0;
    GRD_DTBatchTiles    = 0;
    GRD_HotTraceBudget  = RenderHotFrame( FrameCounter ) ? 180 : 0;
    GRD_MaxPolyVerts    = 0;
    GRD_LastOp          = "Lock";

    if( FAILED(hrClear) || FAILED(hrBegin) )
        GXboxLog.Write( "XboxRender::Lock frame=%d hrClear=0x%08X hrBegin=0x%08X",
            FrameCounter, (DWORD)hrClear, (DWORD)hrBegin );

    // Render/texture-stage state ??? set AFTER the first Clear (matches TFE
    // renderBackend_xbox.cpp:426+ and MS AlphaFog AlphaFog.cpp:376+).
    // Setting these in Init() before the first Clear poisons the Xbox D3D8
    // lib's Clear-flags validator and triggers "Invalid flags passed to Clear".
    static UBOOL bStateInit = 0;
    if( !bStateInit )
    {
        bStateInit = 1;
        SetCachedRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
        SetCachedRenderState( D3DRS_ZWRITEENABLE, TRUE );
        SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
        SetCachedRenderState( D3DRS_LIGHTING, FALSE );
        SetCachedRenderState( D3DRS_SPECULARENABLE, FALSE );
        SetCachedRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
        SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
        SetCachedRenderState( D3DRS_DITHERENABLE, TRUE );
        SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
        SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
        SetCachedRenderState( D3DRS_ALPHAREF, 127 );
        SetCachedRenderState( D3DRS_ALPHAFUNC, D3DCMP_GREATER );

        SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 0 );
        SetCachedTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
        SetCachedTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
        SetCachedTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
        SetCachedTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
        SetCachedTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
        SetCachedTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR );
        SetCachedTextureStageState( 1, D3DTSS_TEXCOORDINDEX, 1 );
        SetCachedTextureStageState( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
        SetCachedTextureStageState( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
        SetCachedTextureStageState( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
        SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

        SetCachedRenderState( D3DRS_FILLMODE, D3DFILL_SOLID );
    }

    unguard;
}

// ============================================================================
// Unlock
// ============================================================================
void UXboxRenderDevice::Unlock( UBOOL Blit )
{
    guard(UXboxRenderDevice::Unlock);

    if( !Device )
        return;

    FlushDGPBatch( "Unlock" );
    FlushDTBatch( "Unlock" );

    DOUBLE BeforeOverlaySeconds = appSeconds();
    GRD_FpsWindowFrames++;
    if( GRD_LastFrameMS > 0.0f )
        GRD_FpsWindowSeconds += (DOUBLE)GRD_LastFrameMS * 0.001;
    if( GRD_FpsWindowSeconds >= 0.50 )
    {
        GRD_DisplayFPS = (FLOAT)((DOUBLE)GRD_FpsWindowFrames / GRD_FpsWindowSeconds);
        GRD_FpsWindowFrames = 0;
        GRD_FpsWindowSeconds = 0.0;
    }

    if( !SceneOpen )
        ResumeSceneAfterTextureUpload( "perf-overlay" );
    if( SceneOpen )
        DrawPerfOverlay();

    HRESULT hrEnd = S_OK;
    if( SceneOpen )
    {
        hrEnd = Device->EndScene();
        SceneOpen = 0;
    }
    Device->SetStreamSource( 0, NULL, 0 );
    GRD_DrawVBStreamBound = 0;
    GRD_DrawVBStreamStride = 0;

    GRD_LastOp = "Unlock";
    DOUBLE BeforePresentSeconds = appSeconds();
    GRD_LastRenderMS = (FLOAT)((BeforePresentSeconds - GRD_FrameStartSeconds) * 1000.0);

    // Present(NULL,NULL,NULL,NULL) ??? exact call used by xQuake gl_fakegl.cpp:2567
    // and MS XDK samples. The retail Xbox D3D8 lib equates this to swap-chain
    // flip + frame fence; earlier Swap(0) was a lower-level backbuffer rotate
    // that didn't push to the CRTC.
    HRESULT hrPresent = Blit ? Device->Present( NULL, NULL, NULL, NULL ) : S_OK;
    DOUBLE AfterPresentSeconds = appSeconds();
    GRD_LastPresentMS = (FLOAT)((AfterPresentSeconds - BeforePresentSeconds) * 1000.0);

    if( GRD_LastPerfLogSeconds == 0.0 )
        GRD_LastPerfLogSeconds = AfterPresentSeconds;
    if( AfterPresentSeconds - GRD_LastPerfLogSeconds >= 2.0 )
    {
        GXboxLog.Write( "PERF fps=%.1f frameMS=%.2f renderMS=%.2f presentMS=%.2f DCS=%d DGP=%d DT=%d prim=%d verts=%d dgpBatch=%d/%d dtBatch=%d/%d vbLocks=%d vbWraps=%d up=%d/%d vbKB=%d state=%d/%d texBind=%d texNew=%d texUp=%d texDef=%d splits=%d liveKB=%d texKB=%d availKB=%u",
            GRD_DisplayFPS, GRD_LastFrameMS, GRD_LastRenderMS, GRD_LastPresentMS,
            GRD_FrameDCS, GRD_FrameDGP, GRD_FrameDT, GRD_FramePrims, GRD_FrameVerts,
            GRD_FrameDGPBatches, GRD_FrameDGPBatchedPolys, GRD_FrameDTBatches, GRD_FrameDTBatchedTiles,
            GRD_FrameVBLocks, GRD_FrameVBWraps, GRD_FrameUPDraws, GRD_FrameUPFails, GRD_FrameVBBytes / 1024,
            GRD_FrameStateSets, GRD_FrameStateSkips,
            GRD_FrameTexBinds, GRD_FrameTexCreates, GRD_FrameTexUploads, GRD_FrameTexDeferred, GRD_FrameSceneSplits,
            TexLiveBytes / 1024, GRD_TotalTexBytes / 1024, (unsigned)RenderAvailPhysKB() );
        GRD_LastPerfLogSeconds = AfterPresentSeconds;
    }

    if( FAILED(hrEnd) || FAILED(hrPresent) || RenderFrameSummaryLog( FrameCounter ) || GRD_FrameTexSkipped )
        GXboxLog.Write( "RFRAME f=%d end=0x%08X present=0x%08X DCS=%d DGP=%d DT=%d prim=%d verts=%d maxPoly=%d badDraw=%d badVert=%d totalBad=%d splits=%d totalSplits=%d vbLocks=%d vbWraps=%d vbKB=%d totalVBLocks=%d totalVBWraps=%d texBind=%d texNew=%d texUp=%d texSkip=%d totalNew=%d totalUp=%d totalSkip=%d liveKB=%d texKB=%d availKB=%u lastTex=%08X:%08X flags=0x%08X last=%s",
            FrameCounter, (DWORD)hrEnd, (DWORD)hrPresent,
            GRD_FrameDCS, GRD_FrameDGP, GRD_FrameDT, GRD_FramePrims, GRD_FrameVerts, GRD_MaxPolyVerts,
            GRD_FrameBadDraws, GRD_FrameBadVerts, GRD_TotalBadDraws, GRD_FrameSceneSplits, GRD_TotalSceneSplits,
            GRD_FrameVBLocks, GRD_FrameVBWraps, GRD_FrameVBBytes / 1024, GRD_TotalVBLocks, GRD_TotalVBWraps,
            GRD_FrameTexBinds, GRD_FrameTexCreates, GRD_FrameTexUploads, GRD_FrameTexSkipped,
            GRD_TotalTexCreates, GRD_TotalTexUploads, GRD_TotalTexSkipped, TexLiveBytes / 1024, GRD_TotalTexBytes / 1024,
            (unsigned)RenderAvailPhysKB(), GRD_LastTextureIDHi, GRD_LastTextureIDLo, GRD_LastPolyFlags, GRD_LastOp );

    unguard;
}

// ============================================================================
// SetSceneNode
// ============================================================================
void UXboxRenderDevice::SetSceneNode( FSceneNode* Frame )
{
    guard(UXboxRenderDevice::SetSceneNode);

    FlushDGPBatch( "SetSceneNode" );
    FlushDTBatch( "SetSceneNode" );

    static UBOOL bFirstSetSceneNode = 1;
    if( bFirstSetSceneNode )
    {
        GXboxLog.Write( "XboxRender::SetSceneNode: FIRST CALL Device=0x%08X Frame=0x%08X engine XB=%d YB=%d X=%d Y=%d, actual BB=%ux%u",
            (DWORD)Device, (DWORD)Frame, (int)Frame->XB, (int)Frame->YB, (int)Frame->X, (int)Frame->Y,
            ActualBackBufferW, ActualBackBufferH );
        bFirstSetSceneNode = 0;
    }

    if( !Device || !Frame )
        return;

    CurrentFrame = Frame;
    RProjZ = Frame->RProj.Z;

    // Clamp the D3D viewport to the ACTUAL backbuffer extents. The engine
    // drives Frame->X/Y at its logical 640x480; CXBX-R may have allocated
    // a smaller surface (e.g. 480x518). Setting a viewport larger than the
    // current render target makes CXBX-R's HLE silently discard the draw
    // (including Clear when rects=NULL, which clears the viewport region).
    INT vpX = Frame->XB;
    INT vpY = Frame->YB;
    INT vpW = Frame->X;
    INT vpH = Frame->Y;
    if( vpX < 0 ) vpX = 0;
    if( vpY < 0 ) vpY = 0;
    if( (UINT)(vpX + vpW) > ActualBackBufferW ) vpW = (INT)ActualBackBufferW - vpX;
    if( (UINT)(vpY + vpH) > ActualBackBufferH ) vpH = (INT)ActualBackBufferH - vpY;
    if( vpW < 1 ) vpW = 1;
    if( vpH < 1 ) vpH = 1;

    D3DVIEWPORT8 vp;
    vp.X      = (DWORD)vpX;
    vp.Y      = (DWORD)vpY;
    vp.Width  = (DWORD)vpW;
    vp.Height = (DWORD)vpH;
    vp.MinZ   = 0.0f;
    vp.MaxZ   = 1.0f;
    Device->SetViewport( &vp );

    D3DMATRIX Identity;
    appMemzero( &Identity, sizeof(Identity) );
    Identity._11 = 1.0f;
    Identity._22 = 1.0f;
    Identity._33 = 1.0f;
    Identity._44 = 1.0f;
    Device->SetTransform( D3DTS_WORLD, &Identity );
    Device->SetTransform( D3DTS_VIEW,  &Identity );
    SetCachedRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
    SetCachedRenderState( D3DRS_ZWRITEENABLE, TRUE );
    SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );

    D3DMATRIX Projection;
    appMemzero( &Projection, sizeof(Projection) );
    Projection._11 = (Frame->X > 0) ? (2.0f * Frame->Proj.Z / (FLOAT)Frame->X) : 1.0f;
    Projection._22 = (Frame->Y > 0) ? (-2.0f * Frame->Proj.Z / (FLOAT)Frame->Y) : -1.0f;
    Projection._33 = ProjZRatio;
    Projection._34 = 1.0f;
    Projection._43 = ProjZOffset;
    Projection._44 = 0.0f;
    Device->SetTransform( D3DTS_PROJECTION, &Projection );

    static INT ProjectionLogCount = 0;
    if( ProjectionLogCount < 4 )
    {
        ProjectionLogCount++;
        GXboxLog.Write( "RPROJ gpu f=%d #%d frame=%dx%d projZ=%.6f m11=%.6f m22=%.6f zNear=%.3f zFar=%.1f",
            FrameCounter, ProjectionLogCount, Frame->X, Frame->Y, Frame->Proj.Z,
            Projection._11, Projection._22, zNear, zFar );
    }

    unguard;
}

// ============================================================================
// SetBlending ??? port of D3D7 SetBlending (lines 1315-1393)
// ============================================================================
void UXboxRenderDevice::SetBlending( DWORD PolyFlags )
{
    // Adjust PolyFlags according to Unreal's precedence rules.
    if( !(PolyFlags & (PF_Translucent|PF_Modulated)) )
        PolyFlags |= PF_Occlude;
    else if( PolyFlags & PF_Translucent )
        PolyFlags &= ~PF_Masked;

    // Detect changes in the blending modes.
    DWORD Xor = CurrentPolyFlags ^ PolyFlags;
    if( Xor & (PF_Translucent|PF_Modulated|PF_Invisible|PF_Occlude|PF_Masked|PF_Highlighted|PF_NoSmooth|PF_Memorized) )
    {
        if( Xor & (PF_Invisible|PF_Translucent|PF_Modulated|PF_Highlighted) )
        {
            if( !(PolyFlags & (PF_Invisible|PF_Translucent|PF_Modulated|PF_Highlighted)) )
            {
                SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
            }
            else if( PolyFlags & PF_Invisible )
            {
                SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                SetCachedRenderState( D3DRS_SRCBLEND,  D3DBLEND_ZERO );
                SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_ONE );
            }
            else if( PolyFlags & PF_Translucent )
            {
                SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                SetCachedRenderState( D3DRS_SRCBLEND,  D3DBLEND_ONE );
                SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR );
            }
            else if( PolyFlags & PF_Modulated )
            {
                SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                SetCachedRenderState( D3DRS_SRCBLEND,  D3DBLEND_DESTCOLOR );
                SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR );
            }
            else if( PolyFlags & PF_Highlighted )
            {
                SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                SetCachedRenderState( D3DRS_SRCBLEND,  D3DBLEND_ONE );
                SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
            }
        }
        if( Xor & PF_Occlude )
        {
            SetCachedRenderState( D3DRS_ZWRITEENABLE, (PolyFlags & PF_Occlude) != 0 );
        }
        if( Xor & PF_Masked )
        {
            SetCachedRenderState( D3DRS_ALPHATESTENABLE, (PolyFlags & PF_Masked) != 0 );
        }
        if( Xor & PF_NoSmooth )
        {
            SetCachedTextureStageState( 0, D3DTSS_MAGFILTER, (PolyFlags & PF_NoSmooth) ? D3DTEXF_POINT : D3DTEXF_LINEAR );
            SetCachedTextureStageState( 0, D3DTSS_MINFILTER, (PolyFlags & PF_NoSmooth) ? D3DTEXF_POINT : D3DTEXF_LINEAR );
        }
        if( Xor & PF_Memorized )
        {
            SetCachedTextureStageState( 1, D3DTSS_COLOROP, (PolyFlags & PF_Memorized) ? D3DTOP_MODULATE : D3DTOP_DISABLE );
            SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, (PolyFlags & PF_Memorized) ? D3DTOP_SELECTARG2 : D3DTOP_DISABLE );
        }
        CurrentPolyFlags = PolyFlags;
    }
}

// ============================================================================
// SetTextureD3D ??? simplified texture cache for Xbox D3D8
// ============================================================================
void UXboxRenderDevice::SetTextureD3D( INT Stage, FTextureInfo& Info, DWORD PolyFlags )
{
    guard(UXboxRenderDevice::SetTextureD3D);

    GRD_LastOp = "SetTexture";
    GRD_LastTextureIDLo = (DWORD)Info.CacheID;
    GRD_LastTextureIDHi = (DWORD)(Info.CacheID >> 32);

    if( GWireframeNoTextureProbe )
    {
        Device->SetTexture( Stage, NULL );
        BoundCacheID[Stage] = 0;
        INT USize = Info.Mips[0] ? Info.Mips[0]->USize : Info.USize;
        INT VSize = Info.Mips[0] ? Info.Mips[0]->VSize : Info.VSize;
        if( USize < 1 ) USize = 1;
        if( VSize < 1 ) VSize = 1;
        FLOAT UScale = Info.UScale > 1.0f ? Info.UScale : 1.0f;
        FLOAT VScale = Info.VScale > 1.0f ? Info.VScale : 1.0f;
        StageUScale[Stage] = 1.0f / (FLOAT)(USize * UScale);
        StageVScale[Stage] = 1.0f / (FLOAT)(VSize * VScale);
        StageUIndex[Stage] = 0;
        StageVIndex[Stage] = 1;
        return;
    }

    // Early out if texture already bound.
    UBOOL bRgba7NeedsMaxColor = (Info.Format == TEXF_RGBA7 && Info.MaxColor && GET_COLOR_DWORD(*Info.MaxColor) == 0xFFFFFFFF);

    if( BoundCacheID[Stage] == Info.CacheID && !Info.bRealtimeChanged && !bRgba7NeedsMaxColor )
    {
        return;
    }

    // Look up in hash table.
    INT HashIndex = ((7 * (DWORD)Info.CacheID) + (DWORD)(Info.CacheID >> 32)) & (XBOX_TEX_CACHE_SIZE - 1);
    FXboxTexCacheEntry* Entry;
    for( Entry = TexCache[HashIndex]; Entry; Entry = Entry->HashNext )
    {
        if( Entry->CacheID == Info.CacheID )
            break;
    }

    if( Entry && Entry->pTexture && GDeferMidSceneRealtimeTextureUpdates && (Info.bRealtimeChanged || bRgba7NeedsMaxColor) )
    {
        if( bRgba7NeedsMaxColor )
            Info.CacheMaxColor();
        Info.bRealtimeChanged = 0;
        bRgba7NeedsMaxColor = 0;
        GRD_FrameTexDeferred++;
        GRD_TotalTexDeferred++;
        if( GVerboseRenderPerfLog && (GRD_TotalTexDeferred <= 16 || (GRD_TotalTexDeferred % 256) == 0) )
            GXboxLog.Write( "RTEX defer-update f=%d total=%d stage=%d id=%08X:%08X fmt=%d tex=0x%08X",
                FrameCounter, GRD_TotalTexDeferred, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                Info.Format, (DWORD)Entry->pTexture );
        if( BoundCacheID[Stage] == Info.CacheID )
        {
            StageUScale[Stage]  = Entry->UScale;
            StageVScale[Stage]  = Entry->VScale;
            StageUIndex[Stage]  = Entry->UIndex;
            StageVIndex[Stage]  = Entry->VIndex;
            Entry->FrameCounter = FrameCounter;
            return;
        }
    }

    if( !Entry || Info.bRealtimeChanged || bRgba7NeedsMaxColor )
    {
        // Need to create or update the texture.
        EndSceneForTextureUpload( !Entry ? "tex-create" : "tex-update" );
        UBOOL bRealtimeChanged = Info.bRealtimeChanged;
        UBOOL bForceRgba7MaxUpload = bRgba7NeedsMaxColor;
        if( !Entry )
        {
            // Allocate from pool, then reuse oldest entries once the resident
            // set is warm. Stock D3D7 keeps fixed texture pools and replaces
            // old cache entries instead of endlessly creating new objects.
            if( TexPoolNext >= XBOX_TEX_CACHE_SIZE )
            {
                FlushTexCache();
                HashIndex = ((7 * (DWORD)Info.CacheID) + (DWORD)(Info.CacheID >> 32)) & (XBOX_TEX_CACHE_SIZE - 1);
            }
            if( TexPoolNext < XBOX_TEX_RESIDENT_LIMIT )
            {
                Entry = &TexPool[TexPoolNext++];
            }
            else
            {
                INT BestIndex = -1;
                INT BestAge = -1;
                for( INT i = 0; i < TexPoolNext; i++ )
                {
                    FXboxTexCacheEntry* Candidate = &TexPool[i];
                    if( Candidate->CacheID == BoundCacheID[0] || Candidate->CacheID == BoundCacheID[1] )
                        continue;
                    INT Age = FrameCounter - Candidate->FrameCounter;
                    if( Age > BestAge )
                    {
                        BestAge = Age;
                        BestIndex = i;
                    }
                }
                if( BestIndex < 0 )
                    BestIndex = 0;

                Entry = &TexPool[BestIndex];
                INT OldHash = ((7 * (DWORD)Entry->CacheID) + (DWORD)(Entry->CacheID >> 32)) & (XBOX_TEX_CACHE_SIZE - 1);
                FXboxTexCacheEntry** Link = &TexCache[OldHash];
                while( *Link && *Link != Entry )
                    Link = &(*Link)->HashNext;
                if( *Link == Entry )
                    *Link = Entry->HashNext;

                if( Entry->pTexture )
                {
                    for( INT Stage = 0; Stage < 4; Stage++ )
                        Device->SetTexture( Stage, NULL );
                    BoundCacheID[0] = 0;
                    BoundCacheID[1] = 0;
                    RenderBlockAndReleaseTexture( Entry->pTexture );
                }
                TexLiveBytes -= Entry->Bytes;
                Entry->Bytes = 0;
                if( GRD_TotalTexCreates <= 16 || (GRD_TotalTexCreates % 128) == 0 )
                    GXboxLog.Write( "RTEX reuse frame=%d slot=%d age=%d pool=%d liveKB=%d availKB=%u old=%08X:%08X",
                        FrameCounter, BestIndex, BestAge, TexPoolNext, TexLiveBytes / 1024,
                        (unsigned)RenderAvailPhysKB(), (DWORD)(Entry->CacheID >> 32), (DWORD)Entry->CacheID );
            }
            Entry->CacheID   = Info.CacheID;
            Entry->pTexture  = NULL;
            Entry->USize     = 0;
            Entry->VSize     = 0;
            Entry->NumMips   = 0;
            Entry->FirstMip  = 0;
            Entry->UIndex    = 0;
            Entry->VIndex    = 1;
            Entry->Format    = D3DFMT_UNKNOWN;
            Entry->Bytes     = 0;
            Entry->HashNext  = TexCache[HashIndex];
            TexCache[HashIndex] = Entry;
        }

        // Determine mip 0 dimensions and first mip to use.
        INT FirstMip = 0;
        INT SrcUSize = Info.Mips[0] ? Info.Mips[0]->USize : Info.USize;
        INT SrcVSize = Info.Mips[0] ? Info.Mips[0]->VSize : Info.VSize;

        // Clamp oversized textures by skipping mips.
        while( (SrcUSize > 1024 || SrcVSize > 1024) && FirstMip + 1 < Info.NumMips )
        {
            FirstMip++;
            SrcUSize = Info.Mips[FirstMip]->USize;
            SrcVSize = Info.Mips[FirstMip]->VSize;
        }
        // PC D3D7 transposed tall textures to satisfy old surface-pool
        // constraints. Xbox D3D8 accepts rectangular power-of-two swizzled
        // textures directly; keeping the source orientation avoids making the
        // base texture upload path disagree with the UT surface UVs.
        UBOOL bSwapUV = 0;
        INT USize = SrcUSize;
        INT VSize = SrcVSize;

        if( SrcUSize < 1 || SrcVSize < 1 || USize < 1 || VSize < 1 || USize > 1024 || VSize > 1024 )
        {
            GRD_FrameTexSkipped++;
            GRD_TotalTexSkipped++;
            GXboxLog.Write( "RTEX reject-dims f=%d stage=%d id=%08X:%08X fmt=%d src=%dx%d dst=%dx%d mips=%d first=%d",
                FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                Info.Format, SrcUSize, SrcVSize, USize, VSize, Info.NumMips, FirstMip );
            Device->SetTexture( Stage, NULL );
            BoundCacheID[Stage] = 0;
            StageUScale[Stage] = 1.0f;
            StageVScale[Stage] = 1.0f;
            StageUIndex[Stage] = 0;
            StageVIndex[Stage] = 1;
            ResumeSceneAfterTextureUpload( "tex-reject" );
            return;
        }

        if( bSwapUV && GRD_TallTexLogCount < 24 )
        {
            GRD_TallTexLogCount++;
            GXboxLog.Write( "RTEX tall-swap #%d f=%d stage=%d id=%08X:%08X fmt=%d src=%dx%d dst=%dx%d",
                GRD_TallTexLogCount, FrameCounter, Stage,
                GRD_LastTextureIDHi, GRD_LastTextureIDLo, Info.Format,
                SrcUSize, SrcVSize, USize, VSize );
        }
        if( Info.Format == TEXF_DXT1 && GRD_DxtUnexpectedLogCount < 8 )
        {
            GRD_DxtUnexpectedLogCount++;
            GXboxLog.Write( "RTEX unexpected-dxt #%d f=%d stage=%d id=%08X:%08X size=%dx%d SupportsTC=0",
                GRD_DxtUnexpectedLogCount, FrameCounter, Stage,
                GRD_LastTextureIDHi, GRD_LastTextureIDLo, SrcUSize, SrcVSize );
        }

        // Compute UV scale (maps UT99 texcoords to 0..1 range for D3D).
        FLOAT SafeInfoUScale = (RenderFiniteFloat(Info.UScale) && Info.UScale > 0.000001f) ? Info.UScale : 1.0f;
        FLOAT SafeInfoVScale = (RenderFiniteFloat(Info.VScale) && Info.VScale > 0.000001f) ? Info.VScale : 1.0f;
        if( SafeInfoUScale != Info.UScale || SafeInfoVScale != Info.VScale )
            GXboxLog.Write( "RTEX scale-sanitize f=%d stage=%d id=%08X:%08X us=%.6f vs=%.6f",
                FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, Info.UScale, Info.VScale );
        Entry->UScale = 1.0f / (FLOAT)(SrcUSize * Max((INT)1, (INT)(1 << FirstMip)) * SafeInfoUScale);
        Entry->VScale = 1.0f / (FLOAT)(SrcVSize * Max((INT)1, (INT)(1 << FirstMip)) * SafeInfoVScale);
        Entry->UIndex = bSwapUV ? 1 : 0;
        Entry->VIndex = bSwapUV ? 0 : 1;

        // Ensure mipmap data is loaded. Because this renderer advertises no
        // lazy/deferred loading, do not unload here; stock D3D only unloads
        // when its lazy texture cache marks that entry as unloaded.
        Info.Load();
        if( Info.Format == TEXF_RGBA7 && Info.MaxColor )
        {
            DWORD BeforeMaxColor = GET_COLOR_DWORD(*Info.MaxColor);
            Info.CacheMaxColor();
            if( GVerboseRenderPerfLog && BeforeMaxColor == 0xFFFFFFFF && GRD_Rgba7MaxLogCount < 24 )
            {
                GRD_Rgba7MaxLogCount++;
                GXboxLog.Write( "RTEX rgba7-max #%d f=%d stage=%d id=%08X:%08X size=%dx%d clamp=%dx%d max=%08X",
                    GRD_Rgba7MaxLogCount, FrameCounter, Stage,
                    GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                    SrcUSize, SrcVSize, Info.UClamp, Info.VClamp,
                    GET_COLOR_DWORD(*Info.MaxColor) );
            }
        }
        Info.bRealtimeChanged = 0;

        INT ApproxBytes = 0;
        INT NumMips = Info.NumMips - FirstMip;
        if( NumMips < 1 ) NumMips = 1;
        D3DFORMAT DestFormat = (Info.Format == TEXF_DXT1) ? D3DFMT_DXT1 : D3DFMT_A8R8G8B8;
        UBOOL bNeedCreate =
            !Entry->pTexture ||
            Entry->USize    != USize ||
            Entry->VSize    != VSize ||
            Entry->NumMips  != NumMips ||
            Entry->FirstMip != FirstMip ||
            Entry->UIndex   != (bSwapUV ? 1 : 0) ||
            Entry->VIndex   != (bSwapUV ? 0 : 1) ||
            Entry->Format   != DestFormat;
        INT UploadSeq = ++GRD_FrameTexUploadSeq;
        UBOOL bHotUpload = ((RenderHotFrame( FrameCounter ) && UploadSeq <= 180) || RenderTextureHotTrace( TexPoolNext, GRD_TotalTexCreates ));
        if( bHotUpload && RenderHotTrace() )
            GXboxLog.Write( "RTEXUP begin f=%d seq=%d stage=%d id=%08X:%08X fmt=%d size=%dx%d mips=%d first=%d need=%d realtime=%d tex=0x%08X",
                FrameCounter, UploadSeq, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                Info.Format, USize, VSize, NumMips, FirstMip, bNeedCreate, bRealtimeChanged || bForceRgba7MaxUpload, (DWORD)Entry->pTexture );

        // Xbox D3D8 is much less forgiving than D3D7's system-surface upload
        // path: do not LockRect a texture while it is still resident in either
        // texture stage. Rebind after the upload below.
        if( Entry->pTexture && (bRealtimeChanged || bForceRgba7MaxUpload) )
        {
            if( BoundCacheID[0] == Info.CacheID )
            {
                Device->SetTexture( 0, NULL );
                BoundCacheID[0] = 0;
            }
            if( BoundCacheID[1] == Info.CacheID )
            {
                Device->SetTexture( 1, NULL );
                BoundCacheID[1] = 0;
            }
            Entry->pTexture->BlockUntilNotBusy();
        }

        if( Info.Format == TEXF_DXT1 )
        {
            // DXT1 compressed ??? pass through directly.
            HRESULT hrCreate = S_OK;
            for( INT tm = 0; tm < NumMips; tm++ )
                ApproxBytes += Max(1, ((USize >> tm) + 3) / 4) * Max(1, ((VSize >> tm) + 3) / 4) * 8;
            if( bNeedCreate )
            {
                if( Entry->pTexture )
                {
                    RenderBlockAndReleaseTexture( Entry->pTexture );
                }
                if( bHotUpload && GVerboseRenderPerfLog )
                    GXboxLog.Write( "RTEX precreate f=%d seq=%d createNext=%d stage=%d id=%08X:%08X fmt=DXT1 size=%dx%d mips=%d bytes=%d pool=%d liveKB=%d availKB=%u",
                        FrameCounter, UploadSeq, GRD_TotalTexCreates + 1, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                        USize, VSize, NumMips, ApproxBytes, TexPoolNext, TexLiveBytes / 1024, (unsigned)RenderAvailPhysKB() );
                hrCreate = Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_DXT1, D3DPOOL_DEFAULT, &Entry->pTexture );
                Entry->USize    = USize;
                Entry->VSize    = VSize;
                Entry->NumMips  = NumMips;
                Entry->FirstMip = FirstMip;
                Entry->UIndex   = bSwapUV ? 1 : 0;
                Entry->VIndex   = bSwapUV ? 0 : 1;
                Entry->Format   = DestFormat;
                TexLiveBytes   -= Entry->Bytes;
                Entry->Bytes    = ApproxBytes;
                TexLiveBytes   += Entry->Bytes;
                GRD_FrameTexCreates++;
                GRD_TotalTexCreates++;
            }
            GRD_FrameTexUploads++;
            GRD_TotalTexUploads++;
            GRD_TotalTexBytes += ApproxBytes;
            if( FAILED(hrCreate) || !Entry->pTexture || (GVerboseRenderPerfLog && bNeedCreate && (GRD_TotalTexCreates <= 16 || (GRD_TotalTexCreates % 64) == 0)) )
                GXboxLog.Write( "RTEX %s create#=%d upload#=%d frame=%d stage=%d id=%08X:%08X fmt=DXT1 size=%dx%d mips=%d bytes=%d hr=0x%08X tex=0x%08X d3dpool=DEFAULT cache=%d realtime=%d",
                    bNeedCreate ? "create" : "update", GRD_TotalTexCreates, GRD_TotalTexUploads, FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                    USize, VSize, NumMips, ApproxBytes, (DWORD)hrCreate, (DWORD)Entry->pTexture, TexPoolNext, bRealtimeChanged || bForceRgba7MaxUpload );
            if( Entry->pTexture )
            {
                for( INT m = FirstMip; m < Info.NumMips; m++ )
                {
                    if( !Info.Mips[m] || !Info.Mips[m]->DataPtr )
                        continue;
                    D3DLOCKED_RECT lr;
                    if( bHotUpload && RenderHotTrace() )
                        GXboxLog.Write( "RTEXUP dxt-prelock f=%d seq=%d mip=%d tex=0x%08X", FrameCounter, UploadSeq, m - FirstMip, (DWORD)Entry->pTexture );
                    HRESULT hrLock = Entry->pTexture->LockRect( m - FirstMip, &lr, NULL, 0 );
                    if( bHotUpload && RenderHotTrace() )
                        GXboxLog.Write( "RTEXUP dxt-lock f=%d seq=%d mip=%d hr=0x%08X ptr=0x%08X data=0x%08X",
                            FrameCounter, UploadSeq, m - FirstMip, (DWORD)hrLock, (DWORD)(SUCCEEDED(hrLock) ? lr.pBits : NULL), (DWORD)Info.Mips[m]->DataPtr );
                    if( SUCCEEDED(hrLock) )
                    {
                        INT MipW = Max(1, USize >> (m - FirstMip));
                        INT MipH = Max(1, VSize >> (m - FirstMip));
                        INT BlocksW = Max(1, (MipW + 3) / 4);
                        INT BlocksH = Max(1, (MipH + 3) / 4);
                        INT DataSize = BlocksW * BlocksH * 8; // DXT1 = 8 bytes per block
                        appMemcpy( lr.pBits, Info.Mips[m]->DataPtr, DataSize );
                        Entry->pTexture->UnlockRect( m - FirstMip );
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP dxt-done f=%d seq=%d mip=%d bytes=%d",
                                FrameCounter, UploadSeq, m - FirstMip, DataSize );
                    }
                    else
                    {
                        GXboxLog.Write( "RTEX LockRect FAILED frame=%d stage=%d id=%08X:%08X mip=%d hr=0x%08X",
                            FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, m - FirstMip, (DWORD)hrLock );
                    }
                }
            }
        }
        else
        {
            // P8, RGBA7, RGBA8 ??? convert to A8R8G8B8.
            // D3DFMT_A8R8G8B8 (swizzled) ??? matches xQuake/OpenJKDF2 pattern.
            // UT99's renderer assumes normalized 0..1 UVs (UScale = 1/USize).
            // LIN_ formats on Xbox require *texel-space* UVs (0..USize), which
            // would break every UV computation downstream. We stay swizzled and
            // run XGSwizzleRect on the upload below (xQuake gl_fakegl.cpp:2459).
            HRESULT hrCreate = S_OK;
            for( INT tm = 0; tm < NumMips; tm++ )
                ApproxBytes += Max(1, USize >> tm) * Max(1, VSize >> tm) * 4;
            if( bNeedCreate )
            {
                if( Entry->pTexture )
                {
                    RenderBlockAndReleaseTexture( Entry->pTexture );
                }
                if( bHotUpload && GVerboseRenderPerfLog )
                    GXboxLog.Write( "RTEX precreate f=%d seq=%d createNext=%d stage=%d id=%08X:%08X fmt=%d->A8R8G8B8 size=%dx%d mips=%d bytes=%d pool=%d liveKB=%d availKB=%u",
                        FrameCounter, UploadSeq, GRD_TotalTexCreates + 1, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                        Info.Format, USize, VSize, NumMips, ApproxBytes, TexPoolNext, TexLiveBytes / 1024, (unsigned)RenderAvailPhysKB() );
                hrCreate = Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Entry->pTexture );
                Entry->USize    = USize;
                Entry->VSize    = VSize;
                Entry->NumMips  = NumMips;
                Entry->FirstMip = FirstMip;
                Entry->UIndex   = bSwapUV ? 1 : 0;
                Entry->VIndex   = bSwapUV ? 0 : 1;
                Entry->Format   = DestFormat;
                TexLiveBytes   -= Entry->Bytes;
                Entry->Bytes    = ApproxBytes;
                TexLiveBytes   += Entry->Bytes;
                GRD_FrameTexCreates++;
                GRD_TotalTexCreates++;
            }
            GRD_FrameTexUploads++;
            GRD_TotalTexUploads++;
            GRD_TotalTexBytes += ApproxBytes;
            if( FAILED(hrCreate) || !Entry->pTexture || (GVerboseRenderPerfLog && bNeedCreate && (GRD_TotalTexCreates <= 16 || (GRD_TotalTexCreates % 64) == 0)) )
                GXboxLog.Write( "RTEX %s create#=%d upload#=%d frame=%d stage=%d id=%08X:%08X fmt=%d->A8R8G8B8 size=%dx%d mips=%d bytes=%d hr=0x%08X tex=0x%08X d3dpool=DEFAULT cache=%d realtime=%d",
                    bNeedCreate ? "create" : "update", GRD_TotalTexCreates, GRD_TotalTexUploads, FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                    Info.Format, USize, VSize, NumMips, ApproxBytes, (DWORD)hrCreate, (DWORD)Entry->pTexture, TexPoolNext, bRealtimeChanged || bForceRgba7MaxUpload );
            if( Entry->pTexture )
            {
                // Scratch buffer for linear pixel composition before swizzling.
                // Worst case is one mip at 1024??1024??4 = 4 MB. Static-local so
                // we don't re-allocate per upload; single-render-thread Xbox.
                static DWORD* Scratch = NULL;
                static INT    ScratchSize = 0;

                for( INT m = FirstMip; m < Info.NumMips; m++ )
                {
                    if( !Info.Mips[m] || !Info.Mips[m]->DataPtr )
                        continue;

                    INT MipW = Info.Mips[m]->USize;
                    INT MipH = Info.Mips[m]->VSize;
                    INT DestW = bSwapUV ? MipH : MipW;
                    INT DestH = bSwapUV ? MipW : MipH;
                    INT Need = DestW * DestH * 4;
                    if( MipW < 1 || MipH < 1 || DestW < 1 || DestH < 1 || Need <= 0 || Need > (4 * 1024 * 1024) )
                    {
                        GRD_FrameTexSkipped++;
                        GRD_TotalTexSkipped++;
                        GXboxLog.Write( "RTEXUP reject-mip f=%d seq=%d mip=%d fmt=%d mipSize=%dx%d dest=%dx%d need=%d",
                            FrameCounter, UploadSeq, m - FirstMip, Info.Format, MipW, MipH, DestW, DestH, Need );
                        continue;
                    }
                    if( bHotUpload && RenderHotTrace() )
                        GXboxLog.Write( "RTEXUP conv f=%d seq=%d mip=%d fmt=%d mipSize=%dx%d need=%d scratch=%d data=0x%08X",
                            FrameCounter, UploadSeq, m - FirstMip, Info.Format, MipW, MipH, Need, ScratchSize, (DWORD)Info.Mips[m]->DataPtr );
                    if( Need > ScratchSize )
                    {
                        appFree( Scratch );
                        Scratch = (DWORD*)appMalloc( Need, TEXT("XboxRenderSwizzleScratch") );
                        ScratchSize = Need;
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP scratch f=%d seq=%d size=%d ptr=0x%08X",
                                FrameCounter, UploadSeq, ScratchSize, (DWORD)Scratch );
                    }
                    if( !Scratch )
                    {
                        GRD_FrameTexSkipped++;
                        GRD_TotalTexSkipped++;
                        GXboxLog.Write( "RTEXUP scratch-failed f=%d seq=%d mip=%d need=%d",
                            FrameCounter, UploadSeq, m - FirstMip, Need );
                        continue;
                    }
                    DWORD* Dst = Scratch;
                    INT CopyW = RenderMipClampSize( Info.UClamp, m, MipW );
                    INT CopyH = RenderMipClampSize( Info.VClamp, m, MipH );
                    if( GVerboseRenderPerfLog && (CopyW != MipW || CopyH != MipH) && GRD_ClampPadLogCount < 32 )
                    {
                        GRD_ClampPadLogCount++;
                        GXboxLog.Write( "RTEX clamp-pad #%d f=%d seq=%d stage=%d fmt=%d mip=%d tex=%dx%d clamp=%dx%d dest=%dx%d",
                            GRD_ClampPadLogCount, FrameCounter, UploadSeq, Stage, Info.Format, m - FirstMip,
                            MipW, MipH, CopyW, CopyH, DestW, DestH );
                    }

                    if( Info.Format == TEXF_P8 )
                    {
                        BYTE* Src = (BYTE*)Info.Mips[m]->DataPtr;
                        FColor* Pal = Info.Palette;
                        for( INT y = 0; y < DestH; y++ )
                        {
                            INT sy = bSwapUV ? Min( y, CopyW - 1 ) : Min( y, CopyH - 1 );
                            for( INT x = 0; x < DestW; x++ )
                            {
                                INT sx = bSwapUV ? Min( x, CopyH - 1 ) : Min( x, CopyW - 1 );
                                BYTE Idx = Src[sy * MipW + sx];
                                if( Idx == 0 && (PolyFlags & PF_Masked) )
                                    Dst[y * DestW + x] = 0x00000000;
                                else if( Pal )
                                {
                                    FColor& C = Pal[Idx];
                                    Dst[y * DestW + x] = D3DCOLOR_ARGB( C.A, C.R, C.G, C.B );
                                }
                                else
                                    Dst[y * DestW + x] = 0xFFFF00FF;
                            }
                        }
                    }
                    else if( Info.Format == TEXF_RGBA7 )
                    {
                        DWORD* Src = (DWORD*)Info.Mips[m]->DataPtr;
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP rgba7-copy f=%d seq=%d mip=%d tex=%dx%d clamp=%dx%d",
                                FrameCounter, UploadSeq, m - FirstMip, MipW, MipH, CopyW, CopyH );
                        INT SrcStride = MipW;
                        for( INT y = 0; y < DestH; y++ )
                        {
                            INT sy = bSwapUV ? Min( y, CopyW - 1 ) : Min( y, CopyH - 1 );
                            DWORD* SrcRow = Src + sy * SrcStride;
                            for( INT x = 0; x < DestW; x++ )
                            {
                                INT sx = bSwapUV ? Min( x, CopyH - 1 ) : Min( x, CopyW - 1 );
                                Dst[y * DestW + x] = SrcRow[sx] * 2;
                            }
                        }
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP rgba7-done f=%d seq=%d mip=%d",
                                FrameCounter, UploadSeq, m - FirstMip );
                    }
                    else
                    {
                        FColor* Src = (FColor*)Info.Mips[m]->DataPtr;
                        for( INT y = 0; y < DestH; y++ )
                        {
                            INT sy = bSwapUV ? Min( y, CopyW - 1 ) : Min( y, CopyH - 1 );
                            for( INT x = 0; x < DestW; x++ )
                            {
                                INT sx = bSwapUV ? Min( x, CopyH - 1 ) : Min( x, CopyW - 1 );
                                FColor& C = Src[sy * MipW + sx];
                                Dst[y * DestW + x] = D3DCOLOR_ARGB( C.A, C.R, C.G, C.B );
                            }
                        }
                    }

                    // Lock the swizzled destination and swizzle our linear scratch
                    // into it. Pattern from xQuake gl_fakegl.cpp:2457-2468.
                    D3DLOCKED_RECT lr;
                    if( bHotUpload && RenderHotTrace() )
                        GXboxLog.Write( "RTEXUP prelock f=%d seq=%d mip=%d tex=0x%08X", FrameCounter, UploadSeq, m - FirstMip, (DWORD)Entry->pTexture );
                    HRESULT hrLock = Entry->pTexture->LockRect( m - FirstMip, &lr, NULL, 0 );
                    if( bHotUpload && RenderHotTrace() )
                        GXboxLog.Write( "RTEXUP lock f=%d seq=%d mip=%d hr=0x%08X ptr=0x%08X",
                            FrameCounter, UploadSeq, m - FirstMip, (DWORD)hrLock, (DWORD)(SUCCEEDED(hrLock) ? lr.pBits : NULL) );
                    if( SUCCEEDED(hrLock) )
                    {
                        RECT  srcRect = { 0, 0, DestW, DestH };
                        POINT dstPoint = { 0, 0 };
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP swizzle-begin f=%d seq=%d mip=%d src=0x%08X dst=0x%08X size=%dx%d",
                                FrameCounter, UploadSeq, m - FirstMip, (DWORD)Dst, (DWORD)lr.pBits, DestW, DestH );
                        XGSwizzleRect(
                            Dst,            // linear source
                            DestW * 4,      // source pitch
                            &srcRect,
                            lr.pBits,       // swizzled destination
                            DestW,          // dest width (this mip)
                            DestH,          // dest height (this mip)
                            &dstPoint,
                            4               // bytes per pixel
                        );
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP swizzle-end f=%d seq=%d mip=%d",
                                FrameCounter, UploadSeq, m - FirstMip );
                        Entry->pTexture->UnlockRect( m - FirstMip );
                        if( bHotUpload && RenderHotTrace() )
                            GXboxLog.Write( "RTEXUP unlock f=%d seq=%d mip=%d",
                                FrameCounter, UploadSeq, m - FirstMip );
                    }
                    else
                    {
                        GXboxLog.Write( "RTEX LockRect FAILED frame=%d stage=%d id=%08X:%08X mip=%d hr=0x%08X",
                            FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, m - FirstMip, (DWORD)hrLock );
                    }
                }
            }
        }
        ResumeSceneAfterTextureUpload( "tex-upload" );
    }

    // Store stage state.
    BoundCacheID[Stage] = Info.CacheID;
    StageUScale[Stage]  = Entry->UScale;
    StageVScale[Stage]  = Entry->VScale;
    StageUIndex[Stage]  = Entry->UIndex;
    StageVIndex[Stage]  = Entry->VIndex;
    Entry->FrameCounter = FrameCounter;

    // Bind the D3D texture.
    HRESULT hrSet = Device->SetTexture( Stage, Entry->pTexture );
    GRD_FrameTexBinds++;
    if( FAILED(hrSet) || !Entry->pTexture )
        GXboxLog.Write( "RTEX SetTexture frame=%d stage=%d id=%08X:%08X tex=0x%08X hr=0x%08X",
            FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, (DWORD)Entry->pTexture, (DWORD)hrSet );

    unguard;
}

// ============================================================================
// DrawComplexSurface ??? BSP world geometry
// Port of D3D7 lines 660-902.
// ============================================================================
void UXboxRenderDevice::DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet )
{
    guard(UXboxRenderDevice::DrawComplexSurface);

    FlushDGPBatch( "DCS" );
    FlushDTBatch( "DCS" );

    GRD_FrameDCS++;
    GRD_LastOp = "DCS";

    if( !Device || !Frame || !Surface.Texture )
        return;

    if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
        GXboxLog.Write( "DCS begin f=%d dcs=%d tex=%08X:%08X light=0x%08X macro=0x%08X fog=0x%08X flags=0x%08X",
            FrameCounter, GRD_FrameDCS,
            (DWORD)(Surface.Texture->CacheID >> 32), (DWORD)Surface.Texture->CacheID,
            (DWORD)Surface.LightMap, (DWORD)Surface.MacroTexture, (DWORD)Surface.FogMap, Surface.PolyFlags );

    if( GWireframeNoTextureProbe )
    {
        Surface.LightMap      = NULL;
        Surface.MacroTexture  = NULL;
        Surface.DetailTexture = NULL;
        Surface.FogMap        = NULL;
        Surface.PolyFlags &= ~(PF_Memorized | PF_Translucent | PF_Modulated | PF_Invisible | PF_Highlighted | PF_Masked);
        Surface.PolyFlags |= PF_Occlude;
    }

    // Mutually exclusive effects.
    if( Surface.DetailTexture && Surface.FogMap )
        Surface.DetailTexture = NULL;

    // Multitexture path: base texture + lightmap in one pass.
    if( GUseXboxBspMultitexture && Surface.LightMap != NULL && Surface.MacroTexture == NULL )
    {
        SetBlending( Surface.PolyFlags | PF_Memorized );
        SetTextureD3D( 0, *Surface.Texture, Surface.PolyFlags );
        SetTextureD3D( 1, *Surface.LightMap, 0 );

        // Make the lightmap stage deterministic. 2D HUD/flash/overlay draws
        // also use the fixed-function stages, so never rely only on
        // CurrentPolyFlags to decide whether stage 1 is already correct.
        SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
        SetCachedTextureStageState( 1, D3DTSS_TEXCOORDINDEX, 1 );
        SetCachedTextureStageState( 1, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
        SetCachedTextureStageState( 1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );

        SetCachedVertexShader( XBOX_FVF_WORLDVERTEX2 );

        // Draw each polygon in the facet.
        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            if( Poly->NumPts < 3 || Poly->NumPts > XBOX_MAX_VERTS )
            {
                static INT SkipLogCount = 0;
                if( SkipLogCount++ < 8 )
                    GXboxLog.Write( "DCS: skipping oversized multitexture poly NumPts=%d max=%d", Poly->NumPts, XBOX_MAX_VERTS );
                continue;
            }

            FXboxWorldVertex2 Verts[XBOX_MAX_VERTS];
            for( INT i = 0; i < Poly->NumPts; i++ )
            {
                Verts[i].x    = Poly->Pts[i]->Point.X;
                Verts[i].y    = Poly->Pts[i]->Point.Y;
                Verts[i].z    = Poly->Pts[i]->Point.Z;
                Verts[i].color = 0xFFFFFFFF;

                FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);

                SetUV0( Verts[i], u - Surface.Texture->Pan.X, v - Surface.Texture->Pan.Y, StageUScale, StageVScale, StageUIndex, StageVIndex );
                SetUV1( Verts[i], u - Surface.LightMap->Pan.X + 0.5f * Surface.LightMap->UScale, v - Surface.LightMap->Pan.Y + 0.5f * Surface.LightMap->VScale, StageUScale, StageVScale, StageUIndex, StageVIndex );
            }
            RenderDiagPrim( Poly->NumPts, Surface.PolyFlags, "DCS-multi" );
            if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                GXboxLog.Write( "RDRAW begin op=DCS-multi f=%d dcs=%d prim=%d pts=%d stride=%d flags=0x%08X",
                    FrameCounter, GRD_FrameDCS, GRD_FramePrims + 1, Poly->NumPts, (INT)sizeof(FXboxWorldVertex2), Surface.PolyFlags );
            HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, Verts, sizeof(FXboxWorldVertex2), "DCS-multi" );
            if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                GXboxLog.Write( "RDRAW end op=DCS-multi f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
            if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
                GXboxLog.Write( "RDRAW FAILED op=DCS-multi frame=%d pts=%d hr=0x%08X flags=0x%08X", FrameCounter, Poly->NumPts, (DWORD)hrDraw, Surface.PolyFlags );
        }

        // Handle masked depth write.
        if( Surface.PolyFlags & PF_Masked )
            SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_EQUAL );
    }
    else
    {
        DisableStage1();

        // Single-texture fallback: multiple passes. Build a bounded scratch
        // vertex array per polygon; large BSP facets can exceed 512 vertices
        // in total, so never accumulate the whole surface into one buffer.
        SetTextureD3D( 0, *Surface.Texture, Surface.PolyFlags );
        SetBlending( Surface.PolyFlags & ~PF_Memorized );
        SetCachedVertexShader( XBOX_FVF_WORLDVERTEX );

        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            if( Poly->NumPts < 3 || Poly->NumPts > XBOX_MAX_VERTS )
            {
                static INT SkipLogCount = 0;
                if( SkipLogCount++ < 8 )
                    GXboxLog.Write( "DCS: skipping oversized base poly NumPts=%d max=%d", Poly->NumPts, XBOX_MAX_VERTS );
                continue;
            }

            FXboxWorldVertex Verts[XBOX_MAX_VERTS];
            for( INT i = 0; i < Poly->NumPts; i++ )
            {
                FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                Verts[i].x     = Poly->Pts[i]->Point.X;
                Verts[i].y     = Poly->Pts[i]->Point.Y;
                Verts[i].z     = Poly->Pts[i]->Point.Z;
                Verts[i].color = 0xFFFFFFFF;
                SetUV( Verts[i], 0, u - Surface.Texture->Pan.X, v - Surface.Texture->Pan.Y, StageUScale, StageVScale, StageUIndex, StageVIndex );
            }
            RenderDiagPrim( Poly->NumPts, Surface.PolyFlags, "DCS-base" );
            if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                GXboxLog.Write( "RDRAW begin op=DCS-base f=%d dcs=%d prim=%d pts=%d stride=%d flags=0x%08X",
                    FrameCounter, GRD_FrameDCS, GRD_FramePrims + 1, Poly->NumPts, (INT)sizeof(FXboxWorldVertex), Surface.PolyFlags );
            HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, Verts, sizeof(FXboxWorldVertex), "DCS-base" );
            if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                GXboxLog.Write( "RDRAW end op=DCS-base f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
            if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
                GXboxLog.Write( "RDRAW FAILED op=DCS-base frame=%d pts=%d hr=0x%08X flags=0x%08X", FrameCounter, Poly->NumPts, (DWORD)hrDraw, Surface.PolyFlags );
        }

        // Handle masked depth write.
        if( Surface.PolyFlags & PF_Masked )
            SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_EQUAL );

        // Pass 2: Macrotexture (modulated overlay).
        if( Surface.MacroTexture )
        {
            SetBlending( PF_Modulated );
            SetTextureD3D( 0, *Surface.MacroTexture, 0 );
            for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
            {
                if( Poly->NumPts < 3 || Poly->NumPts > XBOX_MAX_VERTS )
                    continue;

                FXboxWorldVertex Verts[XBOX_MAX_VERTS];
                for( INT i = 0; i < Poly->NumPts; i++ )
                {
                    FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                    FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                    Verts[i].x     = Poly->Pts[i]->Point.X;
                    Verts[i].y     = Poly->Pts[i]->Point.Y;
                    Verts[i].z     = Poly->Pts[i]->Point.Z;
                    Verts[i].color = 0xFFFFFFFF;
                    SetUV( Verts[i], 0, u - Surface.MacroTexture->Pan.X, v - Surface.MacroTexture->Pan.Y, StageUScale, StageVScale, StageUIndex, StageVIndex );
                }
                RenderDiagPrim( Poly->NumPts, PF_Modulated, "DCS-macro" );
                if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                    GXboxLog.Write( "RDRAW begin op=DCS-macro f=%d dcs=%d prim=%d pts=%d",
                        FrameCounter, GRD_FrameDCS, GRD_FramePrims + 1, Poly->NumPts );
                HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, Verts, sizeof(FXboxWorldVertex), "DCS-macro" );
                if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                    GXboxLog.Write( "RDRAW end op=DCS-macro f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
                if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
                    GXboxLog.Write( "RDRAW FAILED op=DCS-macro frame=%d pts=%d hr=0x%08X", FrameCounter, Poly->NumPts, (DWORD)hrDraw );
            }
        }

        // Pass 3: Lightmap (modulated overlay).
        if( Surface.LightMap )
        {
            SetBlending( PF_Modulated );
            SetTextureD3D( 0, *Surface.LightMap, 0 );
            for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
            {
                if( Poly->NumPts < 3 || Poly->NumPts > XBOX_MAX_VERTS )
                    continue;

                FXboxWorldVertex Verts[XBOX_MAX_VERTS];
                for( INT i = 0; i < Poly->NumPts; i++ )
                {
                    FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                    FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                    Verts[i].x     = Poly->Pts[i]->Point.X;
                    Verts[i].y     = Poly->Pts[i]->Point.Y;
                    Verts[i].z     = Poly->Pts[i]->Point.Z;
                    Verts[i].color = 0xFFFFFFFF;
                    SetUV( Verts[i], 0, u - Surface.LightMap->Pan.X + 0.5f * Surface.LightMap->UScale, v - Surface.LightMap->Pan.Y + 0.5f * Surface.LightMap->VScale, StageUScale, StageVScale, StageUIndex, StageVIndex );
                }
                RenderDiagPrim( Poly->NumPts, PF_Modulated, "DCS-light" );
                if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                    GXboxLog.Write( "RDRAW begin op=DCS-light f=%d dcs=%d prim=%d pts=%d",
                        FrameCounter, GRD_FrameDCS, GRD_FramePrims + 1, Poly->NumPts );
                HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, Verts, sizeof(FXboxWorldVertex), "DCS-light" );
                if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                    GXboxLog.Write( "RDRAW end op=DCS-light f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
                if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
                    GXboxLog.Write( "RDRAW FAILED op=DCS-light frame=%d pts=%d hr=0x%08X", FrameCounter, Poly->NumPts, (DWORD)hrDraw );
            }
        }
    }

    // Fog map pass.
    if( Surface.FogMap )
    {
        SetBlending( PF_Highlighted );
        SetTextureD3D( 0, *Surface.FogMap, 0 );
        SetCachedVertexShader( XBOX_FVF_WORLDVERTEX );

        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            if( Poly->NumPts < 3 || Poly->NumPts > XBOX_MAX_VERTS )
                continue;

            FXboxWorldVertex FogVerts[XBOX_MAX_VERTS];
            for( INT i = 0; i < Poly->NumPts; i++ )
            {
                FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FogVerts[i].x     = Poly->Pts[i]->Point.X;
                FogVerts[i].y     = Poly->Pts[i]->Point.Y;
                FogVerts[i].z     = Poly->Pts[i]->Point.Z;
                FogVerts[i].color = 0xFFFFFFFF;
                SetUV( FogVerts[i], 0, u - Surface.FogMap->Pan.X + 0.5f * Surface.FogMap->UScale, v - Surface.FogMap->Pan.Y + 0.5f * Surface.FogMap->VScale, StageUScale, StageVScale, StageUIndex, StageVIndex );
            }
            RenderDiagPrim( Poly->NumPts, PF_Highlighted, "DCS-fog" );
            if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                GXboxLog.Write( "RDRAW begin op=DCS-fog f=%d dcs=%d prim=%d pts=%d",
                    FrameCounter, GRD_FrameDCS, GRD_FramePrims + 1, Poly->NumPts );
            HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, FogVerts, sizeof(FXboxWorldVertex), "DCS-fog" );
            if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
                GXboxLog.Write( "RDRAW end op=DCS-fog f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
            if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
                GXboxLog.Write( "RDRAW FAILED op=DCS-fog frame=%d pts=%d hr=0x%08X", FrameCounter, Poly->NumPts, (DWORD)hrDraw );
        }
    }

    // Finish mask handling.
    if( Surface.PolyFlags & PF_Masked )
        SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );

    unguard;
}

// ============================================================================
// DrawGouraudPolygon ??? mesh/actor rendering
// Port of D3D7 lines 904-957.
// ============================================================================
void UXboxRenderDevice::DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Info, FTransTexture** Pts, int NumPts, DWORD PolyFlags, FSpanBuffer* Span )
{
    guard(UXboxRenderDevice::DrawGouraudPolygon);

    FlushDTBatch( "DGP" );

    GRD_FrameDGP++;
    GRD_LastOp = "DGP";

    if( !Device || !Frame || NumPts < 3 || NumPts > XBOX_MAX_VERTS )
        return;

    DisableStage1();

    PolyFlags &= ~PF_Memorized;
    if( GWireframeNoTextureProbe )
        PolyFlags = PF_Occlude;

    if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
        GXboxLog.Write( "DGP begin f=%d dgp=%d pts=%d fmt=%d id=%08X:%08X flags=0x%08X",
            FrameCounter, GRD_FrameDGP, NumPts, Info.Format,
            (DWORD)(Info.CacheID >> 32), (DWORD)Info.CacheID, PolyFlags );

    FXboxWorldVertex Verts[XBOX_MAX_VERTS];
    for( INT i = 0; i < NumPts; i++ )
    {
        Verts[i].x     = Pts[i]->Point.X;
        Verts[i].y     = Pts[i]->Point.Y;
        Verts[i].z     = Pts[i]->Point.Z;
        SetUV( Verts[i], 0, Pts[i]->U, Pts[i]->V, StageUScale, StageVScale, StageUIndex, StageVIndex );

        if( GWireframeNoTextureProbe )
        {
            Verts[i].color = 0xFFFFFFFF;
        }
        else if( PolyFlags & PF_Modulated )
        {
            Verts[i].color = 0xFFFFFFFF;
        }
        else
        {
            Verts[i].color = D3DCOLOR_ARGB(
                255,
                Clamp( appFloor(Pts[i]->Light.X * 255.f), 0, 255 ),
                Clamp( appFloor(Pts[i]->Light.Y * 255.f), 0, 255 ),
                Clamp( appFloor(Pts[i]->Light.Z * 255.f), 0, 255 )
            );
        }
    }

    RenderDiagPrim( NumPts, PolyFlags, "DGP" );
    INT TriVerts = (NumPts - 2) * 3;
    UBOOL bCanBatch = !Info.bRealtimeChanged && TriVerts <= XBOX_DGP_BATCH_VERTS;
    if( bCanBatch && GRD_DGPBatchActive &&
        (GRD_DGPBatchCacheID != Info.CacheID || GRD_DGPBatchPolyFlags != PolyFlags || GRD_DGPBatchVerts + TriVerts > XBOX_DGP_BATCH_VERTS) )
        FlushDGPBatch( "DGP-state" );

    if( bCanBatch && !GRD_DGPBatchActive )
    {
        SetTextureD3D( 0, Info, PolyFlags );
        SetBlending( PolyFlags );
        SetCachedVertexShader( XBOX_FVF_WORLDVERTEX );
        GRD_DGPBatchActive = 1;
        GRD_DGPBatchCacheID = Info.CacheID;
        GRD_DGPBatchPolyFlags = PolyFlags;
    }

    if( bCanBatch && GRD_DGPBatchActive && GRD_DGPBatchCacheID == Info.CacheID && GRD_DGPBatchPolyFlags == PolyFlags )
    {
        for( INT i = 1; i < NumPts - 1; i++ )
        {
            GRD_DGPBatch[GRD_DGPBatchVerts++] = Verts[0];
            GRD_DGPBatch[GRD_DGPBatchVerts++] = Verts[i];
            GRD_DGPBatch[GRD_DGPBatchVerts++] = Verts[i + 1];
        }
        GRD_DGPBatchPolys++;
    }
    else
    {
        FlushDGPBatch( "DGP-fallback" );
        SetTextureD3D( 0, Info, PolyFlags );
        SetBlending( PolyFlags );
        SetCachedVertexShader( XBOX_FVF_WORLDVERTEX );
        if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
            GXboxLog.Write( "RDRAW begin op=DGP f=%d dgp=%d prim=%d pts=%d stride=%d flags=0x%08X",
                FrameCounter, GRD_FrameDGP, GRD_FramePrims + 1, NumPts, (INT)sizeof(FXboxWorldVertex), PolyFlags );
        HRESULT hrDraw = DrawPrimitiveVBWorld( D3DPT_TRIANGLEFAN, NumPts - 2, Verts, sizeof(FXboxWorldVertex), "DGP" );
        if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
            GXboxLog.Write( "RDRAW end op=DGP f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
        if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
            GXboxLog.Write( "RDRAW FAILED op=DGP frame=%d pts=%d fmt=%d hr=0x%08X flags=0x%08X",
                FrameCounter, NumPts, Info.Format, (DWORD)hrDraw, PolyFlags );
    }

    unguard;
}

// ============================================================================
// DrawTile ??? HUD/UI tile rendering
// Port of D3D7 lines 958-982.
// ============================================================================
void UXboxRenderDevice::DrawTile( FSceneNode* Frame, FTextureInfo& Info, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, class FSpanBuffer* Span, FLOAT Z, FPlane Color, FPlane Fog, DWORD PolyFlags )
{
    guard(UXboxRenderDevice::DrawTile);

    FlushDGPBatch( "DT" );

    GRD_FrameDT++;
    GRD_LastOp = "DT";

    if( !Device || !Frame )
        return;

    DisableStage1();

    PolyFlags &= ~PF_Memorized;
    if( GWireframeNoTextureProbe )
        PolyFlags = PF_Occlude;
    if( Info.Palette && Info.Palette[128].A != 255 && !(PolyFlags & PF_Translucent) )
        PolyFlags |= PF_Highlighted;

    if( Z <= 0.001f )
    {
        static INT InvalidZTileLogCount = 0;
        GRD_FrameBadDraws++;
        GRD_FrameBadVerts++;
        GRD_TotalBadDraws++;
        if( InvalidZTileLogCount++ < 16 )
            GXboxLog.Write( "DT skip invalid-z f=%d z=%.6f xy=%.1f,%.1f size=%.1f,%.1f fmt=%d flags=0x%08X",
                FrameCounter, Z, X, Y, XL, YL, Info.Format, PolyFlags );
        return;
    }

    if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
        GXboxLog.Write( "DT begin f=%d dt=%d xy=%.1f,%.1f size=%.1f,%.1f z=%.4f fmt=%d id=%08X:%08X flags=0x%08X",
            FrameCounter, GRD_FrameDT, X, Y, XL, YL, Z, Info.Format,
            (DWORD)(Info.CacheID >> 32), (DWORD)Info.CacheID, PolyFlags );

    FLOAT RZ  = 1.0f / Z;
    FLOAT SZ  = ProjZRatio + ProjZOffset * RZ;
    X        += Frame->XB - 0.5f;
    Y        += Frame->YB - 0.5f;

    DWORD Clr;
    if( GWireframeNoTextureProbe )
        Clr = 0xFFFFFFFF;
    else if( PolyFlags & PF_Modulated )
        Clr = 0xFFFFFFFF;
    else
        Clr = FColor(Color).TrueColor() | 0xFF000000;

    UBOOL bCanBatch = !Info.bRealtimeChanged && GRD_DTBatchVerts + 6 <= XBOX_DT_BATCH_VERTS;
    if( bCanBatch && GRD_DTBatchActive &&
        (GRD_DTBatchCacheID != Info.CacheID || GRD_DTBatchPolyFlags != PolyFlags || GRD_DTBatchVerts + 6 > XBOX_DT_BATCH_VERTS) )
        FlushDTBatch( "DT-state" );

    if( bCanBatch && !GRD_DTBatchActive )
    {
        SetBlending( PolyFlags );
        SetTextureD3D( 0, Info, PolyFlags );
        SetCachedVertexShader( XBOX_FVF_TLVERTEX );
        GRD_DTBatchActive = 1;
        GRD_DTBatchCacheID = Info.CacheID;
        GRD_DTBatchPolyFlags = PolyFlags;
    }
    else if( !bCanBatch )
    {
        FlushDTBatch( "DT-fallback" );
        SetBlending( PolyFlags );
        SetTextureD3D( 0, Info, PolyFlags );
        SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    }

    FXboxTLVertex Verts[4];
    Verts[0].x = X;      Verts[0].y = Y;      Verts[0].rhw = RZ; Verts[0].z = SZ; Verts[0].color = Clr; SetUV( Verts[0], 0, U,      V,      StageUScale, StageVScale, StageUIndex, StageVIndex );
    Verts[1].x = X;      Verts[1].y = Y + YL; Verts[1].rhw = RZ; Verts[1].z = SZ; Verts[1].color = Clr; SetUV( Verts[1], 0, U,      V + VL, StageUScale, StageVScale, StageUIndex, StageVIndex );
    Verts[2].x = X + XL; Verts[2].y = Y + YL; Verts[2].rhw = RZ; Verts[2].z = SZ; Verts[2].color = Clr; SetUV( Verts[2], 0, U + UL, V + VL, StageUScale, StageVScale, StageUIndex, StageVIndex );
    Verts[3].x = X + XL; Verts[3].y = Y;      Verts[3].rhw = RZ; Verts[3].z = SZ; Verts[3].color = Clr; SetUV( Verts[3], 0, U + UL, V,      StageUScale, StageVScale, StageUIndex, StageVIndex );

    RenderDiagPrim( 4, PolyFlags, "DT" );
    if( GRD_MenuTextMode )
    {
        GRD_MenuTextTiles++;
        if( GRD_MenuTextLogBudget > 0 && GRD_MenuTextTiles <= 2 )
        {
            GRD_MenuTextLogBudget--;
            GXboxLog.Write( "MTEXT tile id=%d glyph=%d text='%s' f=%d xy=%.1f,%.1f size=%.1f,%.1f uv=%.1f,%.1f %.1f,%.1f scaled=%.6f,%.6f flags=0x%08X fmt=%d cache=%08X:%08X batch=%d up=%d",
                GRD_MenuTextSerial, GRD_MenuTextTiles, GRD_MenuTextLabel, FrameCounter,
                X, Y, XL, YL, U, V, UL, VL, Verts[0].u, Verts[0].v, PolyFlags, Info.Format,
                (DWORD)(Info.CacheID >> 32), (DWORD)Info.CacheID, bCanBatch, GUseDrawPrimitiveUP );
        }
    }
    if( bCanBatch && GRD_DTBatchActive && GRD_DTBatchCacheID == Info.CacheID && GRD_DTBatchPolyFlags == PolyFlags )
    {
        GRD_DTBatch[GRD_DTBatchVerts++] = Verts[0];
        GRD_DTBatch[GRD_DTBatchVerts++] = Verts[1];
        GRD_DTBatch[GRD_DTBatchVerts++] = Verts[2];
        GRD_DTBatch[GRD_DTBatchVerts++] = Verts[0];
        GRD_DTBatch[GRD_DTBatchVerts++] = Verts[2];
        GRD_DTBatch[GRD_DTBatchVerts++] = Verts[3];
        GRD_DTBatchTiles++;
    }
    else
    {
        if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
            GXboxLog.Write( "RDRAW begin op=DT f=%d dt=%d prim=%d stride=%d flags=0x%08X",
                FrameCounter, GRD_FrameDT, GRD_FramePrims + 1, (INT)sizeof(FXboxTLVertex), PolyFlags );
        HRESULT hrDraw = DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "DT" );
        if( RenderHotFrame( FrameCounter ) && RenderHotTrace() )
            GXboxLog.Write( "RDRAW end op=DT f=%d hr=0x%08X", FrameCounter, (DWORD)hrDraw );
        if( FAILED(hrDraw) && RenderShouldLogDrawFailure( FrameCounter ) )
            GXboxLog.Write( "RDRAW FAILED op=DT frame=%d hr=0x%08X fmt=%d flags=0x%08X", FrameCounter, (DWORD)hrDraw, Info.Format, PolyFlags );
    }

    unguard;
}

// ============================================================================
// RestoreDefaultTextureStages
// ============================================================================
void UXboxRenderDevice::RestoreDefaultTextureStages()
{
    guard(UXboxRenderDevice::RestoreDefaultTextureStages);

    if( !Device )
        return;

    SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
    SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    SetCachedTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
    SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
    SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    SetCachedTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
    SetCachedTextureStageState( 0, D3DTSS_TEXCOORDINDEX, 0 );
    SetCachedTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
    SetCachedTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
    SetCachedTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
    SetCachedTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    SetCachedTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
    SetCachedTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR );

    SetCachedTextureStageState( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    SetCachedTextureStageState( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
    SetCachedTextureStageState( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    SetCachedTextureStageState( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
    SetCachedTextureStageState( 1, D3DTSS_TEXCOORDINDEX, 1 );
    SetCachedTextureStageState( 1, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP );
    SetCachedTextureStageState( 1, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP );
    SetCachedTextureStageState( 1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
    SetCachedTextureStageState( 1, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    SetCachedTextureStageState( 1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
    SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
    SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
    Device->SetTexture( 1, NULL );
    BoundCacheID[1] = 0;

    CurrentPolyFlags = 0xFFFFFFFF;

    unguard;
}

// ============================================================================
// Draw2DLine
// ============================================================================
void UXboxRenderDevice::Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 )
{
    guard(UXboxRenderDevice::Draw2DLine);

    FlushDGPBatch( "D2D-line" );
    FlushDTBatch( "D2D-line" );
    DisableStage1();

    if( !Device )
        return;

    SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );
    SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

    DWORD Clr = FColor(Color).TrueColor() | 0xFF000000;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = ProjZRatio + ProjZOffset * RHW;

    FXboxTLVertex Verts[2];
    Verts[0].x = P1.X - 0.5f; Verts[0].y = P1.Y - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = P2.X - 0.5f; Verts[1].y = P2.Y - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;

    SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    DrawPrimitiveVB( D3DPT_LINELIST, 1, Verts, sizeof(FXboxTLVertex), "D2D-line" );

    RestoreDefaultTextureStages();
    SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );

    unguard;
}

// ============================================================================
// Draw2DPoint
// ============================================================================
void UXboxRenderDevice::Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z )
{
    guard(UXboxRenderDevice::Draw2DPoint);

    FlushDGPBatch( "D2D-point" );
    FlushDTBatch( "D2D-point" );
    DisableStage1();

    if( !Device )
        return;

    SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );
    SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

    DWORD Clr = FColor(Color).TrueColor() | 0xFF000000;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = ProjZRatio + ProjZOffset * RHW;

    FXboxTLVertex Verts[4];
    Verts[0].x = X1 - 0.5f; Verts[0].y = Y1 - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = X2 - 0.5f; Verts[1].y = Y1 - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;
    Verts[2].x = X2 - 0.5f; Verts[2].y = Y2 - 0.5f; Verts[2].rhw = RHW; Verts[2].z = SZ; Verts[2].color = Clr; Verts[2].u = 0; Verts[2].v = 0;
    Verts[3].x = X1 - 0.5f; Verts[3].y = Y2 - 0.5f; Verts[3].rhw = RHW; Verts[3].z = SZ; Verts[3].color = Clr; Verts[3].u = 0; Verts[3].v = 0;

    SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "D2D-point" );

    RestoreDefaultTextureStages();
    SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );

    unguard;
}

extern "C" void XboxRenderPrepareMenuText( FSceneNode* Frame, const char* Label )
{
    guard(XboxRenderPrepareMenuText);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame )
        return;

    Ren->FlushDGPBatch( "menu-text-pre" );
    Ren->FlushDTBatch( "menu-text-pre" );
    GRD_MenuTextMode = 1;
    GRD_MenuTextTiles = 0;
    GRD_MenuTextSerial++;
    if( Label )
    {
        appStrncpy( GRD_MenuTextLabel, Label, ARRAY_COUNT(GRD_MenuTextLabel) );
        GRD_MenuTextLabel[ARRAY_COUNT(GRD_MenuTextLabel)-1] = 0;
    }
    else
    {
        GRD_MenuTextLabel[0] = 0;
    }
    if( GRD_MenuTextLogBudget > 0 )
    {
        GRD_MenuTextLogBudget--;
        GXboxLog.Write( "MTEXT begin id=%d text='%s' frame=%d", GRD_MenuTextSerial, GRD_MenuTextLabel, Ren->FrameCounter );
    }
    Ren->RestoreDefaultTextureStages();
    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;

    unguard;
}

extern "C" void XboxRenderFinishMenuText( FSceneNode* Frame )
{
    guard(XboxRenderFinishMenuText);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame )
        return;

    Ren->FlushDTBatch( "menu-text-post" );
    Ren->FlushDGPBatch( "menu-text-post" );
    if( GRD_MenuTextLogBudget > 0 )
    {
        GRD_MenuTextLogBudget--;
        GXboxLog.Write( "MTEXT end id=%d text='%s' tiles=%d frame=%d", GRD_MenuTextSerial, GRD_MenuTextLabel, GRD_MenuTextTiles, Ren->FrameCounter );
    }
    GRD_MenuTextMode = 0;
    Ren->RestoreDefaultTextureStages();
    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;

    unguard;
}

extern "C" void XboxRenderDrawMenuRect( FSceneNode* Frame, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, BYTE A )
{
    guard(XboxRenderDrawMenuRect);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame )
        return;

    Ren->FlushDGPBatch( "menu-rect" );
    Ren->FlushDTBatch( "menu-rect" );
    Ren->DisableStage1();

    DWORD Clr = ((DWORD)A << 24) | ((DWORD)R << 16) | ((DWORD)G << 8) | (DWORD)B;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;

    FXboxTLVertex Verts[4];
    Verts[0].x = X1 - 0.5f; Verts[0].y = Y1 - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = X2 - 0.5f; Verts[1].y = Y1 - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;
    Verts[2].x = X2 - 0.5f; Verts[2].y = Y2 - 0.5f; Verts[2].rhw = RHW; Verts[2].z = SZ; Verts[2].color = Clr; Verts[2].u = 0; Verts[2].v = 0;
    Verts[3].x = X1 - 0.5f; Verts[3].y = Y2 - 0.5f; Verts[3].rhw = RHW; Verts[3].z = SZ; Verts[3].color = Clr; Verts[3].u = 0; Verts[3].v = 0;

    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
    Ren->Device->SetTexture( 0, NULL );
    Ren->BoundCacheID[0] = 0;
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );

    if( A < 255 )
    {
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        Ren->SetCachedRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        Ren->SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
    }
    else
    {
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    }

    Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    Ren->DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "menu-rect" );

    Ren->RestoreDefaultTextureStages();
    if( A < 255 )
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;

    unguard;
}

extern "C" void XboxRenderDrawMenuRingSlice( FSceneNode* Frame, FLOAT CX, FLOAT CY, FLOAT InnerR, FLOAT OuterR, FLOAT StartAngle, FLOAT EndAngle, BYTE R, BYTE G, BYTE B, BYTE A )
{
    guard(XboxRenderDrawMenuRingSlice);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame || OuterR <= InnerR || EndAngle <= StartAngle )
        return;

    Ren->FlushDGPBatch( "menu-ring-slice" );
    Ren->FlushDTBatch( "menu-ring-slice" );
    Ren->DisableStage1();

    const INT Segments = 8;
    FXboxTLVertex Verts[Segments * 6];
    INT VertCount = 0;

    DWORD Clr = ((DWORD)A << 24) | ((DWORD)R << 16) | ((DWORD)G << 8) | (DWORD)B;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;

    for( INT i=0; i<Segments; i++ )
    {
        FLOAT T0 = (FLOAT)i / (FLOAT)Segments;
        FLOAT T1 = (FLOAT)(i + 1) / (FLOAT)Segments;
        FLOAT A0 = StartAngle + (EndAngle - StartAngle) * T0;
        FLOAT A1 = StartAngle + (EndAngle - StartAngle) * T1;

        FLOAT OX0 = CX - appSin(A0) * OuterR;
        FLOAT OY0 = CY - appCos(A0) * OuterR;
        FLOAT OX1 = CX - appSin(A1) * OuterR;
        FLOAT OY1 = CY - appCos(A1) * OuterR;
        FLOAT IX0 = CX - appSin(A0) * InnerR;
        FLOAT IY0 = CY - appCos(A0) * InnerR;
        FLOAT IX1 = CX - appSin(A1) * InnerR;
        FLOAT IY1 = CY - appCos(A1) * InnerR;

        FXboxTLVertex Tri[6] =
        {
            { OX0 - 0.5f, OY0 - 0.5f, SZ, RHW, Clr, 0.0f, 0.0f },
            { OX1 - 0.5f, OY1 - 0.5f, SZ, RHW, Clr, 0.0f, 0.0f },
            { IX1 - 0.5f, IY1 - 0.5f, SZ, RHW, Clr, 0.0f, 0.0f },
            { OX0 - 0.5f, OY0 - 0.5f, SZ, RHW, Clr, 0.0f, 0.0f },
            { IX1 - 0.5f, IY1 - 0.5f, SZ, RHW, Clr, 0.0f, 0.0f },
            { IX0 - 0.5f, IY0 - 0.5f, SZ, RHW, Clr, 0.0f, 0.0f },
        };

        for( INT j=0; j<6; j++ )
            Verts[VertCount++] = Tri[j];
    }

    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
    Ren->Device->SetTexture( 0, NULL );
    Ren->BoundCacheID[0] = 0;
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );

    if( A < 255 )
    {
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        Ren->SetCachedRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        Ren->SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
    }
    else
    {
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    }

    Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    Ren->DrawPrimitiveVB( D3DPT_TRIANGLELIST, VertCount / 3, Verts, sizeof(FXboxTLVertex), "menu-ring-slice" );

    Ren->RestoreDefaultTextureStages();
    if( A < 255 )
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;

    unguard;
}

static UBOOL GRD_MenuMeshSlotActive = 0;
static D3DVIEWPORT8 GRD_MenuMeshSlotOldViewport;
static DWORD GRD_MenuMeshSlotOldScissorCount = 0;
static BOOL GRD_MenuMeshSlotOldScissorExclusive = FALSE;
static D3DRECT GRD_MenuMeshSlotOldScissors[8];

extern "C" void XboxRenderBeginMenuMeshSlot( FSceneNode* Frame, FLOAT X, FLOAT Y, FLOAT W, FLOAT H )
{
    guard(XboxRenderBeginMenuMeshSlot);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame || W < 1.0f || H < 1.0f )
        return;

    Ren->FlushDGPBatch( "menu-mesh-slot-begin" );
    Ren->FlushDTBatch( "menu-mesh-slot-begin" );

    Ren->Device->GetViewport( &GRD_MenuMeshSlotOldViewport );
    GRD_MenuMeshSlotOldScissorCount = ARRAY_COUNT(GRD_MenuMeshSlotOldScissors);
    GRD_MenuMeshSlotOldScissorExclusive = FALSE;
    Ren->Device->GetScissors( &GRD_MenuMeshSlotOldScissorCount, &GRD_MenuMeshSlotOldScissorExclusive, GRD_MenuMeshSlotOldScissors );

    INT X1 = Max<INT>( 0, (INT)appFloor(X) );
    INT Y1 = Max<INT>( 0, (INT)appFloor(Y) );
    INT X2 = Min<INT>( Frame->XB + Frame->X, (INT)appCeil(X + W) );
    INT Y2 = Min<INT>( Frame->YB + Frame->Y, (INT)appCeil(Y + H) );
    if( X2 <= X1 || Y2 <= Y1 )
        return;

    D3DRECT Rect;
    Rect.x1 = X1;
    Rect.y1 = Y1;
    Rect.x2 = X2;
    Rect.y2 = Y2;
    Ren->Device->SetScissors( 1, FALSE, &Rect );
    Ren->Device->Clear( 1, &Rect, D3DCLEAR_ZBUFFER, 0, 1.0f, 0 );
    GRD_MenuMeshSlotActive = 1;

    unguard;
}

extern "C" void XboxRenderEndMenuMeshSlot( FSceneNode* Frame )
{
    guard(XboxRenderEndMenuMeshSlot);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !GRD_MenuMeshSlotActive )
        return;

    Ren->FlushDGPBatch( "menu-mesh-slot-end" );
    Ren->FlushDTBatch( "menu-mesh-slot-end" );
    Ren->Device->SetScissors( GRD_MenuMeshSlotOldScissorCount, GRD_MenuMeshSlotOldScissorExclusive, GRD_MenuMeshSlotOldScissors );
    Ren->Device->SetViewport( &GRD_MenuMeshSlotOldViewport );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;
    GRD_MenuMeshSlotActive = 0;

    unguard;
}

struct FXboxMenuTexture
{
    char Name[64];
    IDirect3DTexture8* Texture;
    DWORD Width;
    DWORD Height;
};

static FXboxMenuTexture GXboxMenuTextures[32];

static FXboxMenuTexture* XboxFindMenuTexture( const char* Name )
{
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
        if( GXboxMenuTextures[i].Texture && appStricmp(GXboxMenuTextures[i].Name, Name)==0 )
            return &GXboxMenuTextures[i];
    return NULL;
}

static FXboxMenuTexture* XboxLoadMenuTexture( UXboxRenderDevice* Ren, const char* Name )
{
    if( !Ren || !Ren->Device || !Name )
        return NULL;

    FXboxMenuTexture* Existing = XboxFindMenuTexture( Name );
    if( Existing )
        return Existing;

    FXboxMenuTexture* Slot = NULL;
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
    {
        if( !GXboxMenuTextures[i].Texture )
        {
            Slot = &GXboxMenuTextures[i];
            break;
        }
    }
    if( !Slot )
    {
        GXboxLog.Write( "XMENU tex cache full loading %s slots=%d", Name, ARRAY_COUNT(GXboxMenuTextures) );
        return NULL;
    }

    char Path[256];
    appSprintf( Path, "D:\\MenuAssets\\%s", Name );
    HANDLE File = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
    {
        GXboxLog.Write( "XMENU tex missing %s err=%lu", Path, GetLastError() );
        return NULL;
    }

    DWORD Header[3] = {0,0,0};
    DWORD Read = 0;
    if( !ReadFile( File, Header, sizeof(Header), &Read, NULL ) || Read != sizeof(Header) || Header[0] != 0x30495558 )
    {
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex bad header %s", Path );
        return NULL;
    }

    DWORD Width = Header[1];
    DWORD Height = Header[2];
    if( Width < 1 || Height < 1 || Width > 1024 || Height > 1024 )
    {
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex bad size %s %lux%lu", Path, Width, Height );
        return NULL;
    }

    IDirect3DTexture8* Texture = NULL;
    HRESULT hr = Ren->Device->CreateTexture( Width, Height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture );
    if( FAILED(hr) || !Texture )
    {
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex create failed %s %lux%lu hr=0x%08X", Path, Width, Height, (DWORD)hr );
        return NULL;
    }

    D3DLOCKED_RECT Locked;
    hr = Texture->LockRect( 0, &Locked, NULL, 0 );
    if( FAILED(hr) )
    {
        Texture->Release();
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex lock failed %s hr=0x%08X", Path, (DWORD)hr );
        return NULL;
    }

    DWORD RowBytes = Width * 4;
    DWORD ImageBytes = RowBytes * Height;
    BYTE* Linear = (BYTE*)appMalloc( ImageBytes, TEXT("MenuTextureLinear") );
    UBOOL Ok = Linear != NULL;
    if( Ok && (!ReadFile( File, Linear, ImageBytes, &Read, NULL ) || Read != ImageBytes) )
        Ok = 0;
    if( Ok )
    {
        RECT SrcRect = { 0, 0, (LONG)Width, (LONG)Height };
        POINT DstPoint = { 0, 0 };
        XGSwizzleRect(
            Linear,
            RowBytes,
            &SrcRect,
            Locked.pBits,
            Width,
            Height,
            &DstPoint,
            4
        );
    }
    if( Linear )
        appFree( Linear );
    Texture->UnlockRect( 0 );
    CloseHandle( File );

    if( !Ok )
    {
        Texture->Release();
        GXboxLog.Write( "XMENU tex short read %s", Path );
        return NULL;
    }

    appStrncpy( Slot->Name, Name, ARRAY_COUNT(Slot->Name) );
    Slot->Name[ARRAY_COUNT(Slot->Name)-1] = 0;
    Slot->Texture = Texture;
    Slot->Width = Width;
    Slot->Height = Height;
    GXboxLog.Write( "XMENU tex loaded %s %lux%lu", Path, Width, Height );
    return Slot;
}

extern "C" UBOOL XboxRenderDrawMenuTexture( FSceneNode* Frame, const char* Name, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha )
{
    guard(XboxRenderDrawMenuTexture);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame || !Name )
        return 0;

    FXboxMenuTexture* Tex = XboxLoadMenuTexture( Ren, Name );
    if( !Tex || !Tex->Texture )
        return 0;

    Ren->FlushDGPBatch( "menu-tex" );
    Ren->FlushDTBatch( "menu-tex" );
    Ren->DisableStage1();

    BYTE A = (BYTE)Clamp<INT>( (INT)(Alpha * 255.0f), 0, 255 );
    DWORD Clr = (A << 24) | 0x00FFFFFF;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;

    FXboxTLVertex Verts[4];
    Verts[0].x = X - 0.5f;      Verts[0].y = Y - 0.5f;      Verts[0].z = SZ; Verts[0].rhw = RHW; Verts[0].color = Clr; Verts[0].u = 0.0f; Verts[0].v = 0.0f;
    Verts[1].x = X+XL - 0.5f;   Verts[1].y = Y - 0.5f;      Verts[1].z = SZ; Verts[1].rhw = RHW; Verts[1].color = Clr; Verts[1].u = 1.0f; Verts[1].v = 0.0f;
    Verts[2].x = X+XL - 0.5f;   Verts[2].y = Y+YL - 0.5f;   Verts[2].z = SZ; Verts[2].rhw = RHW; Verts[2].color = Clr; Verts[2].u = 1.0f; Verts[2].v = 1.0f;
    Verts[3].x = X - 0.5f;      Verts[3].y = Y+YL - 0.5f;   Verts[3].z = SZ; Verts[3].rhw = RHW; Verts[3].color = Clr; Verts[3].u = 0.0f; Verts[3].v = 1.0f;

    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );
    Ren->SetCachedRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
    Ren->SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
    Ren->SetCachedTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    Ren->SetCachedTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
    Ren->SetCachedTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_NONE );
    Ren->Device->SetTexture( 0, Tex->Texture );
    Ren->BoundCacheID[0] = 0;
    Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    Ren->DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "menu-tex" );
    Ren->Device->SetTexture( 0, NULL );
    Ren->BoundCacheID[0] = 0;
    Ren->RestoreDefaultTextureStages();
    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;

    unguard;
    return 1;
}

// ============================================================================
// ClearZ
// ============================================================================
void UXboxRenderDevice::ClearZ( FSceneNode* Frame )
{
    guard(UXboxRenderDevice::ClearZ);
    FlushDGPBatch( "ClearZ" );
    FlushDTBatch( "ClearZ" );
    if( Device )
        Device->Clear( 0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0 );
    unguard;
}

// ============================================================================
// Always-on Xbox FPS overlay
// ============================================================================
static void AddPerfLine( FXboxTLVertex* Verts, INT& Count, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, DWORD Color )
{
    if( Count + 2 > 160 )
        return;

    FLOAT RHW = 1.0f;
    FLOAT Z   = 0.0f;
    Verts[Count].x = X1 - 0.5f; Verts[Count].y = Y1 - 0.5f; Verts[Count].z = Z; Verts[Count].rhw = RHW; Verts[Count].color = Color; Verts[Count].u = 0; Verts[Count].v = 0; Count++;
    Verts[Count].x = X2 - 0.5f; Verts[Count].y = Y2 - 0.5f; Verts[Count].z = Z; Verts[Count].rhw = RHW; Verts[Count].color = Color; Verts[Count].u = 0; Verts[Count].v = 0; Count++;
}

static void AddPerfDigit( FXboxTLVertex* Verts, INT& Count, INT Digit, FLOAT X, FLOAT Y, FLOAT S, DWORD Color )
{
    static const BYTE Segments[10] =
    {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    BYTE M = Segments[Clamp(Digit,0,9)];
    FLOAT W = 4.0f * S;
    FLOAT H = 8.0f * S;
    FLOAT M0 = 4.0f * S;
    if( M & 0x01 ) AddPerfLine( Verts, Count, X,     Y,      X + W, Y,      Color );
    if( M & 0x02 ) AddPerfLine( Verts, Count, X + W, Y,      X + W, Y + M0, Color );
    if( M & 0x04 ) AddPerfLine( Verts, Count, X + W, Y + M0, X + W, Y + H,  Color );
    if( M & 0x08 ) AddPerfLine( Verts, Count, X,     Y + H,  X + W, Y + H,  Color );
    if( M & 0x10 ) AddPerfLine( Verts, Count, X,     Y + M0, X,     Y + H,  Color );
    if( M & 0x20 ) AddPerfLine( Verts, Count, X,     Y,      X,     Y + M0, Color );
    if( M & 0x40 ) AddPerfLine( Verts, Count, X,     Y + M0, X + W, Y + M0, Color );
}

static void AddPerfLabelFPS( FXboxTLVertex* Verts, INT& Count, FLOAT X, FLOAT Y, FLOAT S, DWORD Color )
{
    FLOAT W = 4.0f * S;
    FLOAT H = 8.0f * S;
    FLOAT M = 4.0f * S;

    // F
    AddPerfLine( Verts, Count, X, Y, X, Y + H, Color );
    AddPerfLine( Verts, Count, X, Y, X + W, Y, Color );
    AddPerfLine( Verts, Count, X, Y + M, X + W, Y + M, Color );
    X += 7.0f * S;

    // P
    AddPerfLine( Verts, Count, X, Y, X, Y + H, Color );
    AddPerfLine( Verts, Count, X, Y, X + W, Y, Color );
    AddPerfLine( Verts, Count, X + W, Y, X + W, Y + M, Color );
    AddPerfLine( Verts, Count, X, Y + M, X + W, Y + M, Color );
    X += 7.0f * S;

    // S
    AddPerfLine( Verts, Count, X, Y, X + W, Y, Color );
    AddPerfLine( Verts, Count, X, Y, X, Y + M, Color );
    AddPerfLine( Verts, Count, X, Y + M, X + W, Y + M, Color );
    AddPerfLine( Verts, Count, X + W, Y + M, X + W, Y + H, Color );
    AddPerfLine( Verts, Count, X, Y + H, X + W, Y + H, Color );
}

void UXboxRenderDevice::DrawPerfOverlay()
{
    guard(UXboxRenderDevice::DrawPerfOverlay);

    FlushDGPBatch( "PerfOverlay" );
    FlushDTBatch( "PerfOverlay" );
    DisableStage1();

    if( !Device )
        return;

    FXboxTLVertex Verts[160];
    INT Count = 0;
    DWORD Color = D3DCOLOR_ARGB( 255, 0, 255, 64 );
    FLOAT X = 10.0f;
    FLOAT Y = 10.0f;
    FLOAT S = 2.0f;

    INT FPS = Clamp( appRound(GRD_DisplayFPS), 0, 999 );
    AddPerfLabelFPS( Verts, Count, X, Y, S, Color );
    X += 26.0f * S;
    AddPerfDigit( Verts, Count, (FPS / 100) % 10, X, Y, S, Color ); X += 7.0f * S;
    AddPerfDigit( Verts, Count, (FPS / 10) % 10,  X, Y, S, Color ); X += 7.0f * S;
    AddPerfDigit( Verts, Count, FPS % 10,         X, Y, S, Color );

    if( Count >= 2 )
    {
        Device->SetTexture( 0, NULL );
        Device->SetTexture( 1, NULL );
        BoundCacheID[0] = 0;
        BoundCacheID[1] = 0;
        SetCachedVertexShader( XBOX_FVF_TLVERTEX );
        SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
        SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
        SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
        SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
        SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
        SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

        DrawPrimitiveVB( D3DPT_LINELIST, Count / 2, Verts, sizeof(FXboxTLVertex), "PerfOverlay" );

        SetCachedRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
        SetCachedRenderState( D3DRS_ZWRITEENABLE, TRUE );
        RestoreDefaultTextureStages();
    }

    unguard;
}

// ============================================================================
// EndFlash ??? screen flash effect (damage, pickup, etc.)
// Port of D3D7 lines 1278-1307.
// ============================================================================
void UXboxRenderDevice::EndFlash()
{
    guard(UXboxRenderDevice::EndFlash);

    FlushDGPBatch( "EndFlash" );
    FlushDTBatch( "EndFlash" );
    DisableStage1();

    if( !Device || !Viewport )
        return;

    if( FlashScale != FPlane(.5f,.5f,.5f,.5f) || FlashFog != FPlane(0,0,0,0) )
    {
        FColor D3DColor = FColor(FPlane(FlashFog.X, FlashFog.Y, FlashFog.Z, Min(FlashScale.X * 2.f, 1.f)));
        DWORD Clr = D3DCOLOR_ARGB( D3DColor.A, D3DColor.R, D3DColor.G, D3DColor.B );

        FLOAT RHW = 0.5f;
        FLOAT SZ  = ProjZRatio + ProjZOffset * RHW;

        FXboxTLVertex Verts[4];
        Verts[0].x = 0;               Verts[0].y = 0;               Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
        Verts[1].x = 0;               Verts[1].y = (FLOAT)Viewport->SizeY; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;
        Verts[2].x = (FLOAT)Viewport->SizeX; Verts[2].y = (FLOAT)Viewport->SizeY; Verts[2].rhw = RHW; Verts[2].z = SZ; Verts[2].color = Clr; Verts[2].u = 0; Verts[2].v = 0;
        Verts[3].x = (FLOAT)Viewport->SizeX; Verts[3].y = 0;               Verts[3].rhw = RHW; Verts[3].z = SZ; Verts[3].color = Clr; Verts[3].u = 0; Verts[3].v = 0;

        SetBlending( PF_Translucent | PF_NoOcclude );
        SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        SetCachedRenderState( D3DRS_SRCBLEND,  D3DBLEND_ONE );
        SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_SRCALPHA );
        SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2 );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 );
        SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );

        SetCachedVertexShader( XBOX_FVF_TLVERTEX );
        DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "EndFlash" );

        RestoreDefaultTextureStages();
        SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
        SetBlending( 0 );
    }

    unguard;
}

// ============================================================================
// PushHit / PopHit ??? not needed on Xbox (no hit testing)
// ============================================================================
void UXboxRenderDevice::PushHit( const BYTE* Data, INT Count )
{
    guard(UXboxRenderDevice::PushHit);
    unguard;
}

void UXboxRenderDevice::PopHit( INT Count, UBOOL bForce )
{
    guard(UXboxRenderDevice::PopHit);
    unguard;
}

// ============================================================================
// GetStats
// ============================================================================
void UXboxRenderDevice::GetStats( TCHAR* Result )
{
    guard(UXboxRenderDevice::GetStats);
    Result[0] = 0;
    unguard;
}

// ============================================================================
// ReadPixels
// ============================================================================
void UXboxRenderDevice::ReadPixels( FColor* Pixels )
{
    guard(UXboxRenderDevice::ReadPixels);
    // Not implemented ??? screenshots not supported on Xbox.
    unguard;
}
