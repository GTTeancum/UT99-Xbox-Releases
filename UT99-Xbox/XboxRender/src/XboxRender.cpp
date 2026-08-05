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
static INT   GRD_SourceUnloadLogCount = 0;
static INT   GRD_LowMemoryScaleLogCount = 0;
static UBOOL GRD_LowMemoryTextureMode = 0;
// Untextured-surface diagnosis (see Docs/OPEN_ITEMS.md item 1, white flicker).
// These are pure instrumentation: nothing here changes render behaviour.
//   Reuse    = LRU slot recycles in SetTextureD3D (the branch that blanket-
//              unbinds every stage).
//   Clobber  = those recycles that cleared a stage-0 binding the caller had
//              already been told was good.
//   NoBase   = DrawComplexSurface draws that actually went out with stage 0
//              unbound. This is the white-surface predicate itself, so it is
//              true regardless of what colour an unbound stage samples as.
static INT   GRD_TotalTexReuse   = 0;
static INT   GRD_TotalStage0Clobber = 0;
static INT   GRD_TotalNoBaseDraw = 0;
static INT   GRD_NoBaseLogCount  = 0;
static INT   GRD_ClobberLogCount = 0;
static INT   GRD_ClampMismatchCount = 0;
static INT   GRD_ClampMatchCount = 0;
static INT   GRD_ClampMismatchLogCount = 0;
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
static const UBOOL GShowXboxPerfOverlay = 0;
static DOUBLE GRD_FrameStartSeconds = 0.0;
static DOUBLE GRD_LastFrameStartSeconds = 0.0;
static DOUBLE GRD_LastPerfLogSeconds = 0.0;
static DOUBLE GRD_FpsWindowSeconds = 0.0;
static INT    GRD_FpsWindowFrames = 0;
static FLOAT  GRD_DisplayFPS = 0.0f;
static FLOAT  GRD_LastFrameMS = 0.0f;
static FLOAT  GRD_LastRenderMS = 0.0f;
static FLOAT  GRD_LastPresentMS = 0.0f;
static FLOAT  GRD_DisplayBrightness = 0.5f;
static FLOAT  GRD_DisplayContrast = 1.0f;
static FLOAT  GRD_DisplayGamma = 1.0f;
static UBOOL  GRD_DisplayCalibrationDirty = 1;
static UBOOL  GRD_DisplayPostFailed = 0;
static UBOOL  GRD_DisplayPostLogged = 0;
static IDirect3DTexture8* GRD_DisplaySourceTexture = NULL;
static DWORD  GRD_DisplayPixelShader = 0;
static UINT   GRD_DisplaySourceW = 0;
static UINT   GRD_DisplaySourceH = 0;
static IDirect3DTexture8* GRD_LoadingBackgroundTexture = NULL;
static UINT   GRD_LoadingBackgroundW = 0;
static UINT   GRD_LoadingBackgroundH = 0;
static UBOOL  GRD_LoadingBackgroundReady = 0;
static UBOOL  GRD_LoadingBackgroundCaptureRequested = 0;
static INT    GRD_LoadingBackgroundCaptureSerial = 0;
static UBOOL  GRD_HasPendingLockViewport = 0;
static INT    GRD_PendingLockX = 0;
static INT    GRD_PendingLockY = 0;
static INT    GRD_PendingLockW = 640;
static INT    GRD_PendingLockH = 480;
static UBOOL  GRD_PendingLockClearFullTarget = 0;

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
enum { XBOX_MENU_RECT_BATCH_VERTS = XBOX_SAFE_TRI_BATCH_VERTS };
static FXboxTLVertex GRD_MenuRectBatch[XBOX_MENU_RECT_BATCH_VERTS];
static INT   GRD_MenuRectBatchVerts = 0;
static UBOOL GRD_MenuRectBatchActive = 0;
static UBOOL GRD_MenuTextMode = 0;
static INT   GRD_MenuTextSerial = 0;
static INT   GRD_MenuTextTiles = 0;
static INT   GRD_MenuTextLogBudget = 0;
static char  GRD_MenuTextLabel[64] = {0};
static UBOOL GRD_DrawVBStreamBound = 0;
static UINT  GRD_DrawVBStreamStride = 0;
enum { XBOX_UPLOAD_CHUNK_BYTES = 32 * 1024 };
static BYTE GRD_UploadChunk[XBOX_UPLOAD_CHUNK_BYTES];

static void XboxRenderFlushMenuRectBatch( UXboxRenderDevice* Ren, const char* Reason );

// XDK 5558 pixel shader generated from DisplayCalibration.xps. It applies a
// smooth gamma curve followed by exact contrast and brightness adjustment.
static DWORD GRD_DisplayPixelShaderCode[] =
{
    0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x0000000c,
    0x00001880, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0xc8c80000, 0xc820cc40, 0xcdc120cc,
    0xcdc120cc, 0xccc10000, 0xcc20c120, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x000000c0, 0x00000d00, 0x00000c00, 0x00000c00,
    0x000100c0, 0x00000c00, 0x00000000, 0x00000000,
    0x00011106, 0x00000001, 0x00000000, 0x00000000,
    0xff3210ff, 0xffffffff, 0x000001ff
};

extern "C" void XboxRenderSetDisplayCalibration( FLOAT Brightness, FLOAT Contrast, FLOAT Gamma )
{
    Brightness = Clamp<FLOAT>( Brightness, 0.0f, 1.0f );
    Contrast = Clamp<FLOAT>( Contrast, 0.5f, 1.5f );
    Gamma = Clamp<FLOAT>( Gamma, 0.5f, 2.0f );

    if( Abs(GRD_DisplayBrightness - Brightness) > 0.0001f ||
        Abs(GRD_DisplayContrast - Contrast) > 0.0001f ||
        Abs(GRD_DisplayGamma - Gamma) > 0.0001f )
    {
        GRD_DisplayBrightness = Brightness;
        GRD_DisplayContrast = Contrast;
        GRD_DisplayGamma = Gamma;
        GRD_DisplayCalibrationDirty = 1;
        GRD_DisplayPostLogged = 0;
    }
}

static void XboxRenderApplyDisplayCalibration( UXboxRenderDevice* Ren )
{
    if( !Ren || !Ren->Device || !GRD_DisplayCalibrationDirty )
        return;

    D3DGAMMARAMP LinearRamp;
    for( INT i=0; i<256; i++ )
        LinearRamp.red[i] = LinearRamp.green[i] = LinearRamp.blue[i] = (BYTE)i;

    GXboxLog.Write( "RCOLOR applying brightness=%.2f contrast=%.2f gamma=%.2f",
        GRD_DisplayBrightness, GRD_DisplayContrast, GRD_DisplayGamma );
    D3DDevice::SetGammaRamp( D3DSGR_IMMEDIATE, &LinearRamp );
    GRD_DisplayCalibrationDirty = 0;
    GXboxLog.Write( "RCOLOR applied brightness=%.2f contrast=%.2f gamma=%.2f",
        GRD_DisplayBrightness, GRD_DisplayContrast, GRD_DisplayGamma );
}

static void XboxRenderReleaseDisplayCalibrationResources()
{
    if( GRD_DisplayPixelShader )
    {
        D3DDevice::DeletePixelShader( GRD_DisplayPixelShader );
        GRD_DisplayPixelShader = 0;
    }
    if( GRD_DisplaySourceTexture )
    {
        GRD_DisplaySourceTexture->Release();
        GRD_DisplaySourceTexture = NULL;
    }
    GRD_DisplaySourceW = 0;
    GRD_DisplaySourceH = 0;
}

static UBOOL XboxRenderEnsureDisplayCalibrationResources( UXboxRenderDevice* Ren )
{
    if( !Ren || !Ren->Device || GRD_DisplayPostFailed )
        return 0;
    if( GRD_DisplaySourceTexture && GRD_DisplayPixelShader )
        return 1;

    GRD_DisplaySourceW = Ren->ActualBackBufferW;
    GRD_DisplaySourceH = Ren->ActualBackBufferH;
    HRESULT SourceResult = Ren->Device->CreateTexture(
        GRD_DisplaySourceW, GRD_DisplaySourceH, 1, 0,
        D3DFMT_LIN_X8R8G8B8, D3DPOOL_DEFAULT, &GRD_DisplaySourceTexture );
    HRESULT ShaderResult = D3DDevice::CreatePixelShader(
        (D3DPIXELSHADERDEF*)GRD_DisplayPixelShaderCode, &GRD_DisplayPixelShader );

    if( FAILED(SourceResult) || FAILED(ShaderResult) ||
        !GRD_DisplaySourceTexture || !GRD_DisplayPixelShader )
    {
        GXboxLog.Write( "RCOLOR post resource failure source=0x%08X shader=0x%08X",
            (DWORD)SourceResult, (DWORD)ShaderResult );
        XboxRenderReleaseDisplayCalibrationResources();
        GRD_DisplayPostFailed = 1;
        return 0;
    }

    GXboxLog.Write( "RCOLOR post resources ready frame=%ux%u source=%ux%u",
        (unsigned)Ren->ActualBackBufferW, (unsigned)Ren->ActualBackBufferH,
        (unsigned)GRD_DisplaySourceW, (unsigned)GRD_DisplaySourceH );
    return 1;
}

static void XboxRenderApplyDisplayPostProcess( UXboxRenderDevice* Ren )
{
    if( !Ren || !Ren->Device || GRD_DisplayPostFailed )
        return;

    UBOOL Neutral = Abs(GRD_DisplayBrightness - 0.5f) < 0.0001f
        && Abs(GRD_DisplayContrast - 1.0f) < 0.0001f
        && Abs(GRD_DisplayGamma - 1.0f) < 0.0001f;
    if( Neutral || !XboxRenderEnsureDisplayCalibrationResources(Ren) )
        return;

    IDirect3DSurface8* BackSurface = NULL;
    IDirect3DSurface8* SourceSurface = NULL;
    HRESULT BackResult = Ren->Device->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &BackSurface );
    HRESULT SurfaceResult = GRD_DisplaySourceTexture->GetSurfaceLevel( 0, &SourceSurface );
    HRESULT CopyResult = E_FAIL;
    if( SUCCEEDED(BackResult) && BackSurface && SUCCEEDED(SurfaceResult) && SourceSurface )
    {
        RECT SourceRect;
        SourceRect.left = 0;
        SourceRect.top = 0;
        SourceRect.right = (LONG)Ren->ActualBackBufferW;
        SourceRect.bottom = (LONG)Ren->ActualBackBufferH;
        POINT DestPoint;
        DestPoint.x = 0;
        DestPoint.y = 0;
        CopyResult = Ren->Device->CopyRects( BackSurface, &SourceRect, 1, SourceSurface, &DestPoint );
    }
    if( SourceSurface ) SourceSurface->Release();
    if( BackSurface ) BackSurface->Release();
    if( FAILED(CopyResult) )
    {
        GXboxLog.Write( "RCOLOR post copy failure back=0x%08X surface=0x%08X copy=0x%08X",
            (DWORD)BackResult, (DWORD)SurfaceResult, (DWORD)CopyResult );
        GRD_DisplayPostFailed = 1;
        return;
    }

    struct FDisplayVertex
    {
        FLOAT X, Y, Z, RHW;
        FLOAT U, V;
    };
    FLOAT W = (FLOAT)Ren->ActualBackBufferW;
    FLOAT H = (FLOAT)Ren->ActualBackBufferH;
    FLOAT UMax = W;
    FLOAT VMax = H;
    FDisplayVertex Verts[4] =
    {
        { -0.5f,   -0.5f,   1.0f, 1.0f, 0.0f, 0.0f },
        { W-0.5f,  -0.5f,   1.0f, 1.0f, UMax, 0.0f },
        { W-0.5f,  H-0.5f,  1.0f, 1.0f, UMax, VMax },
        { -0.5f,   H-0.5f,  1.0f, 1.0f, 0.0f, VMax }
    };

    Ren->Device->SetPixelShader( GRD_DisplayPixelShader );
    Ren->Device->SetTexture( 0, GRD_DisplaySourceTexture );
    Ren->Device->SetTexture( 1, NULL );
    Ren->Device->SetTexture( 2, NULL );
    D3DVIEWPORT8 FullViewport;
    FullViewport.X = 0;
    FullViewport.Y = 0;
    FullViewport.Width = Ren->ActualBackBufferW;
    FullViewport.Height = Ren->ActualBackBufferH;
    FullViewport.MinZ = 0.0f;
    FullViewport.MaxZ = 1.0f;
    Ren->Device->SetViewport( &FullViewport );
    FLOAT CurveBase = GRD_DisplayGamma <= 1.0f
        ? Clamp<FLOAT>((GRD_DisplayGamma - 0.5f) * 2.0f, 0.0f, 1.0f)
        : 1.0f;
    FLOAT CurveExtra = GRD_DisplayGamma > 1.0f
        ? Clamp<FLOAT>(GRD_DisplayGamma - 1.0f, 0.0f, 1.0f)
        : 0.0f;
    FLOAT ContrastHalf = GRD_DisplayContrast * 0.5f;
    FLOAT Bias = (GRD_DisplayBrightness - 0.5f) + 0.5f * (1.0f - GRD_DisplayContrast);
    FLOAT Constants[4][4] =
    {
        { CurveBase, CurveBase, CurveBase, CurveBase },
        { CurveExtra, CurveExtra, CurveExtra, CurveExtra },
        { ContrastHalf, ContrastHalf, ContrastHalf, ContrastHalf },
        { Bias, Bias, Bias, Bias }
    };
    Ren->Device->SetPixelShaderConstant( 0, Constants, 4 );
    Ren->Device->SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    Ren->Device->SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
    Ren->Device->SetTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
    Ren->Device->SetTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
    for( INT Stage=1; Stage<=2; Stage++ )
    {
        Ren->Device->SetTextureStageState( Stage, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
        Ren->Device->SetTextureStageState( Stage, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
        Ren->Device->SetTextureStageState( Stage, D3DTSS_MIPFILTER, D3DTEXF_NONE );
        Ren->Device->SetTextureStageState( Stage, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
        Ren->Device->SetTextureStageState( Stage, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
    }
    Ren->Device->SetRenderState( D3DRS_ZENABLE, FALSE );
    Ren->Device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->Device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->Device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->Device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
    Ren->Device->SetRenderState( D3DRS_FILLMODE, D3DFILL_SOLID );
    Ren->Device->SetVertexShader( D3DFVF_XYZRHW | D3DFVF_TEX1 );

    HRESULT BeginResult = Ren->Device->BeginScene();
    HRESULT DrawResult = FAILED(BeginResult) ? BeginResult :
        Ren->Device->DrawPrimitiveUP( D3DPT_QUADLIST, 1, Verts, sizeof(FDisplayVertex) );
    HRESULT EndResult = SUCCEEDED(BeginResult) ? Ren->Device->EndScene() : BeginResult;

    Ren->Device->SetPixelShader( NULL );
    Ren->Device->SetTexture( 0, NULL );
    Ren->Device->SetTexture( 1, NULL );
    Ren->Device->SetTexture( 2, NULL );
    Ren->Device->SetRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
    Ren->Device->SetRenderState( D3DRS_ZWRITEENABLE, TRUE );

    appMemzero( Ren->CachedRenderStateValid, sizeof(Ren->CachedRenderStateValid) );
    appMemzero( Ren->CachedTextureStageStateValid, sizeof(Ren->CachedTextureStageStateValid) );
    Ren->CachedVertexShaderValid = 0;
    Ren->BoundCacheID[0] = Ren->BoundCacheID[1] = 0;

    // The post-process clamps stages 0-2 above so the backbuffer copy does not
    // sample past its edge. Those are raw SetTextureStageState calls, and the
    // only code that programs WRAP for world geometry is the one-shot
    // bStateInit block in Lock(), which has long since run. Without this the
    // clamp survives into the next frame and every BSP surface - whose UVs tile
    // far outside 0..1 - smears its edge texel across the whole poly, which
    // reads as extremely stretched textures for the rest of the session.
    // Only reachable when the player moves brightness/contrast/gamma off
    // neutral, which is why it does not reproduce on default settings.
    Ren->RestoreDefaultTextureStages();

    if( FAILED(BeginResult) || FAILED(DrawResult) || FAILED(EndResult) )
    {
        GXboxLog.Write( "RCOLOR post draw failure begin=0x%08X draw=0x%08X end=0x%08X",
            (DWORD)BeginResult, (DWORD)DrawResult, (DWORD)EndResult );
        GRD_DisplayPostFailed = 1;
    }
    else if( !GRD_DisplayPostLogged )
    {
        GXboxLog.Write( "RCOLOR post active brightness=%.2f contrast=%.2f gamma=%.2f",
            GRD_DisplayBrightness, GRD_DisplayContrast, GRD_DisplayGamma );
        GRD_DisplayPostLogged = 1;
    }
}

static HRESULT XboxRenderApplyViewport( IDirect3DDevice8* InDevice, INT X, INT Y, INT W, INT H, UINT BackBufferW, UINT BackBufferH )
{
    if( !InDevice )
        return E_FAIL;

    if( BackBufferW < 1 )
        BackBufferW = 1;
    if( BackBufferH < 1 )
        BackBufferH = 1;

    INT vpX = X;
    INT vpY = Y;
    INT vpW = W;
    INT vpH = H;
    if( vpX < 0 ) vpX = 0;
    if( vpY < 0 ) vpY = 0;
    if( vpX >= (INT)BackBufferW ) vpX = (INT)BackBufferW - 1;
    if( vpY >= (INT)BackBufferH ) vpY = (INT)BackBufferH - 1;
    if( vpW < 1 ) vpW = 1;
    if( vpH < 1 ) vpH = 1;
    if( (UINT)(vpX + vpW) > BackBufferW ) vpW = (INT)BackBufferW - vpX;
    if( (UINT)(vpY + vpH) > BackBufferH ) vpH = (INT)BackBufferH - vpY;
    if( vpW < 1 ) vpW = 1;
    if( vpH < 1 ) vpH = 1;

    D3DVIEWPORT8 vp;
    vp.X      = (DWORD)vpX;
    vp.Y      = (DWORD)vpY;
    vp.Width  = (DWORD)vpW;
    vp.Height = (DWORD)vpH;
    vp.MinZ   = 0.0f;
    vp.MaxZ   = 1.0f;
    return InDevice->SetViewport( &vp );
}

static void XboxRenderReleaseLoadingBackgroundResources()
{
    if( GRD_LoadingBackgroundTexture )
    {
        GRD_LoadingBackgroundTexture->BlockUntilNotBusy();
        GRD_LoadingBackgroundTexture->Release();
        GRD_LoadingBackgroundTexture = NULL;
    }
    GRD_LoadingBackgroundW = 0;
    GRD_LoadingBackgroundH = 0;
    GRD_LoadingBackgroundReady = 0;
}

static UBOOL XboxRenderEnsureLoadingBackgroundTexture( UXboxRenderDevice* Ren )
{
    if( !Ren || !Ren->Device )
        return 0;

    UINT W = Ren->ActualBackBufferW ? Ren->ActualBackBufferW : 640;
    UINT H = Ren->ActualBackBufferH ? Ren->ActualBackBufferH : 480;
    if( GRD_LoadingBackgroundTexture && GRD_LoadingBackgroundW == W && GRD_LoadingBackgroundH == H )
        return 1;

    XboxRenderReleaseLoadingBackgroundResources();
    HRESULT hr = Ren->Device->CreateTexture( W, H, 1, 0, D3DFMT_LIN_X8R8G8B8, D3DPOOL_DEFAULT, &GRD_LoadingBackgroundTexture );
    if( FAILED(hr) || !GRD_LoadingBackgroundTexture )
    {
        GXboxLog.Write( "RLOADBG texture create failed size=%ux%u hr=0x%08X", (unsigned)W, (unsigned)H, (DWORD)hr );
        XboxRenderReleaseLoadingBackgroundResources();
        return 0;
    }

    GRD_LoadingBackgroundW = W;
    GRD_LoadingBackgroundH = H;
    GRD_LoadingBackgroundReady = 0;
    GXboxLog.Write( "RLOADBG texture ready size=%ux%u", (unsigned)W, (unsigned)H );
    return 1;
}

static void XboxRenderCaptureLoadingBackgroundNow( UXboxRenderDevice* Ren )
{
    GRD_LoadingBackgroundCaptureRequested = 0;
    GRD_LoadingBackgroundReady = 0;
    if( !XboxRenderEnsureLoadingBackgroundTexture( Ren ) )
        return;

    IDirect3DSurface8* BackSurface = NULL;
    IDirect3DSurface8* DestSurface = NULL;
    HRESULT BackResult = Ren->Device->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &BackSurface );
    HRESULT SurfaceResult = GRD_LoadingBackgroundTexture->GetSurfaceLevel( 0, &DestSurface );
    HRESULT CopyResult = E_FAIL;
    if( SUCCEEDED(BackResult) && BackSurface && SUCCEEDED(SurfaceResult) && DestSurface )
    {
        RECT SourceRect;
        SourceRect.left = 0;
        SourceRect.top = 0;
        SourceRect.right = (LONG)GRD_LoadingBackgroundW;
        SourceRect.bottom = (LONG)GRD_LoadingBackgroundH;
        POINT DestPoint;
        DestPoint.x = 0;
        DestPoint.y = 0;
        CopyResult = Ren->Device->CopyRects( BackSurface, &SourceRect, 1, DestSurface, &DestPoint );
    }
    if( DestSurface ) DestSurface->Release();
    if( BackSurface ) BackSurface->Release();

    if( SUCCEEDED(CopyResult) )
    {
        GRD_LoadingBackgroundReady = 1;
        GRD_LoadingBackgroundCaptureSerial++;
        GXboxLog.Write( "RLOADBG capture ok serial=%d size=%ux%u", GRD_LoadingBackgroundCaptureSerial,
            (unsigned)GRD_LoadingBackgroundW, (unsigned)GRD_LoadingBackgroundH );
    }
    else
    {
        GXboxLog.Write( "RLOADBG capture failed back=0x%08X surface=0x%08X copy=0x%08X",
            (DWORD)BackResult, (DWORD)SurfaceResult, (DWORD)CopyResult );
    }
}

extern "C" void XboxRenderRequestLoadingFrameBackground( URenderDevice* RenderDevice )
{
    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( RenderDevice );
    if( !Ren || !Ren->Device )
        return;
    GRD_LoadingBackgroundReady = 0;
    GRD_LoadingBackgroundCaptureRequested = 1;
}

extern "C" void XboxRenderReleaseLoadingFrameBackground( URenderDevice* RenderDevice )
{
    GRD_LoadingBackgroundCaptureRequested = 0;
    XboxRenderReleaseLoadingBackgroundResources();
}

extern "C" void XboxRenderSetPendingViewRegion( INT X, INT Y, INT W, INT H, UBOOL ClearFullTarget )
{
    GRD_PendingLockX = X;
    GRD_PendingLockY = Y;
    GRD_PendingLockW = W;
    GRD_PendingLockH = H;
    GRD_PendingLockClearFullTarget = ClearFullTarget;
    GRD_HasPendingLockViewport = 1;
}

extern "C" void XboxRenderClearRegion( URenderDevice* RenderDevice, INT X, INT Y, INT W, INT H )
{
    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( RenderDevice );
    if( !Ren || !Ren->Device )
        return;

    HRESULT hrViewport = XboxRenderApplyViewport( Ren->Device, X, Y, W, H, Ren->ActualBackBufferW, Ren->ActualBackBufferH );
    HRESULT hrClear = Ren->Device->Clear( 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x00000000, 1.0f, 0 );
    static INT ClearFailLogCount = 0;
    if( (FAILED(hrViewport) || FAILED(hrClear)) && ClearFailLogCount < 8 )
    {
        ClearFailLogCount++;
        GXboxLog.Write( "RCLR region=%d,%d %dx%d viewport=0x%08X clear=0x%08X",
            X, Y, W, H, (DWORD)hrViewport, (DWORD)hrClear );
    }
}

static void XboxRenderLoadingRect( UXboxRenderDevice* Ren, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, DWORD Color )
{
    if( !Ren || !Ren->Device )
        return;

    const FLOAT RHW = 1.0f;
    const FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;
    FXboxTLVertex Verts[6];

    Verts[0].x = X1 - 0.5f; Verts[0].y = Y1 - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Color; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = X2 - 0.5f; Verts[1].y = Y1 - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Color; Verts[1].u = 0; Verts[1].v = 0;
    Verts[2].x = X2 - 0.5f; Verts[2].y = Y2 - 0.5f; Verts[2].rhw = RHW; Verts[2].z = SZ; Verts[2].color = Color; Verts[2].u = 0; Verts[2].v = 0;
    Verts[3] = Verts[0];
    Verts[4] = Verts[2];
    Verts[5].x = X1 - 0.5f; Verts[5].y = Y2 - 0.5f; Verts[5].rhw = RHW; Verts[5].z = SZ; Verts[5].color = Color; Verts[5].u = 0; Verts[5].v = 0;

    Ren->DrawPrimitiveVB( D3DPT_TRIANGLELIST, 2, Verts, sizeof(FXboxTLVertex), "loading-rect" );
}

static UBOOL XboxRenderDrawLoadingBackground( UXboxRenderDevice* Ren, FLOAT W, FLOAT H )
{
    if( !Ren || !Ren->Device || !GRD_LoadingBackgroundReady || !GRD_LoadingBackgroundTexture )
        return 0;

    const FLOAT RHW = 1.0f;
    const FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;
    const FLOAT UMax = (FLOAT)GRD_LoadingBackgroundW;
    const FLOAT VMax = (FLOAT)GRD_LoadingBackgroundH;
    FXboxTLVertex Verts[4];

    Verts[0].x = -0.5f;     Verts[0].y = -0.5f;     Verts[0].z = SZ; Verts[0].rhw = RHW; Verts[0].color = 0xFFFFFFFF; Verts[0].u = 0.0f; Verts[0].v = 0.0f;
    Verts[1].x = W - 0.5f;  Verts[1].y = -0.5f;     Verts[1].z = SZ; Verts[1].rhw = RHW; Verts[1].color = 0xFFFFFFFF; Verts[1].u = UMax;  Verts[1].v = 0.0f;
    Verts[2].x = W - 0.5f;  Verts[2].y = H - 0.5f;  Verts[2].z = SZ; Verts[2].rhw = RHW; Verts[2].color = 0xFFFFFFFF; Verts[2].u = UMax;  Verts[2].v = VMax;
    Verts[3].x = -0.5f;     Verts[3].y = H - 0.5f;  Verts[3].z = SZ; Verts[3].rhw = RHW; Verts[3].color = 0xFFFFFFFF; Verts[3].u = 0.0f; Verts[3].v = VMax;

    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP );
    Ren->SetCachedTextureStageState( 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_POINT );
    Ren->SetCachedTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_POINT );
    Ren->SetCachedTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_NONE );
    Ren->Device->SetTexture( 0, GRD_LoadingBackgroundTexture );
    Ren->BoundCacheID[0] = 0;
    Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );
    HRESULT hrDraw = Ren->DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "loading-bg" );
    Ren->Device->SetTexture( 0, NULL );
    Ren->BoundCacheID[0] = 0;
    return SUCCEEDED(hrDraw);
}

extern "C" UBOOL XboxRenderDrawLoadingFrame( URenderDevice* RenderDevice, INT Step, INT DrawCount )
{
    guard(XboxRenderDrawLoadingFrame);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( RenderDevice );
    if( !Ren || !Ren->Device || Ren->SceneOpen || !GRD_LoadingBackgroundReady || !GRD_LoadingBackgroundTexture )
        return 0;

    Ren->FrameCounter++;
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
    GRD_HotTraceBudget  = 0;
    GRD_MaxPolyVerts    = 0;
    GRD_LastOp          = "LoadingFrame";
    GRD_FrameStartSeconds = appSeconds();

    const UINT BackW = Ren->ActualBackBufferW ? Ren->ActualBackBufferW : 640;
    const UINT BackH = Ren->ActualBackBufferH ? Ren->ActualBackBufferH : 480;
    HRESULT hrViewport = XboxRenderApplyViewport( Ren->Device, 0, 0, BackW, BackH, BackW, BackH );
    HRESULT hrBegin = SUCCEEDED(hrViewport) ? Ren->Device->BeginScene() : E_FAIL;
    if( FAILED(hrViewport) || FAILED(hrBegin) )
    {
        GXboxLog.Write( "RLOAD failed frame=%d step=%d draw=%d viewport=0x%08X begin=0x%08X",
            Ren->FrameCounter, Step, DrawCount, (DWORD)hrViewport, (DWORD)hrBegin );
        return 0;
    }

    Ren->SceneOpen = 1;
    const FLOAT W = (FLOAT)BackW;
    const FLOAT H = (FLOAT)BackH;
    Ren->FlushDGPBatch( "loading-frame" );
    Ren->FlushDTBatch( "loading-frame" );
    Ren->DisableStage1();
    Ren->RestoreDefaultTextureStages();
    if( !XboxRenderDrawLoadingBackground( Ren, W, H ) )
    {
        HRESULT hrEnd = Ren->Device->EndScene();
        Ren->SceneOpen = 0;
        Ren->Device->SetStreamSource( 0, NULL, 0 );
        GRD_DrawVBStreamBound = 0;
        GRD_DrawVBStreamStride = 0;
        GXboxLog.Write( "RLOAD failed frame=%d step=%d draw=%d background=0 end=0x%08X",
            Ren->FrameCounter, Step, DrawCount, (DWORD)hrEnd );
        return 0;
    }
    Ren->RestoreDefaultTextureStages();
    Ren->Device->SetTexture( 0, NULL );
    Ren->BoundCacheID[0] = 0;
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_FALSE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );
    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
    Ren->SetCachedRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
    Ren->SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
    Ren->SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
    Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );

    const FLOAT Scale = H / 480.0f;
    const FLOAT CX = W * 0.5f;
    const FLOAT CY = H - 30.0f * Scale;
    const FLOAT Radius = 13.0f * Scale;
    const FLOAT Dot = Max<FLOAT>( 2.5f, 3.5f * Scale );
    static const FLOAT Offsets[8][2] =
    {
        {  0.0f,-1.0f }, {  0.707f,-0.707f }, {  1.0f, 0.0f }, {  0.707f, 0.707f },
        {  0.0f, 1.0f }, { -0.707f, 0.707f }, { -1.0f, 0.0f }, { -0.707f,-0.707f }
    };

    for( INT i=0; i<8; i++ )
    {
        const INT Age = (i - (Step & 7) + 8) & 7;
        const BYTE A = (BYTE)(70 + (7 - Age) * 22);
        const BYTE R = (BYTE)(18 + (7 - Age) * 4);
        const BYTE G = (BYTE)(80 + (7 - Age) * 18);
        const BYTE B = (BYTE)(135 + (7 - Age) * 14);
        const FLOAT X = CX + Offsets[i][0] * Radius;
        const FLOAT Y = CY + Offsets[i][1] * Radius;
        const DWORD Color = ((DWORD)A << 24) | ((DWORD)R << 16) | ((DWORD)G << 8) | (DWORD)B;
        XboxRenderLoadingRect( Ren, X - Dot, Y - Dot, X + Dot, Y + Dot, Color );
    }

    Ren->RestoreDefaultTextureStages();
    Ren->SetCachedRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
    Ren->SetCachedRenderState( D3DRS_ZWRITEENABLE, TRUE );
    Ren->SetCachedRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
    Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
    Ren->CurrentPolyFlags = 0xFFFFFFFF;

    HRESULT hrEnd = Ren->Device->EndScene();
    Ren->SceneOpen = 0;
    Ren->Device->SetStreamSource( 0, NULL, 0 );
    GRD_DrawVBStreamBound = 0;
    GRD_DrawVBStreamStride = 0;
    HRESULT hrPresent = SUCCEEDED(hrEnd) ? Ren->Device->Present( NULL, NULL, NULL, NULL ) : E_FAIL;

    if( DrawCount <= 16 || (DrawCount & 7) == 0 || FAILED(hrEnd) || FAILED(hrPresent) )
    {
        GXboxLog.Write( "RLOAD frame=%d step=%d draw=%d end=0x%08X present=0x%08X",
            Ren->FrameCounter, Step, DrawCount, (DWORD)hrEnd, (DWORD)hrPresent );
    }

    return SUCCEEDED(hrEnd) && SUCCEEDED(hrPresent);
    unguard;
}

static HRESULT XboxRenderCreateDeviceChecked( IDirect3D8* InDirect3D, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* PP, IDirect3DDevice8** OutDevice, const char* Label )
{
    HRESULT hr = E_FAIL;
    DWORD ExceptionCode = 0;
    if( !InDirect3D )
    {
        GXboxLog.Write( "XboxRender::Init: %s CreateDevice skipped; Direct3D is NULL", Label );
        if( OutDevice )
            *OutDevice = NULL;
        return E_FAIL;
    }
    __try
    {
        hr = InDirect3D->CreateDevice(
            0,
            D3DDEVTYPE_HAL,
            NULL,
            BehaviorFlags,
            PP,
            OutDevice
        );
        GXboxLog.Write( "XboxRender::Init: %s CreateDevice returned 0x%08X ptr=0x%08X",
            Label, (DWORD)hr, OutDevice ? (DWORD)*OutDevice : 0 );
    }
    __except( ExceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER )
    {
        GXboxLog.Write( "XboxRender::Init: %s CreateDevice SEH exception=0x%08X", Label, ExceptionCode );
        hr = E_FAIL;
        if( OutDevice )
            *OutDevice = NULL;
    }
    return hr;
}

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

static void RenderBlockAndReleasePalette( IDirect3DPalette8*& Palette )
{
    if( Palette )
    {
        Palette->BlockUntilNotBusy();
        Palette->Release();
        Palette = NULL;
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
    SupportsTC           = 1;
    PrecacheOnFlip       = 0;
    SupportsLazyTextures = 0;
    PrefersDeferredLoad  = 1;
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
    GXboxLog.Write( "XboxRender::Init: fog maps disabled; DXT/S3TC compressed texture upload enabled" );
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
    GRD_SourceUnloadLogCount = 0;
    GRD_LowMemoryScaleLogCount = 0;
    GRD_LowMemoryTextureMode = 0;
    GRD_LastOp          = "Init";

    // Z-buffer formula: SZ = ProjZRatio + ProjZOffset * RHW
    zNear      = 1.f;
    zFar       = 32767.f;
    ProjZRatio  = zFar / (zFar - zNear);
    ProjZOffset = -ProjZRatio * zNear;

    // Use the same Direct3DCreate8 entry path as the May 15 hardware-oriented
    // renderer snapshot, then guard CreateDevice so hardware failures are logged.
    GXboxLog.Write( "XboxRender::Init: calling Direct3DCreate8(D3D_SDK_VERSION=%d)", D3D_SDK_VERSION );
    Direct3D = Direct3DCreate8( D3D_SDK_VERSION );
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
    // Hardware baseline from the May 15 renderer snapshot:
    //   swizzled X8R8G8B8 backbuffer, D16 auto-depth, single backbuffer,
    //   DISCARD swap, and immediate presentation.
    // D16 intentionally has no stencil plane; do not clear stencil below.
    PP.BackBufferWidth  = 640;
    PP.BackBufferHeight = 480;
    PP.BackBufferFormat = D3DFMT_X8R8G8B8;
    PP.BackBufferCount  = 1;
    PP.MultiSampleType  = D3DMULTISAMPLE_NONE;
    PP.SwapEffect       = D3DSWAPEFFECT_DISCARD;
    PP.hDeviceWindow    = NULL;
    PP.Windowed         = FALSE;
    PP.EnableAutoDepthStencil = TRUE;
    PP.AutoDepthStencilFormat = D3DFMT_D16;
    PP.FullScreen_RefreshRateInHz      = 60;
    PP.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    // May 15 hardware-oriented path configured push buffers before CreateDevice.
    Direct3D_SetPushBufferSize( 512 * 1024, 64 * 1024 );
    GXboxLog.Write( "XboxRender::Init: SetPushBufferSize(512K, 64K) called" );

    // Create device. Real hardware must not let D3D's create path escape to
    // main() as an unknown exception; retry once without PUREDEVICE.
    GXboxLog.Write( "XboxRender::Init: calling CreateDevice primary()" );
    HRESULT hr = XboxRenderCreateDeviceChecked(
        Direct3D,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
        &PP,
        &Device,
        "primary"
    );
    if( FAILED(hr) || !Device )
    {
        D3DPRESENT_PARAMETERS FallbackPP = PP;
        FallbackPP.AutoDepthStencilFormat = D3DFMT_D16;
        GXboxLog.Write( "XboxRender::Init: primary failed; retrying fallback D16/non-pure" );
        hr = XboxRenderCreateDeviceChecked(
            Direct3D,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &FallbackPP,
            &Device,
            "fallback"
        );
        if( SUCCEEDED(hr) && Device )
            PP = FallbackPP;
    }
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
    GRD_DisplayCalibrationDirty = 1;
    GRD_DisplayPostFailed = 0;
    GRD_DisplayPostLogged = 0;

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

    GXboxLog.Write( "XboxRender::Init: upload chunk bytes=%d availKB=%u",
        XBOX_UPLOAD_CHUNK_BYTES, (unsigned)RenderAvailPhysKB() );

    // NOTE: render/texture-stage state is deliberately NOT set here. State
    // setup is performed inside Lock(), after the first depth/target clear.

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
    XboxRenderReleaseDisplayCalibrationResources();
    XboxRenderReleaseLoadingBackgroundResources();
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
            RenderBlockAndReleasePalette( Entry->pPalette );
            Entry = Entry->HashNext;
        }
        TexCache[i] = NULL;
    }
    TexPoolNext = 0;
    TexLiveBytes = 0;
    appMemzero( TexPool, sizeof(TexPool) );
}

void UXboxRenderDevice::ReleaseTexCacheEntry( FXboxTexCacheEntry* Entry )
{
    if( !Entry )
        return;

    if( Entry->CacheID == BoundCacheID[0] )
    {
        Device->SetTexture( 0, NULL );
        BoundCacheID[0] = 0;
    }
    if( Entry->CacheID == BoundCacheID[1] )
    {
        Device->SetTexture( 1, NULL );
        BoundCacheID[1] = 0;
    }

    RenderBlockAndReleaseTexture( Entry->pTexture );
    RenderBlockAndReleasePalette( Entry->pPalette );
    TexLiveBytes -= Entry->Bytes;
    if( TexLiveBytes < 0 )
        TexLiveBytes = 0;
    Entry->Bytes = 0;
}

void UXboxRenderDevice::EvictTexCacheForUpload( INT NeededBytes )
{
    const DWORD UploadKB = (DWORD)(Max(NeededBytes, 0) / 1024 + 768);
    const DWORD WantedKB = Max<DWORD>( UploadKB, 3072 );
    INT Released = 0;
    INT ReleasedKB = 0;
    while( (RenderAvailPhysKB() < WantedKB || TexLiveBytes > XBOX_TEX_LIVE_BUDGET) && Released < 48 )
    {
        INT BestIndex = -1;
        INT BestAge = -1;
        for( INT i = 0; i < TexPoolNext; i++ )
        {
            FXboxTexCacheEntry* Candidate = &TexPool[i];
            if( !Candidate->pTexture )
                continue;
            if( Candidate->Pinned )
                continue;
            if( Candidate->CacheID == BoundCacheID[0] || Candidate->CacheID == BoundCacheID[1] )
                continue;
            INT Age = FrameCounter - Candidate->FrameCounter;
            if( Age <= 0 )
                continue;
            if( Age > BestAge )
            {
                BestAge = Age;
                BestIndex = i;
            }
        }
        if( BestIndex < 0 )
            break;

        FXboxTexCacheEntry* Entry = &TexPool[BestIndex];
        ReleasedKB += Entry->Bytes / 1024;
        ReleaseTexCacheEntry( Entry );
        Released++;
    }

    if( Released && (GRD_TotalTexCreates <= 24 || (GRD_TotalTexCreates % 64) == 0 || RenderAvailPhysKB() < WantedKB) )
    {
        static INT EvictLogCount = 0;
        EvictLogCount++;
        DWORD AvailKB = RenderAvailPhysKB();
        if( EvictLogCount <= 48 || (EvictLogCount % 256) == 0 || AvailKB < 1024 )
            GXboxLog.Write( "RTEX evict #%d f=%d count=%d freedKB=%d liveKB=%d availKB=%u needKB=%d wantKB=%u",
                EvictLogCount, FrameCounter, Released, ReleasedKB, TexLiveBytes / 1024, (unsigned)AvailKB, NeededBytes / 1024, (unsigned)WantedKB );
    }
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

    FrameCounter++;
    if( TexLiveBytes > XBOX_TEX_LIVE_BUDGET )
        EvictTexCacheForUpload( 0 );
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
    HRESULT hrViewport = S_OK;
    UBOOL bClearRenderLock = XboxSplitShouldClearRenderLock();
    UBOOL bClearedFullTarget = 0;
    if( GRD_HasPendingLockViewport )
    {
        if( bClearRenderLock && GRD_PendingLockClearFullTarget )
        {
            hrViewport = XboxRenderApplyViewport( Device, 0, 0, ActualBackBufferW, ActualBackBufferH, ActualBackBufferW, ActualBackBufferH );
            if( SUCCEEDED(hrViewport) )
            {
                hrClear = Device->Clear( 0, NULL,
                    D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                    0x00000000,
                    1.0f, 0 );
                bClearedFullTarget = SUCCEEDED(hrClear);
            }
        }

        hrViewport = XboxRenderApplyViewport( Device, GRD_PendingLockX, GRD_PendingLockY, GRD_PendingLockW, GRD_PendingLockH, ActualBackBufferW, ActualBackBufferH );
        GRD_HasPendingLockViewport = 0;
        GRD_PendingLockClearFullTarget = 0;
    }
    if( bClearRenderLock && !bClearedFullTarget )
    {
        hrClear = Device->Clear( 0, NULL,
            D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
            0x00000000,
            1.0f, 0 );
    }

    HRESULT hrBegin = Device->BeginScene();
    SceneOpen = SUCCEEDED(hrBegin);

    if( RenderFrameSummaryLog( FrameCounter ) || FAILED(hrViewport) || FAILED(hrClear) || FAILED(hrBegin) )
        GXboxLog.Write( "RLOCK f=%d viewport=0x%08X clear=0x%08X begin=0x%08X",
            FrameCounter, (DWORD)hrViewport, (DWORD)hrClear, (DWORD)hrBegin );

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

    XboxRenderApplyDisplayCalibration( this );

    XboxRenderFlushMenuRectBatch( this, "Unlock" );
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
    if( SceneOpen && GShowXboxPerfOverlay )
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

    XboxRenderApplyDisplayPostProcess( this );
    if( GRD_LoadingBackgroundCaptureRequested )
        XboxRenderCaptureLoadingBackgroundNow( this );

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
        GXboxLog.Write( "PERF fps=%.1f frameMS=%.2f renderMS=%.2f presentMS=%.2f DCS=%d DGP=%d DT=%d prim=%d verts=%d dgpBatch=%d/%d dtBatch=%d/%d vbLocks=%d vbWraps=%d up=%d/%d vbKB=%d state=%d/%d texBind=%d texNew=%d texUp=%d texDef=%d splits=%d liveKB=%d texKB=%d availKB=%u clampBad=%d clampOk=%d nobase=%d",
            GRD_DisplayFPS, GRD_LastFrameMS, GRD_LastRenderMS, GRD_LastPresentMS,
            GRD_FrameDCS, GRD_FrameDGP, GRD_FrameDT, GRD_FramePrims, GRD_FrameVerts,
            GRD_FrameDGPBatches, GRD_FrameDGPBatchedPolys, GRD_FrameDTBatches, GRD_FrameDTBatchedTiles,
            GRD_FrameVBLocks, GRD_FrameVBWraps, GRD_FrameUPDraws, GRD_FrameUPFails, GRD_FrameVBBytes / 1024,
            GRD_FrameStateSets, GRD_FrameStateSkips,
            GRD_FrameTexBinds, GRD_FrameTexCreates, GRD_FrameTexUploads, GRD_FrameTexDeferred, GRD_FrameSceneSplits,
            TexLiveBytes / 1024, GRD_TotalTexBytes / 1024, (unsigned)RenderAvailPhysKB(),
            GRD_ClampMismatchCount, GRD_ClampMatchCount, GRD_TotalNoBaseDraw );
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

    // Clamp to the ACTUAL backbuffer extents. CXBX-R can allocate a smaller
    // surface than the engine's logical 640x480 dimensions.
    XboxRenderApplyViewport( Device, Frame->XB, Frame->YB, Frame->X, Frame->Y, ActualBackBufferW, ActualBackBufferH );

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
UBOOL UXboxRenderDevice::SetTextureD3D( INT Stage, FTextureInfo& Info, DWORD PolyFlags )
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
        return 1;
    }

    // P8 masked textures depend on draw flags during upload: palette index 0
    // must become alpha 0. Cache that variant separately from ordinary P8
    // uploads, or a texture first seen opaque will stay opaque when later
    // drawn as PF_Masked.
    UBOOL bNeedsMaskedAlpha = (Info.Format == TEXF_P8 && (PolyFlags & PF_Masked));

    // Early out if texture already bound.
    UBOOL bRgba7NeedsMaxColor = (Info.Format == TEXF_RGBA7 && Info.MaxColor && GET_COLOR_DWORD(*Info.MaxColor) == 0xFFFFFFFF);

    if( BoundCacheID[Stage] == Info.CacheID && !Info.bRealtimeChanged && !bRgba7NeedsMaxColor )
    {
        INT BoundHashIndex = ((7 * (DWORD)Info.CacheID) + (DWORD)(Info.CacheID >> 32)) & (XBOX_TEX_CACHE_SIZE - 1);
        FXboxTexCacheEntry* BoundEntry;
        for( BoundEntry = TexCache[BoundHashIndex]; BoundEntry; BoundEntry = BoundEntry->HashNext )
        {
            if( BoundEntry->CacheID == Info.CacheID )
                break;
        }
        if( BoundEntry && BoundEntry->pTexture && BoundEntry->MaskedAlpha == bNeedsMaskedAlpha )
            return 1;
        BoundCacheID[Stage] = 0;
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
            return 1;
        }
    }

    UBOOL bMaskedAlphaChanged = (Entry && Entry->pTexture && Entry->MaskedAlpha != bNeedsMaskedAlpha);

    if( !Entry || !Entry->pTexture || Info.bRealtimeChanged || bRgba7NeedsMaxColor || bMaskedAlphaChanged )
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
                    if( Candidate->Pinned )
                        continue;
                    if( Candidate->CacheID == BoundCacheID[0] || Candidate->CacheID == BoundCacheID[1] )
                        continue;
                    INT Age = FrameCounter - Candidate->FrameCounter;
                    if( Age <= 0 )
                        continue;
                    if( Age > BestAge )
                    {
                        BestAge = Age;
                        BestIndex = i;
                    }
                }
                if( BestIndex < 0 )
                {
                    GRD_FrameTexSkipped++;
                    GRD_TotalTexSkipped++;
                    GXboxLog.Write( "RTEX reuse no-candidate frame=%d pool=%d liveKB=%d availKB=%u id=%08X:%08X",
                        FrameCounter, TexPoolNext, TexLiveBytes / 1024,
                        (unsigned)RenderAvailPhysKB(), (DWORD)(Info.CacheID >> 32), (DWORD)Info.CacheID );
                    Device->SetTexture( Stage, NULL );
                    BoundCacheID[Stage] = 0;
                    ResumeSceneAfterTextureUpload( "tex-reuse-no-candidate" );
                    return 0;
                }

                Entry = &TexPool[BestIndex];
                INT OldHash = ((7 * (DWORD)Entry->CacheID) + (DWORD)(Entry->CacheID >> 32)) & (XBOX_TEX_CACHE_SIZE - 1);
                FXboxTexCacheEntry** Link = &TexCache[OldHash];
                while( *Link && *Link != Entry )
                    Link = &(*Link)->HashNext;
                if( *Link == Entry )
                    *Link = Entry->HashNext;

                GRD_TotalTexReuse++;
                if( Entry->pTexture )
                {
                    // Diagnosis only: record whether this blanket unbind is
                    // about to drop a stage binding that belongs to some other
                    // texture. The candidate scan above already excludes
                    // BoundCacheID[0]/[1], so any live binding cleared here is
                    // collateral damage rather than a necessary unbind.
                    UBOOL bClobber0 = (BoundCacheID[0] != 0 && BoundCacheID[0] != Entry->CacheID);
                    UBOOL bClobber1 = (BoundCacheID[1] != 0 && BoundCacheID[1] != Entry->CacheID);
                    if( bClobber0 || bClobber1 )
                    {
                        GRD_TotalStage0Clobber++;
                        if( GRD_ClobberLogCount < 64 || (GRD_TotalStage0Clobber % 128) == 0 )
                        {
                            GRD_ClobberLogCount++;
                            GXboxLog.Write( "RTEX clobber #%d f=%d slot=%d s0=%d s1=%d bound0=%08X:%08X bound1=%08X:%08X victim=%08X:%08X want=%08X:%08X",
                                GRD_TotalStage0Clobber, FrameCounter, BestIndex, (INT)bClobber0, (INT)bClobber1,
                                (DWORD)(BoundCacheID[0] >> 32), (DWORD)BoundCacheID[0],
                                (DWORD)(BoundCacheID[1] >> 32), (DWORD)BoundCacheID[1],
                                (DWORD)(Entry->CacheID >> 32), (DWORD)Entry->CacheID,
                                GRD_LastTextureIDHi, GRD_LastTextureIDLo );
                        }
                    }

                    for( INT UnbindStage = 0; UnbindStage < 4; UnbindStage++ )
                        Device->SetTexture( UnbindStage, NULL );
                    BoundCacheID[0] = 0;
                    BoundCacheID[1] = 0;
                    RenderBlockAndReleaseTexture( Entry->pTexture );
                }
                RenderBlockAndReleasePalette( Entry->pPalette );
                TexLiveBytes -= Entry->Bytes;
                Entry->Bytes = 0;
                if( GRD_TotalTexReuse <= 64 || (GRD_TotalTexReuse % 128) == 0 )
                    GXboxLog.Write( "RTEX reuse #%d frame=%d slot=%d age=%d pool=%d liveKB=%d availKB=%u old=%08X:%08X",
                        GRD_TotalTexReuse, FrameCounter, BestIndex, BestAge, TexPoolNext, TexLiveBytes / 1024,
                        (unsigned)RenderAvailPhysKB(), (DWORD)(Entry->CacheID >> 32), (DWORD)Entry->CacheID );
            }
            Entry->CacheID   = Info.CacheID;
            Entry->pTexture  = NULL;
            Entry->pPalette  = NULL;
            Entry->USize     = 0;
            Entry->VSize     = 0;
            Entry->NumMips   = 0;
            Entry->FirstMip  = 0;
            Entry->UIndex    = 0;
            Entry->VIndex    = 1;
            Entry->Format    = D3DFMT_UNKNOWN;
            Entry->MaskedAlpha = 0;
            Entry->Bytes     = 0;
            Entry->CreateFailedFrame = -1000000;
            Entry->CreateFailedBytes = 0;
            Entry->Pinned    = 0;
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

        // Retail Xbox memory is shared by the CPU and GPU. Large maps can leave
        // too little room to load a HUD texture while keeping every full-size
        // world texture resident. Latch a pressure mode for the rest of the
        // session, but preserve skins and UI at full resolution.
        DWORD TextureAvailKB = RenderAvailPhysKB();
        if( !GRD_LowMemoryTextureMode && TextureAvailKB < 6144 )
        {
            GRD_LowMemoryTextureMode = 1;
            GXboxLog.Write( "RTEX lowmem-mode f=%d availKB=%u liveKB=%d pool=%d",
                FrameCounter, (unsigned)TextureAvailKB, TexLiveBytes / 1024, TexPoolNext );
        }

        INT SoftwareMipBias = 0;
        UBOOL bLowMemoryWorldTexture =
            GRD_LowMemoryTextureMode &&
            Info.Texture &&
            Info.Texture->LODSet == LODSET_World &&
            (SrcUSize >= 128 || SrcVSize >= 128);
        if( bLowMemoryWorldTexture )
        {
            if( FirstMip + 1 < Info.NumMips && Info.Mips[FirstMip + 1] )
            {
                FirstMip++;
                SrcUSize = Info.Mips[FirstMip]->USize;
                SrcVSize = Info.Mips[FirstMip]->VSize;
            }
            else if( Info.Format != TEXF_DXT1 )
            {
                SoftwareMipBias = 1;
            }
        }
        // PC D3D7 transposed tall textures to satisfy old surface-pool
        // constraints. Xbox D3D8 accepts rectangular power-of-two swizzled
        // textures directly; keeping the source orientation avoids making the
        // base texture upload path disagree with the UT surface UVs.
        UBOOL bSwapUV = 0;
        INT USize = Max( 1, SrcUSize >> SoftwareMipBias );
        INT VSize = Max( 1, SrcVSize >> SoftwareMipBias );

        if( bLowMemoryWorldTexture && (SoftwareMipBias || FirstMip > 0) && GRD_LowMemoryScaleLogCount < 48 )
        {
            GRD_LowMemoryScaleLogCount++;
            GXboxLog.Write( "RTEX lowmem-scale #%d f=%d stage=%d id=%08X:%08X fmt=%d lod=%d src=%dx%d dst=%dx%d first=%d soft=%d availKB=%u",
                GRD_LowMemoryScaleLogCount, FrameCounter, Stage,
                GRD_LastTextureIDHi, GRD_LastTextureIDLo, Info.Format,
                Info.Texture ? (INT)Info.Texture->LODSet : -1,
                SrcUSize, SrcVSize, USize, VSize, FirstMip, SoftwareMipBias,
                (unsigned)TextureAvailKB );
        }

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
            return 0;
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
            GXboxLog.Write( "RTEX dxt #%d f=%d stage=%d id=%08X:%08X size=%dx%d",
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

        // XBOXRENDER_CLAMPTRACE: UScale above assumes the texture content sits
        // at native texel positions across the full SrcUSize allocation. The
        // upload instead rescales the UClamp/VClamp rectangle over the whole
        // destination, so any texture where clamp < size renders stretched by
        // size/clamp. Log the ratio so we know how many real world textures
        // this hits rather than guessing.
        if( (Info.UClamp > 0 && Info.UClamp != SrcUSize) || (Info.VClamp > 0 && Info.VClamp != SrcVSize) )
        {
            GRD_ClampMismatchCount++;
            if( GRD_ClampMismatchLogCount < 40 )
            {
                GRD_ClampMismatchLogCount++;
                GXboxLog.Write( "CLAMPTRACE #%d f=%d lod=%d size=%dx%d clamp=%dx%d stretch=%.3fx%.3f fmt=%d",
                    GRD_ClampMismatchCount, FrameCounter,
                    Info.Texture ? (INT)Info.Texture->LODSet : -1,
                    SrcUSize, SrcVSize, Info.UClamp, Info.VClamp,
                    Info.UClamp > 0 ? (FLOAT)SrcUSize / (FLOAT)Info.UClamp : 1.0f,
                    Info.VClamp > 0 ? (FLOAT)SrcVSize / (FLOAT)Info.VClamp : 1.0f,
                    Info.Format );
            }
        }
        else
        {
            GRD_ClampMatchCount++;
        }
        Entry->UIndex = bSwapUV ? 1 : 0;
        Entry->VIndex = bSwapUV ? 0 : 1;

        UBOOL bUnloadSourceAfterUpload = !Info.bRealtime && !Info.bParametric;

        INT ApproxBytes = 0;
        INT NumMips = Info.NumMips - FirstMip;
        if( NumMips < 1 ) NumMips = 1;
        D3DFORMAT DestFormat = (Info.Format == TEXF_DXT1) ? D3DFMT_DXT1 : (Info.Format == TEXF_P8 ? D3DFMT_P8 : D3DFMT_A8R8G8B8);
        INT ExpectedBytes = 0;
        for( INT tm = 0; tm < NumMips; tm++ )
        {
            if( DestFormat == D3DFMT_DXT1 )
                ExpectedBytes += Max(1, ((USize >> tm) + 3) / 4) * Max(1, ((VSize >> tm) + 3) / 4) * 8;
            else if( DestFormat == D3DFMT_P8 )
                ExpectedBytes += Max(1, USize >> tm) * Max(1, VSize >> tm);
            else
                ExpectedBytes += Max(1, USize >> tm) * Max(1, VSize >> tm) * 4;
        }
        UBOOL bNeedCreate =
            !Entry->pTexture ||
            Entry->USize    != USize ||
            Entry->VSize    != VSize ||
            Entry->NumMips  != NumMips ||
            Entry->FirstMip != FirstMip ||
            Entry->UIndex   != (bSwapUV ? 1 : 0) ||
            Entry->VIndex   != (bSwapUV ? 0 : 1) ||
            Entry->Format   != DestFormat ||
            Entry->MaskedAlpha != bNeedsMaskedAlpha;
        INT UploadSeq = ++GRD_FrameTexUploadSeq;
        UBOOL bHotUpload = ((RenderHotFrame( FrameCounter ) && UploadSeq <= 180) || RenderTextureHotTrace( TexPoolNext, GRD_TotalTexCreates ));
        if( bHotUpload && RenderHotTrace() )
            GXboxLog.Write( "RTEXUP begin f=%d seq=%d stage=%d id=%08X:%08X fmt=%d size=%dx%d mips=%d first=%d need=%d realtime=%d tex=0x%08X",
                FrameCounter, UploadSeq, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                Info.Format, USize, VSize, NumMips, FirstMip, bNeedCreate, bRealtimeChanged || bForceRgba7MaxUpload, (DWORD)Entry->pTexture );

        if( bNeedCreate )
        {
            if( FrameCounter - Entry->CreateFailedFrame < 15 && Entry->CreateFailedBytes >= ExpectedBytes )
            {
                GRD_FrameTexSkipped++;
                GRD_TotalTexSkipped++;
                Device->SetTexture( Stage, NULL );
                BoundCacheID[Stage] = 0;
                StageUScale[Stage] = Entry->UScale;
                StageVScale[Stage] = Entry->VScale;
                StageUIndex[Stage] = Entry->UIndex;
                StageVIndex[Stage] = Entry->VIndex;
                Info.bRealtimeChanged = 0;
                if( bUnloadSourceAfterUpload )
                    Info.Unload();
                ResumeSceneAfterTextureUpload( "tex-create-retry-skip" );
                return 0;
            }
            EvictTexCacheForUpload( ExpectedBytes );
        }

        // Static texture locks remain lazy on Xbox. Reserve shared memory
        // before loading source mips, then release those CPU bytes after the
        // D3D texture owns its copy.
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

        // Xbox D3D8 is much less forgiving than D3D7's system-surface upload
        // path: do not LockRect a texture while it is still resident in either
        // texture stage. Rebind after the upload below.
        if( Entry->pTexture && (bRealtimeChanged || bForceRgba7MaxUpload || bMaskedAlphaChanged) )
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
                RenderBlockAndReleasePalette( Entry->pPalette );
                if( bHotUpload && GVerboseRenderPerfLog )
                    GXboxLog.Write( "RTEX precreate f=%d seq=%d createNext=%d stage=%d id=%08X:%08X fmt=DXT1 size=%dx%d mips=%d bytes=%d pool=%d liveKB=%d availKB=%u",
                        FrameCounter, UploadSeq, GRD_TotalTexCreates + 1, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                        USize, VSize, NumMips, ApproxBytes, TexPoolNext, TexLiveBytes / 1024, (unsigned)RenderAvailPhysKB() );
                hrCreate = Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_DXT1, D3DPOOL_DEFAULT, &Entry->pTexture );
                if( SUCCEEDED(hrCreate) && Entry->pTexture )
                {
                    Entry->USize    = USize;
                    Entry->VSize    = VSize;
                    Entry->NumMips  = NumMips;
                    Entry->FirstMip = FirstMip;
                    Entry->UIndex   = bSwapUV ? 1 : 0;
                    Entry->VIndex   = bSwapUV ? 0 : 1;
                    Entry->Format   = DestFormat;
                    Entry->MaskedAlpha = bNeedsMaskedAlpha;
                    TexLiveBytes   -= Entry->Bytes;
                    Entry->Bytes    = ApproxBytes;
                    TexLiveBytes   += Entry->Bytes;
                    GRD_FrameTexCreates++;
                    GRD_TotalTexCreates++;
                    Entry->CreateFailedFrame = -1000000;
                    Entry->CreateFailedBytes = 0;
                }
            }
            GRD_FrameTexUploads++;
            GRD_TotalTexUploads++;
            GRD_TotalTexBytes += ApproxBytes;
            if( FAILED(hrCreate) || !Entry->pTexture || (GVerboseRenderPerfLog && bNeedCreate && (GRD_TotalTexCreates <= 16 || (GRD_TotalTexCreates % 64) == 0)) )
            {
                if( bNeedCreate && (FAILED(hrCreate) || !Entry->pTexture) )
                {
                    Entry->CreateFailedFrame = FrameCounter;
                    Entry->CreateFailedBytes = ApproxBytes;
                }
                GXboxLog.Write( "RTEX %s create#=%d upload#=%d frame=%d stage=%d id=%08X:%08X fmt=DXT1 size=%dx%d mips=%d bytes=%d hr=0x%08X tex=0x%08X d3dpool=DEFAULT cache=%d realtime=%d",
                    bNeedCreate ? "create" : "update", GRD_TotalTexCreates, GRD_TotalTexUploads, FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                    USize, VSize, NumMips, ApproxBytes, (DWORD)hrCreate, (DWORD)Entry->pTexture, TexPoolNext, bRealtimeChanged || bForceRgba7MaxUpload );
            }
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
        else if( Info.Format == TEXF_P8 )
        {
            // Native Xbox P8 texture plus hardware palette. This preserves the
            // original indexed art while avoiding 4x expansion to A8R8G8B8.
            HRESULT hrCreate = S_OK;
            HRESULT hrPalette = S_OK;
            for( INT tm = 0; tm < NumMips; tm++ )
                ApproxBytes += Max(1, USize >> tm) * Max(1, VSize >> tm);
            if( bNeedCreate )
            {
                if( Entry->pTexture )
                    RenderBlockAndReleaseTexture( Entry->pTexture );
                RenderBlockAndReleasePalette( Entry->pPalette );
                hrCreate = Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_P8, D3DPOOL_DEFAULT, &Entry->pTexture );
                if( SUCCEEDED(hrCreate) && Entry->pTexture )
                {
                    hrPalette = Device->CreatePalette( D3DPALETTE_256, &Entry->pPalette );
                    if( FAILED(hrPalette) || !Entry->pPalette )
                    {
                        RenderBlockAndReleaseTexture( Entry->pTexture );
                    }
                }
                if( SUCCEEDED(hrCreate) && Entry->pTexture && SUCCEEDED(hrPalette) && Entry->pPalette )
                {
                    Entry->USize    = USize;
                    Entry->VSize    = VSize;
                    Entry->NumMips  = NumMips;
                    Entry->FirstMip = FirstMip;
                    Entry->UIndex   = bSwapUV ? 1 : 0;
                    Entry->VIndex   = bSwapUV ? 0 : 1;
                    Entry->Format   = DestFormat;
                    Entry->MaskedAlpha = bNeedsMaskedAlpha;
                    TexLiveBytes   -= Entry->Bytes;
                    Entry->Bytes    = ApproxBytes;
                    TexLiveBytes   += Entry->Bytes;
                    GRD_FrameTexCreates++;
                    GRD_TotalTexCreates++;
                    Entry->CreateFailedFrame = -1000000;
                    Entry->CreateFailedBytes = 0;
                }
            }
            GRD_FrameTexUploads++;
            GRD_TotalTexUploads++;
            GRD_TotalTexBytes += ApproxBytes;
            if( FAILED(hrCreate) || FAILED(hrPalette) || !Entry->pTexture || !Entry->pPalette || (GVerboseRenderPerfLog && bNeedCreate && (GRD_TotalTexCreates <= 16 || (GRD_TotalTexCreates % 64) == 0)) )
            {
                if( bNeedCreate && (FAILED(hrCreate) || FAILED(hrPalette) || !Entry->pTexture || !Entry->pPalette) )
                {
                    Entry->CreateFailedFrame = FrameCounter;
                    Entry->CreateFailedBytes = ApproxBytes;
                }
                GXboxLog.Write( "RTEX %s create#=%d upload#=%d frame=%d stage=%d id=%08X:%08X fmt=P8 size=%dx%d mips=%d bytes=%d hr=0x%08X phr=0x%08X tex=0x%08X pal=0x%08X cache=%d masked=%d",
                    bNeedCreate ? "create" : "update", GRD_TotalTexCreates, GRD_TotalTexUploads, FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                    USize, VSize, NumMips, ApproxBytes, (DWORD)hrCreate, (DWORD)hrPalette, (DWORD)Entry->pTexture, (DWORD)Entry->pPalette, TexPoolNext, bNeedsMaskedAlpha );
            }

            if( Entry->pPalette && Info.Palette )
            {
                D3DCOLOR* PalColors = NULL;
                HRESULT hrPalLock = Entry->pPalette->Lock( &PalColors, 0 );
                if( SUCCEEDED(hrPalLock) && PalColors )
                {
                    for( INT i = 0; i < 256; i++ )
                    {
                        FColor& C = Info.Palette[i];
                        BYTE A = (i == 0 && (PolyFlags & PF_Masked)) ? 0 : C.A;
                        PalColors[i] = D3DCOLOR_ARGB( A, C.R, C.G, C.B );
                    }
                    Entry->pPalette->Unlock();
                }
                else
                {
                    GXboxLog.Write( "RTEX P8 palette lock failed frame=%d stage=%d id=%08X:%08X hr=0x%08X",
                        FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, (DWORD)hrPalLock );
                }
            }

            if( Entry->pTexture )
            {
                for( INT m = FirstMip; m < Info.NumMips; m++ )
                {
                    if( !Info.Mips[m] || !Info.Mips[m]->DataPtr )
                        continue;

                    INT MipW = Info.Mips[m]->USize;
                    INT MipH = Info.Mips[m]->VSize;
                    INT DestW = Max( 1, (bSwapUV ? MipH : MipW) >> SoftwareMipBias );
                    INT DestH = Max( 1, (bSwapUV ? MipW : MipH) >> SoftwareMipBias );
                    INT Need = DestW * DestH;
                    if( MipW < 1 || MipH < 1 || DestW < 1 || DestH < 1 || Need <= 0 || Need > (1024 * 1024) )
                    {
                        GRD_FrameTexSkipped++;
                        GRD_TotalTexSkipped++;
                        GXboxLog.Write( "RTEXUP P8 reject-mip f=%d seq=%d mip=%d mipSize=%dx%d dest=%dx%d need=%d",
                            FrameCounter, UploadSeq, m - FirstMip, MipW, MipH, DestW, DestH, Need );
                        continue;
                    }
                    INT RowBytes = DestW;
                    INT RowsPerChunk = RowBytes > 0 ? (XBOX_UPLOAD_CHUNK_BYTES / RowBytes) : 0;
                    if( RowsPerChunk < 1 )
                    {
                        GRD_FrameTexSkipped++;
                        GRD_TotalTexSkipped++;
                        GXboxLog.Write( "RTEXUP P8 row-too-wide f=%d seq=%d mip=%d rowBytes=%d chunk=%d",
                            FrameCounter, UploadSeq, m - FirstMip, RowBytes, XBOX_UPLOAD_CHUNK_BYTES );
                        continue;
                    }

                    BYTE* Src = (BYTE*)Info.Mips[m]->DataPtr;
                    INT CopyW = RenderMipClampSize( Info.UClamp, m, MipW );
                    INT CopyH = RenderMipClampSize( Info.VClamp, m, MipH );
                    D3DLOCKED_RECT lr;
                    HRESULT hrLock = Entry->pTexture->LockRect( m - FirstMip, &lr, NULL, 0 );
                    if( SUCCEEDED(hrLock) )
                    {
                        for( INT y0 = 0; y0 < DestH; y0 += RowsPerChunk )
                        {
                            INT ChunkH = Min( RowsPerChunk, DestH - y0 );
                            BYTE* ScratchP8 = GRD_UploadChunk;
                            for( INT y = 0; y < ChunkH; y++ )
                            {
                                INT dy = y0 + y;
                                for( INT x = 0; x < DestW; x++ )
                                {
                                    INT sy = bSwapUV
                                        ? Min( (x * CopyH) / DestW, CopyH - 1 )
                                        : Min( (dy * CopyH) / DestH, CopyH - 1 );
                                    INT sx = bSwapUV
                                        ? Min( (dy * CopyW) / DestH, CopyW - 1 )
                                        : Min( (x * CopyW) / DestW, CopyW - 1 );
                                    ScratchP8[y * DestW + x] = Src[sy * MipW + sx];
                                }
                            }

                            RECT  srcRect = { 0, 0, DestW, ChunkH };
                            POINT dstPoint = { 0, y0 };
                            XGSwizzleRect(
                                ScratchP8,
                                DestW,
                                &srcRect,
                                lr.pBits,
                                DestW,
                                DestH,
                                &dstPoint,
                                1
                            );
                        }
                        Entry->pTexture->UnlockRect( m - FirstMip );
                    }
                    else
                    {
                        GXboxLog.Write( "RTEX P8 LockRect FAILED frame=%d stage=%d id=%08X:%08X mip=%d hr=0x%08X",
                            FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, m - FirstMip, (DWORD)hrLock );
                    }
                }
            }
        }
        else
        {
            // RGBA7/RGBA8: convert to A8R8G8B8.
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
                RenderBlockAndReleasePalette( Entry->pPalette );
                if( bHotUpload && GVerboseRenderPerfLog )
                    GXboxLog.Write( "RTEX precreate f=%d seq=%d createNext=%d stage=%d id=%08X:%08X fmt=%d->A8R8G8B8 size=%dx%d mips=%d bytes=%d pool=%d liveKB=%d availKB=%u",
                        FrameCounter, UploadSeq, GRD_TotalTexCreates + 1, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                        Info.Format, USize, VSize, NumMips, ApproxBytes, TexPoolNext, TexLiveBytes / 1024, (unsigned)RenderAvailPhysKB() );
                hrCreate = Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Entry->pTexture );
                if( SUCCEEDED(hrCreate) && Entry->pTexture )
                {
                    Entry->USize    = USize;
                    Entry->VSize    = VSize;
                    Entry->NumMips  = NumMips;
                    Entry->FirstMip = FirstMip;
                    Entry->UIndex   = bSwapUV ? 1 : 0;
                    Entry->VIndex   = bSwapUV ? 0 : 1;
                    Entry->Format   = DestFormat;
                    Entry->MaskedAlpha = bNeedsMaskedAlpha;
                    TexLiveBytes   -= Entry->Bytes;
                    Entry->Bytes    = ApproxBytes;
                    TexLiveBytes   += Entry->Bytes;
                    GRD_FrameTexCreates++;
                    GRD_TotalTexCreates++;
                    Entry->CreateFailedFrame = -1000000;
                    Entry->CreateFailedBytes = 0;
                }
            }
            GRD_FrameTexUploads++;
            GRD_TotalTexUploads++;
            GRD_TotalTexBytes += ApproxBytes;
            if( FAILED(hrCreate) || !Entry->pTexture || (GVerboseRenderPerfLog && bNeedCreate && (GRD_TotalTexCreates <= 16 || (GRD_TotalTexCreates % 64) == 0)) )
            {
                if( bNeedCreate && (FAILED(hrCreate) || !Entry->pTexture) )
                {
                    Entry->CreateFailedFrame = FrameCounter;
                    Entry->CreateFailedBytes = ApproxBytes;
                }
                GXboxLog.Write( "RTEX %s create#=%d upload#=%d frame=%d stage=%d id=%08X:%08X fmt=%d->A8R8G8B8 size=%dx%d mips=%d bytes=%d hr=0x%08X tex=0x%08X d3dpool=DEFAULT cache=%d realtime=%d",
                    bNeedCreate ? "create" : "update", GRD_TotalTexCreates, GRD_TotalTexUploads, FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo,
                    Info.Format, USize, VSize, NumMips, ApproxBytes, (DWORD)hrCreate, (DWORD)Entry->pTexture, TexPoolNext, bRealtimeChanged || bForceRgba7MaxUpload );
            }
            if( Entry->pTexture )
            {
                for( INT m = FirstMip; m < Info.NumMips; m++ )
                {
                    if( !Info.Mips[m] || !Info.Mips[m]->DataPtr )
                        continue;

                    INT MipW = Info.Mips[m]->USize;
                    INT MipH = Info.Mips[m]->VSize;
                    INT DestW = Max( 1, (bSwapUV ? MipH : MipW) >> SoftwareMipBias );
                    INT DestH = Max( 1, (bSwapUV ? MipW : MipH) >> SoftwareMipBias );
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
                            FrameCounter, UploadSeq, m - FirstMip, Info.Format, MipW, MipH, Need, XBOX_UPLOAD_CHUNK_BYTES, (DWORD)Info.Mips[m]->DataPtr );
                    INT RowBytes = DestW * 4;
                    INT RowsPerChunk = RowBytes > 0 ? (XBOX_UPLOAD_CHUNK_BYTES / RowBytes) : 0;
                    if( RowsPerChunk < 1 )
                    {
                        GRD_FrameTexSkipped++;
                        GRD_TotalTexSkipped++;
                        GXboxLog.Write( "RTEXUP row-too-wide f=%d seq=%d mip=%d rowBytes=%d chunk=%d",
                            FrameCounter, UploadSeq, m - FirstMip, RowBytes, XBOX_UPLOAD_CHUNK_BYTES );
                        continue;
                    }
                    INT CopyW = RenderMipClampSize( Info.UClamp, m, MipW );
                    INT CopyH = RenderMipClampSize( Info.VClamp, m, MipH );
                    if( GVerboseRenderPerfLog && (CopyW != MipW || CopyH != MipH) && GRD_ClampPadLogCount < 32 )
                    {
                        GRD_ClampPadLogCount++;
                        GXboxLog.Write( "RTEX clamp-pad #%d f=%d seq=%d stage=%d fmt=%d mip=%d tex=%dx%d clamp=%dx%d dest=%dx%d",
                            GRD_ClampPadLogCount, FrameCounter, UploadSeq, Stage, Info.Format, m - FirstMip,
                            MipW, MipH, CopyW, CopyH, DestW, DestH );
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
                        for( INT y0 = 0; y0 < DestH; y0 += RowsPerChunk )
                        {
                            INT ChunkH = Min( RowsPerChunk, DestH - y0 );
                            DWORD* Dst = (DWORD*)GRD_UploadChunk;

                            if( Info.Format == TEXF_P8 )
                            {
                                BYTE* Src = (BYTE*)Info.Mips[m]->DataPtr;
                                FColor* Pal = Info.Palette;
                                for( INT y = 0; y < ChunkH; y++ )
                                {
                                    INT dy = y0 + y;
                                    for( INT x = 0; x < DestW; x++ )
                                    {
                                        INT sy = bSwapUV
                                            ? Min( (x * CopyH) / DestW, CopyH - 1 )
                                            : Min( (dy * CopyH) / DestH, CopyH - 1 );
                                        INT sx = bSwapUV
                                            ? Min( (dy * CopyW) / DestH, CopyW - 1 )
                                            : Min( (x * CopyW) / DestW, CopyW - 1 );
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
                                INT SrcStride = MipW;
                                for( INT y = 0; y < ChunkH; y++ )
                                {
                                    INT dy = y0 + y;
                                    for( INT x = 0; x < DestW; x++ )
                                    {
                                        INT sy = bSwapUV
                                            ? Min( (x * CopyH) / DestW, CopyH - 1 )
                                            : Min( (dy * CopyH) / DestH, CopyH - 1 );
                                        INT sx = bSwapUV
                                            ? Min( (dy * CopyW) / DestH, CopyW - 1 )
                                            : Min( (x * CopyW) / DestW, CopyW - 1 );
                                        DWORD* SrcRow = Src + sy * SrcStride;
                                        Dst[y * DestW + x] = SrcRow[sx] * 2;
                                    }
                                }
                            }
                            else
                            {
                                FColor* Src = (FColor*)Info.Mips[m]->DataPtr;
                                for( INT y = 0; y < ChunkH; y++ )
                                {
                                    INT dy = y0 + y;
                                    for( INT x = 0; x < DestW; x++ )
                                    {
                                        INT sy = bSwapUV
                                            ? Min( (x * CopyH) / DestW, CopyH - 1 )
                                            : Min( (dy * CopyH) / DestH, CopyH - 1 );
                                        INT sx = bSwapUV
                                            ? Min( (dy * CopyW) / DestH, CopyW - 1 )
                                            : Min( (x * CopyW) / DestW, CopyW - 1 );
                                        FColor& C = Src[sy * MipW + sx];
                                        Dst[y * DestW + x] = D3DCOLOR_ARGB( C.A, C.R, C.G, C.B );
                                    }
                                }
                            }

                            RECT  srcRect = { 0, 0, DestW, ChunkH };
                            POINT dstPoint = { 0, y0 };
                            if( bHotUpload && RenderHotTrace() )
                                GXboxLog.Write( "RTEXUP swizzle-begin f=%d seq=%d mip=%d src=0x%08X dst=0x%08X size=%dx%d y=%d h=%d",
                                    FrameCounter, UploadSeq, m - FirstMip, (DWORD)Dst, (DWORD)lr.pBits, DestW, DestH, y0, ChunkH );
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
                        }
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
        if( bUnloadSourceAfterUpload )
        {
            Info.Unload();
            DWORD UnloadAvailKB = RenderAvailPhysKB();
            GRD_SourceUnloadLogCount++;
            if( GRD_SourceUnloadLogCount <= 24 || ( UnloadAvailKB < 8192 && ( GRD_SourceUnloadLogCount & 0xFF ) == 0 ) )
            {
                GXboxLog.Write( "RTEX source-unload #%d f=%d stage=%d id=%08X:%08X fmt=%d mips=%d availKB=%u",
                    GRD_SourceUnloadLogCount, FrameCounter, Stage,
                    GRD_LastTextureIDHi, GRD_LastTextureIDLo, Info.Format, Info.NumMips,
                    (unsigned)UnloadAvailKB );
            }
        }
        ResumeSceneAfterTextureUpload( "tex-upload" );
    }

    if( !Entry || !Entry->pTexture )
    {
        Device->SetTexture( Stage, NULL );
        BoundCacheID[Stage] = 0;
        StageUScale[Stage] = Entry ? Entry->UScale : 1.0f;
        StageVScale[Stage] = Entry ? Entry->VScale : 1.0f;
        StageUIndex[Stage] = Entry ? Entry->UIndex : 0;
        StageVIndex[Stage] = Entry ? Entry->VIndex : 1;
        return 0;
    }

    // Store stage state.
    if( GRD_MenuTextMode )
        Entry->Pinned = 1;
    BoundCacheID[Stage] = Info.CacheID;
    StageUScale[Stage]  = Entry->UScale;
    StageVScale[Stage]  = Entry->VScale;
    StageUIndex[Stage]  = Entry->UIndex;
    StageVIndex[Stage]  = Entry->VIndex;
    Entry->FrameCounter = FrameCounter;

    // Bind the D3D texture.
    if( Entry->pPalette )
        Device->SetPalette( Stage, Entry->pPalette );
    HRESULT hrSet = Device->SetTexture( Stage, Entry->pTexture );
    UBOOL bSetOk = SUCCEEDED(hrSet) && Entry->pTexture;
    GRD_FrameTexBinds++;
    if( FAILED(hrSet) || !Entry->pTexture )
        GXboxLog.Write( "RTEX SetTexture frame=%d stage=%d id=%08X:%08X tex=0x%08X hr=0x%08X",
            FrameCounter, Stage, GRD_LastTextureIDHi, GRD_LastTextureIDLo, (DWORD)Entry->pTexture, (DWORD)hrSet );

    return bSetOk;
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
        UBOOL bBaseTextureOk = SetTextureD3D( 0, *Surface.Texture, Surface.PolyFlags );
        UBOOL bLightMapOk = SetTextureD3D( 1, *Surface.LightMap, 0 );
        if( !bBaseTextureOk )
        {
            DisableStage1();
            return;
        }

        // Diagnosis only: bBaseTextureOk is sampled before the lightmap bind,
        // so it can be stale by the time we draw. If stage 0 no longer holds
        // the base texture here, this surface is about to be drawn untextured.
        // Deliberately does not correct the binding - the point of this pass is
        // to find out whether that ever actually happens in a flickering
        // session. See Docs/OPEN_ITEMS.md item 1.
        if( BoundCacheID[0] != Surface.Texture->CacheID )
        {
            GRD_TotalNoBaseDraw++;
            if( GRD_NoBaseLogCount < 64 || (GRD_TotalNoBaseDraw % 128) == 0 )
            {
                GRD_NoBaseLogCount++;
                GXboxLog.Write( "RTEX nobase #%d f=%d dcs=%d want=%08X:%08X bound0=%08X:%08X light=%08X:%08X lightOk=%d reuse=%d clobber=%d pool=%d",
                    GRD_TotalNoBaseDraw, FrameCounter, GRD_FrameDCS,
                    (DWORD)(Surface.Texture->CacheID >> 32), (DWORD)Surface.Texture->CacheID,
                    (DWORD)(BoundCacheID[0] >> 32), (DWORD)BoundCacheID[0],
                    (DWORD)(Surface.LightMap->CacheID >> 32), (DWORD)Surface.LightMap->CacheID,
                    (INT)bLightMapOk, GRD_TotalTexReuse, GRD_TotalStage0Clobber, TexPoolNext );
            }
        }

        // XBOXRENDER_ADDRTRACE: read the real sampler address mode back from
        // D3D on the first BSP surface of every 60th frame. 1 = WRAP, 3 = CLAMP.
        // This is the direct proof of the calibration state leak; screenshots
        // cannot settle it because the camera is not deterministic.
        if( (FrameCounter % 60) == 0 && GRD_FrameDCS == 1 )
        {
            DWORD AddrU0 = 0, AddrV0 = 0, AddrU1 = 0;
            Device->GetTextureStageState( 0, D3DTSS_ADDRESSU, &AddrU0 );
            Device->GetTextureStageState( 0, D3DTSS_ADDRESSV, &AddrV0 );
            Device->GetTextureStageState( 1, D3DTSS_ADDRESSU, &AddrU1 );
            GXboxLog.Write( "ADDRTRACE f=%d s0u=%u s0v=%u s1u=%u (1=WRAP 3=CLAMP) bright=%.2f",
                FrameCounter, (unsigned)AddrU0, (unsigned)AddrV0, (unsigned)AddrU1,
                GRD_DisplayBrightness );
        }

        // Make the lightmap stage deterministic. 2D HUD/flash/overlay draws
        // also use the fixed-function stages, so never rely only on
        // CurrentPolyFlags to decide whether stage 1 is already correct.
        SetCachedTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        SetCachedTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
        if( bLightMapOk )
        {
            SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_MODULATE );
            SetCachedTextureStageState( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
            SetCachedTextureStageState( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
            SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 );
            SetCachedTextureStageState( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
            SetCachedTextureStageState( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
            SetCachedTextureStageState( 1, D3DTSS_TEXCOORDINDEX, 1 );
            SetCachedTextureStageState( 1, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
            SetCachedTextureStageState( 1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
        }
        else
        {
            Device->SetTexture( 1, NULL );
            BoundCacheID[1] = 0;
            SetCachedTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
            SetCachedTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
        }

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
        if( !SetTextureD3D( 0, *Surface.Texture, Surface.PolyFlags ) )
            return;
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
            if( SetTextureD3D( 0, *Surface.MacroTexture, 0 ) )
            {
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
        }

        // Pass 3: Lightmap (modulated overlay).
        if( Surface.LightMap )
        {
            SetBlending( PF_Modulated );
            if( SetTextureD3D( 0, *Surface.LightMap, 0 ) )
            {
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
    }

    // Fog map pass.
    if( Surface.FogMap )
    {
        SetBlending( PF_Highlighted );
        if( SetTextureD3D( 0, *Surface.FogMap, 0 ) )
        {
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
        if( !SetTextureD3D( 0, Info, PolyFlags ) )
            return;
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
        if( !SetTextureD3D( 0, Info, PolyFlags ) )
            return;
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

    if( GRD_MenuTextMode )
        PolyFlags = (PolyFlags | PF_Masked | PF_NoSmooth) & ~(PF_Translucent | PF_Modulated);

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
        if( !SetTextureD3D( 0, Info, PolyFlags ) )
            return;
        SetCachedVertexShader( XBOX_FVF_TLVERTEX );
        GRD_DTBatchActive = 1;
        GRD_DTBatchCacheID = Info.CacheID;
        GRD_DTBatchPolyFlags = PolyFlags;
    }
    else if( !bCanBatch )
    {
        FlushDTBatch( "DT-fallback" );
        SetBlending( PolyFlags );
        if( !SetTextureD3D( 0, Info, PolyFlags ) )
            return;
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

    XboxRenderFlushMenuRectBatch( Ren, "menu-text-pre" );
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

    XboxRenderFlushMenuRectBatch( Ren, "menu-text-post" );
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

static void XboxRenderFlushMenuRectBatch( UXboxRenderDevice* Ren, const char* Reason )
{
    guard(XboxRenderFlushMenuRectBatch);

    if( !GRD_MenuRectBatchActive || GRD_MenuRectBatchVerts <= 0 )
    {
        GRD_MenuRectBatchActive = 0;
        GRD_MenuRectBatchVerts = 0;
        return;
    }

    if( Ren && Ren->Device )
    {
        Ren->FlushDGPBatch( Reason ? Reason : "menu-rect" );
        Ren->FlushDTBatch( Reason ? Reason : "menu-rect" );
        Ren->DisableStage1();

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
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        Ren->SetCachedRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
        Ren->SetCachedRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
        Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );

        Ren->DrawPrimitiveVB( D3DPT_TRIANGLELIST, GRD_MenuRectBatchVerts / 3, GRD_MenuRectBatch, sizeof(FXboxTLVertex), "menu-rect-batch" );

        Ren->RestoreDefaultTextureStages();
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
        Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
        Ren->SetCachedRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
        Ren->CurrentPolyFlags = 0xFFFFFFFF;
    }

    GRD_MenuRectBatchActive = 0;
    GRD_MenuRectBatchVerts = 0;

    unguard;
}

extern "C" void XboxRenderDrawMenuRect( FSceneNode* Frame, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, BYTE R, BYTE G, BYTE B, BYTE A )
{
    guard(XboxRenderDrawMenuRect);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame )
        return;

    // Menu coordinates are canvas-local, just like UCanvas text coordinates.
    X1 += Frame->XB;
    X2 += Frame->XB;
    Y1 += Frame->YB;
    Y2 += Frame->YB;

    if( GRD_MenuRectBatchVerts + 6 > XBOX_MENU_RECT_BATCH_VERTS )
        XboxRenderFlushMenuRectBatch( Ren, "menu-rect-full" );

    DWORD Clr = ((DWORD)A << 24) | ((DWORD)R << 16) | ((DWORD)G << 8) | (DWORD)B;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;

    FXboxTLVertex Verts[4];
    Verts[0].x = X1 - 0.5f; Verts[0].y = Y1 - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = X2 - 0.5f; Verts[1].y = Y1 - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;
    Verts[2].x = X2 - 0.5f; Verts[2].y = Y2 - 0.5f; Verts[2].rhw = RHW; Verts[2].z = SZ; Verts[2].color = Clr; Verts[2].u = 0; Verts[2].v = 0;
    Verts[3].x = X1 - 0.5f; Verts[3].y = Y2 - 0.5f; Verts[3].rhw = RHW; Verts[3].z = SZ; Verts[3].color = Clr; Verts[3].u = 0; Verts[3].v = 0;

    GRD_MenuRectBatch[GRD_MenuRectBatchVerts++] = Verts[0];
    GRD_MenuRectBatch[GRD_MenuRectBatchVerts++] = Verts[1];
    GRD_MenuRectBatch[GRD_MenuRectBatchVerts++] = Verts[2];
    GRD_MenuRectBatch[GRD_MenuRectBatchVerts++] = Verts[0];
    GRD_MenuRectBatch[GRD_MenuRectBatchVerts++] = Verts[2];
    GRD_MenuRectBatch[GRD_MenuRectBatchVerts++] = Verts[3];
    GRD_MenuRectBatchActive = 1;

    unguard;
}

extern "C" void XboxRenderDrawMenuRingSlice( FSceneNode* Frame, FLOAT CX, FLOAT CY, FLOAT InnerR, FLOAT OuterR, FLOAT StartAngle, FLOAT EndAngle, BYTE R, BYTE G, BYTE B, BYTE A )
{
    guard(XboxRenderDrawMenuRingSlice);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame || OuterR <= InnerR || EndAngle <= StartAngle )
        return;

    CX += Frame->XB;
    CY += Frame->YB;

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

    XboxRenderFlushMenuRectBatch( Ren, "menu-mesh-slot-begin" );
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

    XboxRenderFlushMenuRectBatch( Ren, "menu-mesh-slot-end" );
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
    INT LastUsedFrame;
};

static FXboxMenuTexture GXboxMenuTextures[32];
static char GXboxMenuTextureFailures[16][64];
static INT GXboxMenuTextureAccessCounter = 1;

extern "C" void XboxRenderGetMenuTextureStats( INT* OutCount, INT* OutApproxKB, INT* OutFailures )
{
    INT Count = 0;
    INT ApproxKB = 0;
    INT Failures = 0;
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
    {
        if( GXboxMenuTextures[i].Texture )
        {
            Count++;
            ApproxKB += (INT)((GXboxMenuTextures[i].Width * GXboxMenuTextures[i].Height * 4) / 1024);
        }
    }
    for( INT j=0; j<ARRAY_COUNT(GXboxMenuTextureFailures); j++ )
        if( GXboxMenuTextureFailures[j][0] )
            Failures++;

    if( OutCount )
        *OutCount = Count;
    if( OutApproxKB )
        *OutApproxKB = ApproxKB;
    if( OutFailures )
        *OutFailures = Failures;
}

extern "C" void XboxRenderReleaseMenuTexture( const char* Name )
{
    if( !Name || !Name[0] )
        return;

    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
    {
        if( GXboxMenuTextures[i].Texture && appStricmp(GXboxMenuTextures[i].Name, Name)==0 )
        {
            INT ReleasedKB = (INT)((GXboxMenuTextures[i].Width * GXboxMenuTextures[i].Height * 4) / 1024);
            RenderBlockAndReleaseTexture( GXboxMenuTextures[i].Texture );
            appMemzero( &GXboxMenuTextures[i], sizeof(GXboxMenuTextures[i]) );
            GXboxLog.Write( "XMENU render texture released %s approxKB=%d", Name, ReleasedKB );
            return;
        }
    }
}

extern "C" void XboxRenderReleaseMenuTextures()
{
    INT Released = 0;
    INT ReleasedKB = 0;
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
    {
        if( GXboxMenuTextures[i].Texture )
        {
            Released++;
            ReleasedKB += (INT)((GXboxMenuTextures[i].Width * GXboxMenuTextures[i].Height * 4) / 1024);
            RenderBlockAndReleaseTexture( GXboxMenuTextures[i].Texture );
        }
        appMemzero( &GXboxMenuTextures[i], sizeof(GXboxMenuTextures[i]) );
    }
    appMemzero( GXboxMenuTextureFailures, sizeof(GXboxMenuTextureFailures) );
    if( Released )
        GXboxLog.Write( "XMENU render textures released count=%d approxKB=%d", Released, ReleasedKB );
}

static UBOOL XboxMenuTextureFailedBefore( const char* Name )
{
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextureFailures); i++ )
        if( GXboxMenuTextureFailures[i][0] && appStricmp(GXboxMenuTextureFailures[i], Name)==0 )
            return 1;
    return 0;
}

static void XboxRememberMenuTextureFailure( const char* Name )
{
    if( !Name || XboxMenuTextureFailedBefore( Name ) )
        return;

    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextureFailures); i++ )
    {
        if( !GXboxMenuTextureFailures[i][0] )
        {
            appStrncpy( GXboxMenuTextureFailures[i], Name, ARRAY_COUNT(GXboxMenuTextureFailures[i]) );
            GXboxMenuTextureFailures[i][ARRAY_COUNT(GXboxMenuTextureFailures[i])-1] = 0;
            return;
        }
    }
}

static FXboxMenuTexture* XboxFindMenuTexture( const char* Name )
{
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
    {
        if( GXboxMenuTextures[i].Texture && appStricmp(GXboxMenuTextures[i].Name, Name)==0 )
        {
            GXboxMenuTextures[i].LastUsedFrame = GXboxMenuTextureAccessCounter++;
            return &GXboxMenuTextures[i];
        }
    }
    return NULL;
}

static FXboxMenuTexture* XboxEvictMenuTextureLRU( const char* NewName )
{
    INT BestIndex = -1;
    INT BestFrame = 0x7fffffff;
    for( INT i=0; i<ARRAY_COUNT(GXboxMenuTextures); i++ )
    {
        if( GXboxMenuTextures[i].Texture && GXboxMenuTextures[i].LastUsedFrame < BestFrame )
        {
            BestFrame = GXboxMenuTextures[i].LastUsedFrame;
            BestIndex = i;
        }
    }

    if( BestIndex < 0 )
        return NULL;

    INT ReleasedKB = (INT)((GXboxMenuTextures[BestIndex].Width * GXboxMenuTextures[BestIndex].Height * 4) / 1024);
    GXboxLog.Write( "XMENU tex LRU evict old=%s new=%s age=%d approxKB=%d",
        GXboxMenuTextures[BestIndex].Name,
        NewName ? NewName : "",
        GXboxMenuTextureAccessCounter - GXboxMenuTextures[BestIndex].LastUsedFrame,
        ReleasedKB );
    RenderBlockAndReleaseTexture( GXboxMenuTextures[BestIndex].Texture );
    appMemzero( &GXboxMenuTextures[BestIndex], sizeof(GXboxMenuTextures[BestIndex]) );
    return &GXboxMenuTextures[BestIndex];
}

static FXboxMenuTexture* XboxLoadMenuTexture( UXboxRenderDevice* Ren, const char* Name )
{
    if( !Ren || !Ren->Device || !Name )
        return NULL;

    FXboxMenuTexture* Existing = XboxFindMenuTexture( Name );
    if( Existing )
        return Existing;
    if( XboxMenuTextureFailedBefore( Name ) )
        return NULL;

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
        Slot = XboxEvictMenuTextureLRU( Name );
        if( !Slot )
        {
            GXboxLog.Write( "XMENU tex cache full loading %s slots=%d", Name, ARRAY_COUNT(GXboxMenuTextures) );
            return NULL;
        }
    }

    char Path[256];
    appSprintf( Path, "D:\\MenuAssets\\%s", Name );
    HANDLE File = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
    {
        GXboxLog.Write( "XMENU tex missing %s err=%lu", Path, GetLastError() );
        XboxRememberMenuTextureFailure( Name );
        return NULL;
    }

    DWORD Header[3] = {0,0,0};
    DWORD Read = 0;
    if( !ReadFile( File, Header, sizeof(Header), &Read, NULL ) || Read != sizeof(Header) || Header[0] != 0x30495558 )
    {
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex bad header %s", Path );
        XboxRememberMenuTextureFailure( Name );
        return NULL;
    }

    DWORD Width = Header[1];
    DWORD Height = Header[2];
    if( Width < 1 || Height < 1 || Width > 1024 || Height > 1024 )
    {
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex bad size %s %lux%lu", Path, Width, Height );
        XboxRememberMenuTextureFailure( Name );
        return NULL;
    }

    DWORD TexWidth = Width;
    DWORD TexHeight = Height;

    IDirect3DTexture8* Texture = NULL;
    HRESULT hr = Ren->Device->CreateTexture( TexWidth, TexHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture );
    if( FAILED(hr) || !Texture )
    {
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex create failed %s %lux%lu hr=0x%08X", Path, TexWidth, TexHeight, (DWORD)hr );
        XboxRememberMenuTextureFailure( Name );
        return NULL;
    }

    D3DLOCKED_RECT Locked;
    hr = Texture->LockRect( 0, &Locked, NULL, 0 );
    if( FAILED(hr) )
    {
        Texture->Release();
        CloseHandle( File );
        GXboxLog.Write( "XMENU tex lock failed %s hr=0x%08X", Path, (DWORD)hr );
        XboxRememberMenuTextureFailure( Name );
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
        RECT SrcRect = { 0, 0, (LONG)TexWidth, (LONG)TexHeight };
        POINT DstPoint = { 0, 0 };
        if( Ok )
        {
            XGSwizzleRect(
                Linear,
                RowBytes,
                &SrcRect,
                Locked.pBits,
                TexWidth,
                TexHeight,
                &DstPoint,
                4
            );
        }
    }
    if( Linear )
        appFree( Linear );
    Texture->UnlockRect( 0 );
    CloseHandle( File );

    if( !Ok )
    {
        Texture->Release();
        GXboxLog.Write( "XMENU tex short read %s", Path );
        XboxRememberMenuTextureFailure( Name );
        return NULL;
    }

    appStrncpy( Slot->Name, Name, ARRAY_COUNT(Slot->Name) );
    Slot->Name[ARRAY_COUNT(Slot->Name)-1] = 0;
    Slot->Texture = Texture;
    Slot->Width = TexWidth;
    Slot->Height = TexHeight;
    Slot->LastUsedFrame = GXboxMenuTextureAccessCounter++;
    GXboxLog.Write( "XMENU tex loaded %s %lux%lu", Path, TexWidth, TexHeight );
    return Slot;
}

extern "C" UBOOL XboxRenderDrawMenuTexture( FSceneNode* Frame, const char* Name, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha )
{
    guard(XboxRenderDrawMenuTexture);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame || !Name )
        return 0;

    X += Frame->XB;
    Y += Frame->YB;

    FXboxMenuTexture* Tex = XboxLoadMenuTexture( Ren, Name );
    if( !Tex || !Tex->Texture )
        return 0;

    XboxRenderFlushMenuRectBatch( Ren, "menu-tex" );
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

extern "C" UBOOL XboxRenderDrawMenuUTexture( FSceneNode* Frame, UTexture* Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Alpha )
{
    UBOOL bDrawn = 0;
    guard(XboxRenderDrawMenuUTexture);

    UXboxRenderDevice* Ren = Cast<UXboxRenderDevice>( GRenderDevice );
    if( !Ren || !Ren->Device || !Frame || !Texture )
        return 0;

    X += Frame->XB;
    Y += Frame->YB;

    DOUBLE Time = (Frame->Viewport) ? Frame->Viewport->CurrentTime : 0.0;
    UTexture* DrawTexture = GIsEditor ? Texture : Texture->Get( Time );
    if( !DrawTexture )
        return 0;

    FTextureInfo Info;
    DrawTexture->Lock( Info, Time, -1, Ren );

    Ren->FlushDGPBatch( "menu-utexture" );
    Ren->FlushDTBatch( "menu-utexture" );
    XboxRenderFlushMenuRectBatch( Ren, "menu-utexture" );
    Ren->DisableStage1();

    if( Ren->SetTextureD3D( 0, Info, 0 ) )
    {
        BYTE A = (BYTE)Clamp<INT>( (INT)(Alpha * 255.0f), 0, 255 );
        DWORD Clr = (A << 24) | 0x00FFFFFF;
        FLOAT RHW = 1.0f;
        FLOAT SZ  = Ren->ProjZRatio + Ren->ProjZOffset * RHW;
        FLOAT UL = Info.UScale * (FLOAT)Info.USize;
        FLOAT VL = Info.VScale * (FLOAT)Info.VSize;

        FXboxTLVertex Verts[4];
        Verts[0].x = X - 0.5f;      Verts[0].y = Y - 0.5f;      Verts[0].z = SZ; Verts[0].rhw = RHW; Verts[0].color = Clr; SetUV( Verts[0], 0, 0.0f, 0.0f, Ren->StageUScale, Ren->StageVScale, Ren->StageUIndex, Ren->StageVIndex );
        Verts[1].x = X+XL - 0.5f;   Verts[1].y = Y - 0.5f;      Verts[1].z = SZ; Verts[1].rhw = RHW; Verts[1].color = Clr; SetUV( Verts[1], 0, UL,   0.0f, Ren->StageUScale, Ren->StageVScale, Ren->StageUIndex, Ren->StageVIndex );
        Verts[2].x = X+XL - 0.5f;   Verts[2].y = Y+YL - 0.5f;   Verts[2].z = SZ; Verts[2].rhw = RHW; Verts[2].color = Clr; SetUV( Verts[2], 0, UL,   VL,   Ren->StageUScale, Ren->StageVScale, Ren->StageUIndex, Ren->StageVIndex );
        Verts[3].x = X - 0.5f;      Verts[3].y = Y+YL - 0.5f;   Verts[3].z = SZ; Verts[3].rhw = RHW; Verts[3].color = Clr; SetUV( Verts[3], 0, 0.0f, VL,   Ren->StageUScale, Ren->StageVScale, Ren->StageUIndex, Ren->StageVIndex );

        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, A < 255 );
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
        Ren->SetCachedVertexShader( XBOX_FVF_TLVERTEX );
        Ren->DrawPrimitiveVB( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex), "menu-utexture" );
        Ren->Device->SetTexture( 0, NULL );
        Ren->BoundCacheID[0] = 0;
        Ren->RestoreDefaultTextureStages();
        Ren->SetCachedRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
        Ren->SetCachedRenderState( D3DRS_ALPHATESTENABLE, FALSE );
        Ren->CurrentPolyFlags = 0xFFFFFFFF;
        bDrawn = 1;
    }

    DrawTexture->Unlock( Info );
    unguard;
    return bDrawn;
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
