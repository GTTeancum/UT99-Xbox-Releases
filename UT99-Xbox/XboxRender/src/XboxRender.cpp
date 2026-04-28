// XboxRender.cpp
// Xbox D3D8 render device implementation.
// Ported from the D3D7 reference renderer (D3DDrv/Src/Direct3D7.cpp).

#include "XboxRender.h"

IMPLEMENT_CLASS(UXboxRenderDevice);
IMPLEMENT_PACKAGE(XboxRender);

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

// ============================================================================
// StaticConstructor
// ============================================================================
void UXboxRenderDevice::StaticConstructor()
{
    guard(UXboxRenderDevice::StaticConstructor);

    SpanBased            = 0;
    FullscreenOnly       = 1;
    SupportsFogMaps      = 1;
    SupportsDistanceFog  = 1;
    VolumetricLighting   = 1;
    ShinySurfaces        = 1;
    Coronas              = 1;
    HighDetailActors     = 1;
    SupportsTC           = 1;
    PrecacheOnFlip       = 0;
    SupportsLazyTextures = 0;
    PrefersDeferredLoad  = 0;
    DetailTextures       = 1;

    unguard;
}

// ============================================================================
// Init
// ============================================================================
UBOOL UXboxRenderDevice::Init( UViewport* InViewport, INT NewX, INT NewY, INT NewColorBytes, UBOOL Fullscreen )
{
    guard(UXboxRenderDevice::Init);

    GXboxLog.Write( "XboxRender::Init: entered (%dx%d, %dbpp)", NewX, NewY, NewColorBytes );

    Viewport      = InViewport;
    Direct3D      = NULL;
    Device        = NULL;
    BackBuffer    = NULL;
    DepthBuffer   = NULL;
    DeviceCreated = 0;
    CurrentFrame  = NULL;
    CurrentPolyFlags = 0;
    FrameCounter  = 0;
    TexPoolNext   = 0;
    appMemzero( TexCache, sizeof(TexCache) );
    appMemzero( TexPool,  sizeof(TexPool) );
    BoundCacheID[0] = 0;
    BoundCacheID[1] = 0;

    // Z-buffer formula: SZ = ProjZRatio + ProjZOffset * RHW
    zNear      = 1.f;
    zFar       = 32767.f;
    ProjZRatio  = zFar / (zFar - zNear);
    ProjZOffset = -ProjZRatio * zNear;

    // Create Direct3D
    GXboxLog.Write( "XboxRender::Init: calling Direct3DCreate8()" );
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
    PP.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    // Create device
    GXboxLog.Write( "XboxRender::Init: calling CreateDevice()" );
    HRESULT hr = Direct3D->CreateDevice(
        0,
        D3DDEVTYPE_HAL,
        NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &PP,
        &Device
    );
    if( FAILED(hr) )
    {
        GXboxLog.Write( "XboxRender::Init: CreateDevice FAILED (hr=0x%08X)", hr );
        debugf( NAME_Init, TEXT("XboxRender: CreateDevice failed (0x%08X)"), hr );
        Direct3D->Release();
        Direct3D = NULL;
        return 0;
    }
    GXboxLog.Write( "XboxRender::Init: CreateDevice OK (ptr=0x%08X)", (DWORD)Device );

    DeviceCreated = 1;

    // Get back buffer and depth buffer
    Device->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &BackBuffer );
    Device->GetDepthStencilSurface( &DepthBuffer );
    GXboxLog.Write( "XboxRender::Init: BackBuffer=0x%08X, DepthBuffer=0x%08X",
        (DWORD)BackBuffer, (DWORD)DepthBuffer );

    // Set initial render states
    Device->SetRenderState( D3DRS_ZENABLE, D3DZB_TRUE );
    Device->SetRenderState( D3DRS_ZWRITEENABLE, TRUE );
    Device->SetRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
    Device->SetRenderState( D3DRS_LIGHTING, FALSE );
    Device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
    Device->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
    Device->SetRenderState( D3DRS_DITHERENABLE, TRUE );
    Device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
    Device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
    Device->SetRenderState( D3DRS_ALPHAREF, 127 );
    Device->SetRenderState( D3DRS_ALPHAFUNC, D3DCMP_GREATER );

    // Default texture stage state
    Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
    Device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
    Device->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );
    Device->SetTextureStageState( 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
    Device->SetTextureStageState( 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );
    Device->SetTextureStageState( 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR );
    Device->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
    Device->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

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

    if( DepthBuffer )  { DepthBuffer->Release();  DepthBuffer  = NULL; }
    if( BackBuffer )   { BackBuffer->Release();    BackBuffer   = NULL; }
    if( Device )       { Device->Release();        Device       = NULL; }
    if( Direct3D )     { Direct3D->Release();      Direct3D     = NULL; }
    DeviceCreated = 0;

    GXboxLog.Write( "XboxRender::Exit: done" );
    debugf( NAME_Init, TEXT("XboxRender: Device released") );

    unguard;
}

// ============================================================================
// Flush — release all cached textures
// ============================================================================
void UXboxRenderDevice::Flush( UBOOL AllowPrecache )
{
    guard(UXboxRenderDevice::Flush);
    FlushTexCache();
    unguard;
}

void UXboxRenderDevice::FlushTexCache()
{
    for( INT i = 0; i < XBOX_TEX_CACHE_SIZE; i++ )
    {
        FXboxTexCacheEntry* Entry = TexCache[i];
        while( Entry )
        {
            if( Entry->pTexture )
            {
                Entry->pTexture->Release();
                Entry->pTexture = NULL;
            }
            Entry = Entry->HashNext;
        }
        TexCache[i] = NULL;
    }
    TexPoolNext = 0;
    appMemzero( TexPool, sizeof(TexPool) );
    BoundCacheID[0] = 0;
    BoundCacheID[1] = 0;
    if( Device )
    {
        Device->SetTexture( 0, NULL );
        Device->SetTexture( 1, NULL );
    }
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

    if( !Device )
        return;

    FrameCounter++;
    FlashScale = InFlashScale;
    FlashFog   = InFlashFog;

    // Clear back buffer and depth buffer
    DWORD dwFlags = D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL;
    if( RenderLockFlags & LOCKR_ClearScreen )
        dwFlags |= D3DCLEAR_TARGET;

    Device->Clear( 0, NULL, dwFlags,
        FColor(ScreenClear).TrueColor(),
        1.0f, 0 );

    Device->BeginScene();

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

    Device->EndScene();

    if( Blit )
        Device->Present( NULL, NULL, NULL, NULL );

    unguard;
}

// ============================================================================
// SetSceneNode
// ============================================================================
void UXboxRenderDevice::SetSceneNode( FSceneNode* Frame )
{
    guard(UXboxRenderDevice::SetSceneNode);

    if( !Device || !Frame )
        return;

    CurrentFrame = Frame;
    RProjZ = Frame->RProj.Z;

    // Set the D3D8 viewport
    D3DVIEWPORT8 vp;
    vp.X      = Frame->XB;
    vp.Y      = Frame->YB;
    vp.Width  = Frame->X;
    vp.Height = Frame->Y;
    vp.MinZ   = 0.0f;
    vp.MaxZ   = 1.0f;
    Device->SetViewport( &vp );

    unguard;
}

// ============================================================================
// SetBlending — port of D3D7 SetBlending (lines 1315-1393)
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
                Device->SetRenderState( D3DRS_ALPHABLENDENABLE, FALSE );
            }
            else if( PolyFlags & PF_Invisible )
            {
                Device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                Device->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_ZERO );
                Device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_ONE );
            }
            else if( PolyFlags & PF_Translucent )
            {
                Device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                Device->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_ONE );
                Device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR );
            }
            else if( PolyFlags & PF_Modulated )
            {
                Device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                Device->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_DESTCOLOR );
                Device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR );
            }
            else if( PolyFlags & PF_Highlighted )
            {
                Device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
                Device->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_ONE );
                Device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
            }
        }
        if( Xor & PF_Occlude )
        {
            Device->SetRenderState( D3DRS_ZWRITEENABLE, (PolyFlags & PF_Occlude) != 0 );
        }
        if( Xor & PF_Masked )
        {
            Device->SetRenderState( D3DRS_ALPHATESTENABLE, (PolyFlags & PF_Masked) != 0 );
        }
        if( Xor & PF_NoSmooth )
        {
            Device->SetTextureStageState( 0, D3DTSS_MAGFILTER, (PolyFlags & PF_NoSmooth) ? D3DTEXF_POINT : D3DTEXF_LINEAR );
            Device->SetTextureStageState( 0, D3DTSS_MINFILTER, (PolyFlags & PF_NoSmooth) ? D3DTEXF_POINT : D3DTEXF_LINEAR );
        }
        if( Xor & PF_Memorized )
        {
            Device->SetTextureStageState( 1, D3DTSS_COLOROP, (PolyFlags & PF_Memorized) ? D3DTOP_MODULATE : D3DTOP_DISABLE );
            Device->SetTextureStageState( 1, D3DTSS_ALPHAOP, (PolyFlags & PF_Memorized) ? D3DTOP_SELECTARG2 : D3DTOP_DISABLE );
        }
        CurrentPolyFlags = PolyFlags;
    }
}

// ============================================================================
// SetTextureD3D — simplified texture cache for Xbox D3D8
// ============================================================================
void UXboxRenderDevice::SetTextureD3D( INT Stage, FTextureInfo& Info, DWORD PolyFlags )
{
    guard(UXboxRenderDevice::SetTextureD3D);

    // Early out if texture already bound.
    if( BoundCacheID[Stage] == Info.CacheID && !Info.bRealtimeChanged )
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

    if( !Entry || Info.bRealtimeChanged )
    {
        // Need to create or update the texture.
        if( !Entry )
        {
            // Allocate from pool.
            if( TexPoolNext >= XBOX_TEX_CACHE_SIZE )
            {
                // Pool exhausted — flush everything and start over.
                FlushTexCache();
                HashIndex = ((7 * (DWORD)Info.CacheID) + (DWORD)(Info.CacheID >> 32)) & (XBOX_TEX_CACHE_SIZE - 1);
            }
            Entry = &TexPool[TexPoolNext++];
            Entry->CacheID   = Info.CacheID;
            Entry->pTexture  = NULL;
            Entry->HashNext  = TexCache[HashIndex];
            TexCache[HashIndex] = Entry;
        }

        // Determine mip 0 dimensions and first mip to use.
        INT FirstMip = 0;
        INT USize = Info.Mips[0] ? Info.Mips[0]->USize : Info.USize;
        INT VSize = Info.Mips[0] ? Info.Mips[0]->VSize : Info.VSize;

        // Clamp oversized textures by skipping mips.
        while( (USize > 1024 || VSize > 1024) && FirstMip + 1 < Info.NumMips )
        {
            FirstMip++;
            USize = Info.Mips[FirstMip]->USize;
            VSize = Info.Mips[FirstMip]->VSize;
        }

        // Compute UV scale (maps UT99 texcoords to 0..1 range for D3D).
        Entry->UScale = 1.0f / (FLOAT)(USize * Max((INT)1, (INT)(1 << FirstMip)) * Info.UScale);
        Entry->VScale = 1.0f / (FLOAT)(VSize * Max((INT)1, (INT)(1 << FirstMip)) * Info.VScale);

        // Release old texture if updating.
        if( Entry->pTexture )
        {
            Entry->pTexture->Release();
            Entry->pTexture = NULL;
        }

        // Ensure mipmap data is loaded.
        Info.Load();
        Info.bRealtimeChanged = 0;

        if( Info.Format == TEXF_DXT1 )
        {
            // DXT1 compressed — pass through directly.
            INT NumMips = Info.NumMips - FirstMip;
            if( NumMips < 1 ) NumMips = 1;
            Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_DXT1, D3DPOOL_DEFAULT, &Entry->pTexture );
            if( Entry->pTexture )
            {
                for( INT m = FirstMip; m < Info.NumMips; m++ )
                {
                    if( !Info.Mips[m] || !Info.Mips[m]->DataPtr )
                        continue;
                    D3DLOCKED_RECT lr;
                    if( SUCCEEDED(Entry->pTexture->LockRect( m - FirstMip, &lr, NULL, 0 )) )
                    {
                        INT MipW = Max(1, USize >> (m - FirstMip));
                        INT MipH = Max(1, VSize >> (m - FirstMip));
                        INT BlocksW = Max(1, MipW / 4);
                        INT BlocksH = Max(1, MipH / 4);
                        INT DataSize = BlocksW * BlocksH * 8; // DXT1 = 8 bytes per block
                        appMemcpy( lr.pBits, Info.Mips[m]->DataPtr, DataSize );
                        Entry->pTexture->UnlockRect( m - FirstMip );
                    }
                }
            }
        }
        else
        {
            // P8, RGBA7, RGBA8 — convert to A8R8G8B8.
            INT NumMips = Info.NumMips - FirstMip;
            if( NumMips < 1 ) NumMips = 1;
            Device->CreateTexture( USize, VSize, NumMips, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Entry->pTexture );
            if( Entry->pTexture )
            {
                for( INT m = FirstMip; m < Info.NumMips; m++ )
                {
                    if( !Info.Mips[m] || !Info.Mips[m]->DataPtr )
                        continue;
                    D3DLOCKED_RECT lr;
                    if( SUCCEEDED(Entry->pTexture->LockRect( m - FirstMip, &lr, NULL, 0 )) )
                    {
                        INT MipW = Info.Mips[m]->USize;
                        INT MipH = Info.Mips[m]->VSize;
                        DWORD* Dst = (DWORD*)lr.pBits;
                        INT DstPitch = lr.Pitch / 4; // in DWORDs

                        if( Info.Format == TEXF_P8 )
                        {
                            // Palettized 8-bit.
                            BYTE* Src = (BYTE*)Info.Mips[m]->DataPtr;
                            FColor* Pal = Info.Palette;
                            for( INT y = 0; y < MipH; y++ )
                            {
                                for( INT x = 0; x < MipW; x++ )
                                {
                                    BYTE Idx = Src[y * MipW + x];
                                    if( Idx == 0 && (PolyFlags & PF_Masked) )
                                    {
                                        Dst[y * DstPitch + x] = 0x00000000; // fully transparent
                                    }
                                    else if( Pal )
                                    {
                                        FColor& C = Pal[Idx];
                                        Dst[y * DstPitch + x] = D3DCOLOR_ARGB( C.A, C.R, C.G, C.B );
                                    }
                                    else
                                    {
                                        Dst[y * DstPitch + x] = 0xFFFF00FF; // magenta = missing palette
                                    }
                                }
                            }
                        }
                        else if( Info.Format == TEXF_RGBA7 )
                        {
                            // 7-bit per channel (stored as FColor array).
                            FColor* Src = (FColor*)Info.Mips[m]->DataPtr;
                            for( INT y = 0; y < MipH; y++ )
                            {
                                for( INT x = 0; x < MipW; x++ )
                                {
                                    FColor& C = Src[y * MipW + x];
                                    // Shift up by 1 to convert 7-bit to 8-bit range.
                                    Dst[y * DstPitch + x] = D3DCOLOR_ARGB(
                                        Min((INT)(C.A << 1), 255),
                                        Min((INT)(C.R << 1), 255),
                                        Min((INT)(C.G << 1), 255),
                                        Min((INT)(C.B << 1), 255)
                                    );
                                }
                            }
                        }
                        else
                        {
                            // RGBA8 or unknown — treat as FColor array, swap to ARGB.
                            FColor* Src = (FColor*)Info.Mips[m]->DataPtr;
                            for( INT y = 0; y < MipH; y++ )
                            {
                                for( INT x = 0; x < MipW; x++ )
                                {
                                    FColor& C = Src[y * MipW + x];
                                    Dst[y * DstPitch + x] = D3DCOLOR_ARGB( C.A, C.R, C.G, C.B );
                                }
                            }
                        }
                        Entry->pTexture->UnlockRect( m - FirstMip );
                    }
                }
            }
        }
    }

    // Store stage state.
    BoundCacheID[Stage] = Info.CacheID;
    StageUScale[Stage]  = Entry->UScale;
    StageVScale[Stage]  = Entry->VScale;
    Entry->FrameCounter = FrameCounter;

    // Bind the D3D texture.
    Device->SetTexture( Stage, Entry->pTexture );

    unguard;
}

// ============================================================================
// DrawComplexSurface — BSP world geometry
// Port of D3D7 lines 660-902.
// ============================================================================
void UXboxRenderDevice::DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet )
{
    guard(UXboxRenderDevice::DrawComplexSurface);

    if( !Device || !Frame )
        return;

    // Mutually exclusive effects.
    if( Surface.DetailTexture && Surface.FogMap )
        Surface.DetailTexture = NULL;

    // Pre-count vertices for the whole facet.
    INT TotalVerts = 0;
    for( FSavedPoly* CountPoly = Facet.Polys; CountPoly; CountPoly = CountPoly->Next )
        TotalVerts += CountPoly->NumPts;

    // Multitexture path: base texture + lightmap in one pass.
    if( Surface.LightMap != NULL && Surface.MacroTexture == NULL )
    {
        SetBlending( Surface.PolyFlags | PF_Memorized );
        SetTextureD3D( 0, *Surface.Texture, Surface.PolyFlags );
        SetTextureD3D( 1, *Surface.LightMap, 0 );

        // Set stage 1 filtering.
        Device->SetTextureStageState( 1, D3DTSS_COLORARG1, D3DTA_TEXTURE );
        Device->SetTextureStageState( 1, D3DTSS_COLORARG2, D3DTA_CURRENT );
        Device->SetTextureStageState( 1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
        Device->SetTextureStageState( 1, D3DTSS_ALPHAARG2, D3DTA_CURRENT );
        Device->SetTextureStageState( 1, D3DTSS_MINFILTER, D3DTEXF_LINEAR );
        Device->SetTextureStageState( 1, D3DTSS_MAGFILTER, D3DTEXF_LINEAR );

        Device->SetVertexShader( XBOX_FVF_TLVERTEX2 );

        // Draw each polygon in the facet.
        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            if( Poly->NumPts < 3 || Poly->NumPts > XBOX_MAX_VERTS )
                continue;

            FXboxTLVertex2 Verts[XBOX_MAX_VERTS];
            for( INT i = 0; i < Poly->NumPts; i++ )
            {
                FLOAT RHW = Poly->Pts[i]->RZ * RProjZ;
                Verts[i].x    = Poly->Pts[i]->ScreenX + Frame->XB - 0.5f;
                Verts[i].y    = Poly->Pts[i]->ScreenY + Frame->YB - 0.5f;
                Verts[i].z    = ProjZRatio + ProjZOffset * RHW;
                Verts[i].rhw  = RHW;
                Verts[i].color = 0xFFFFFFFF;

                FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);

                Verts[i].u0 = (u - Surface.Texture->Pan.X) * StageUScale[0];
                Verts[i].v0 = (v - Surface.Texture->Pan.Y) * StageVScale[0];
                Verts[i].u1 = (u - Surface.LightMap->Pan.X + 0.5f * Surface.LightMap->UScale) * StageUScale[1];
                Verts[i].v1 = (v - Surface.LightMap->Pan.Y + 0.5f * Surface.LightMap->VScale) * StageVScale[1];
            }
            Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, Verts, sizeof(FXboxTLVertex2) );
        }

        // Handle masked depth write.
        if( Surface.PolyFlags & PF_Masked )
            Device->SetRenderState( D3DRS_ZFUNC, D3DCMP_EQUAL );
    }
    else
    {
        // Single-texture fallback: multiple passes.
        // Pre-compute world-space UVs for all verts.
        FLOAT MasterU[XBOX_MAX_VERTS];
        FLOAT MasterV[XBOX_MAX_VERTS];
        FXboxTLVertex Verts[XBOX_MAX_VERTS];
        INT n = 0;
        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            for( INT i = 0; i < Poly->NumPts && n < XBOX_MAX_VERTS; i++, n++ )
            {
                FLOAT RHW = Poly->Pts[i]->RZ * RProjZ;
                Verts[n].x    = Poly->Pts[i]->ScreenX + Frame->XB - 0.5f;
                Verts[n].y    = Poly->Pts[i]->ScreenY + Frame->YB - 0.5f;
                Verts[n].z    = ProjZRatio + ProjZOffset * RHW;
                Verts[n].rhw  = RHW;
                MasterU[n]    = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                MasterV[n]    = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
            }
        }

        // Pass 1: Base texture.
        SetTextureD3D( 0, *Surface.Texture, Surface.PolyFlags );
        SetBlending( Surface.PolyFlags & ~PF_Memorized );
        Device->SetVertexShader( XBOX_FVF_TLVERTEX );

        n = 0;
        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            for( INT i = 0; i < Poly->NumPts; i++ )
            {
                Verts[n + i].color = 0xFFFFFFFF;
                Verts[n + i].u = (MasterU[n + i] - Surface.Texture->Pan.X) * StageUScale[0];
                Verts[n + i].v = (MasterV[n + i] - Surface.Texture->Pan.Y) * StageVScale[0];
            }
            Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, &Verts[n], sizeof(FXboxTLVertex) );
            n += Poly->NumPts;
        }

        // Handle masked depth write.
        if( Surface.PolyFlags & PF_Masked )
            Device->SetRenderState( D3DRS_ZFUNC, D3DCMP_EQUAL );

        // Pass 2: Macrotexture (modulated overlay).
        if( Surface.MacroTexture )
        {
            SetBlending( PF_Modulated );
            SetTextureD3D( 0, *Surface.MacroTexture, 0 );
            n = 0;
            for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
            {
                for( INT i = 0; i < Poly->NumPts; i++ )
                {
                    Verts[n + i].color = 0xFFFFFFFF;
                    Verts[n + i].u = (MasterU[n + i] - Surface.MacroTexture->Pan.X) * StageUScale[0];
                    Verts[n + i].v = (MasterV[n + i] - Surface.MacroTexture->Pan.Y) * StageVScale[0];
                }
                Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, &Verts[n], sizeof(FXboxTLVertex) );
                n += Poly->NumPts;
            }
        }

        // Pass 3: Lightmap (modulated overlay).
        if( Surface.LightMap )
        {
            SetBlending( PF_Modulated );
            SetTextureD3D( 0, *Surface.LightMap, 0 );
            n = 0;
            for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
            {
                for( INT i = 0; i < Poly->NumPts; i++ )
                {
                    Verts[n + i].color = 0xFFFFFFFF;
                    Verts[n + i].u = (MasterU[n + i] - Surface.LightMap->Pan.X + 0.5f * Surface.LightMap->UScale) * StageUScale[0];
                    Verts[n + i].v = (MasterV[n + i] - Surface.LightMap->Pan.Y + 0.5f * Surface.LightMap->VScale) * StageVScale[0];
                }
                Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, &Verts[n], sizeof(FXboxTLVertex) );
                n += Poly->NumPts;
            }
        }
    }

    // Fog map pass.
    if( Surface.FogMap )
    {
        SetBlending( PF_Highlighted );
        SetTextureD3D( 0, *Surface.FogMap, 0 );
        Device->SetVertexShader( XBOX_FVF_TLVERTEX );

        INT n = 0;
        FXboxTLVertex FogVerts[XBOX_MAX_VERTS];
        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            for( INT i = 0; i < Poly->NumPts && n < XBOX_MAX_VERTS; i++, n++ )
            {
                FLOAT RHW = Poly->Pts[i]->RZ * RProjZ;
                FLOAT u = Facet.MapCoords.XAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FLOAT v = Facet.MapCoords.YAxis | (*(FVector*)Poly->Pts[i] - Facet.MapCoords.Origin);
                FogVerts[n].x    = Poly->Pts[i]->ScreenX + Frame->XB - 0.5f;
                FogVerts[n].y    = Poly->Pts[i]->ScreenY + Frame->YB - 0.5f;
                FogVerts[n].z    = ProjZRatio + ProjZOffset * RHW;
                FogVerts[n].rhw  = RHW;
                FogVerts[n].color = 0xFFFFFFFF;
                FogVerts[n].u = (u - Surface.FogMap->Pan.X + 0.5f * Surface.FogMap->UScale) * StageUScale[0];
                FogVerts[n].v = (v - Surface.FogMap->Pan.Y + 0.5f * Surface.FogMap->VScale) * StageVScale[0];
            }
        }
        n = 0;
        for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
        {
            Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, Poly->NumPts - 2, &FogVerts[n], sizeof(FXboxTLVertex) );
            n += Poly->NumPts;
        }
    }

    // Finish mask handling.
    if( Surface.PolyFlags & PF_Masked )
        Device->SetRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );

    // Disable stage 1 if we used multitexture.
    if( Surface.LightMap && Surface.MacroTexture == NULL )
    {
        Device->SetTextureStageState( 1, D3DTSS_COLOROP, D3DTOP_DISABLE );
        Device->SetTextureStageState( 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE );
        Device->SetTexture( 1, NULL );
        BoundCacheID[1] = 0;
        // Re-clear PF_Memorized in current state.
        CurrentPolyFlags &= ~PF_Memorized;
    }

    unguard;
}

// ============================================================================
// DrawGouraudPolygon — mesh/actor rendering
// Port of D3D7 lines 904-957.
// ============================================================================
void UXboxRenderDevice::DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Info, FTransTexture** Pts, int NumPts, DWORD PolyFlags, FSpanBuffer* Span )
{
    guard(UXboxRenderDevice::DrawGouraudPolygon);

    if( !Device || !Frame || NumPts < 3 || NumPts > XBOX_MAX_VERTS )
        return;

    PolyFlags &= ~PF_Memorized;
    SetTextureD3D( 0, Info, PolyFlags );
    SetBlending( PolyFlags );

    Device->SetVertexShader( XBOX_FVF_TLVERTEX );

    FXboxTLVertex Verts[XBOX_MAX_VERTS];
    for( INT i = 0; i < NumPts; i++ )
    {
        FLOAT RHW      = Pts[i]->RZ * RProjZ;
        Verts[i].x     = Pts[i]->ScreenX + Frame->XB - 0.5f;
        Verts[i].y     = Pts[i]->ScreenY + Frame->YB - 0.5f;
        Verts[i].z     = ProjZRatio + ProjZOffset * RHW;
        Verts[i].rhw   = RHW;
        Verts[i].u     = Pts[i]->U * StageUScale[0];
        Verts[i].v     = Pts[i]->V * StageVScale[0];

        if( PolyFlags & PF_Modulated )
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

    Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, NumPts - 2, Verts, sizeof(FXboxTLVertex) );

    unguard;
}

// ============================================================================
// DrawTile — HUD/UI tile rendering
// Port of D3D7 lines 958-982.
// ============================================================================
void UXboxRenderDevice::DrawTile( FSceneNode* Frame, FTextureInfo& Info, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, class FSpanBuffer* Span, FLOAT Z, FPlane Color, FPlane Fog, DWORD PolyFlags )
{
    guard(UXboxRenderDevice::DrawTile);

    if( !Device || !Frame )
        return;

    PolyFlags &= ~PF_Memorized;
    if( Info.Palette && Info.Palette[128].A != 255 && !(PolyFlags & PF_Translucent) )
        PolyFlags |= PF_Highlighted;
    SetBlending( PolyFlags );
    SetTextureD3D( 0, Info, PolyFlags );

    FLOAT RZ  = 1.0f / Z;
    FLOAT SZ  = ProjZRatio + ProjZOffset * RZ;
    X        += Frame->XB - 0.5f;
    Y        += Frame->YB - 0.5f;

    DWORD Clr;
    if( PolyFlags & PF_Modulated )
        Clr = 0xFFFFFFFF;
    else
        Clr = FColor(Color).TrueColor() | 0xFF000000;

    Device->SetVertexShader( XBOX_FVF_TLVERTEX );

    FXboxTLVertex Verts[4];
    Verts[0].x = X;      Verts[0].y = Y;      Verts[0].rhw = RZ; Verts[0].z = SZ; Verts[0].u = U      * StageUScale[0]; Verts[0].v = V       * StageVScale[0]; Verts[0].color = Clr;
    Verts[1].x = X;      Verts[1].y = Y + YL; Verts[1].rhw = RZ; Verts[1].z = SZ; Verts[1].u = U      * StageUScale[0]; Verts[1].v = (V+VL)  * StageVScale[0]; Verts[1].color = Clr;
    Verts[2].x = X + XL; Verts[2].y = Y + YL; Verts[2].rhw = RZ; Verts[2].z = SZ; Verts[2].u = (U+UL) * StageUScale[0]; Verts[2].v = (V+VL)  * StageVScale[0]; Verts[2].color = Clr;
    Verts[3].x = X + XL; Verts[3].y = Y;      Verts[3].rhw = RZ; Verts[3].z = SZ; Verts[3].u = (U+UL) * StageUScale[0]; Verts[3].v = V       * StageVScale[0]; Verts[3].color = Clr;

    Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex) );

    unguard;
}

// ============================================================================
// Draw2DLine
// ============================================================================
void UXboxRenderDevice::Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 )
{
    guard(UXboxRenderDevice::Draw2DLine);

    if( !Device )
        return;

    Device->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

    DWORD Clr = FColor(Color).TrueColor() | 0xFF000000;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = ProjZRatio + ProjZOffset * RHW;

    FXboxTLVertex Verts[2];
    Verts[0].x = P1.X - 0.5f; Verts[0].y = P1.Y - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = P2.X - 0.5f; Verts[1].y = P2.Y - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;

    Device->SetVertexShader( XBOX_FVF_TLVERTEX );
    Device->DrawPrimitiveUP( D3DPT_LINELIST, 1, Verts, sizeof(FXboxTLVertex) );

    Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
    Device->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );

    unguard;
}

// ============================================================================
// Draw2DPoint
// ============================================================================
void UXboxRenderDevice::Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z )
{
    guard(UXboxRenderDevice::Draw2DPoint);

    if( !Device )
        return;

    Device->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_FLAT );
    Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_DISABLE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE );

    DWORD Clr = FColor(Color).TrueColor() | 0xFF000000;
    FLOAT RHW = 1.0f;
    FLOAT SZ  = ProjZRatio + ProjZOffset * RHW;

    FXboxTLVertex Verts[4];
    Verts[0].x = X1 - 0.5f; Verts[0].y = Y1 - 0.5f; Verts[0].rhw = RHW; Verts[0].z = SZ; Verts[0].color = Clr; Verts[0].u = 0; Verts[0].v = 0;
    Verts[1].x = X2 - 0.5f; Verts[1].y = Y1 - 0.5f; Verts[1].rhw = RHW; Verts[1].z = SZ; Verts[1].color = Clr; Verts[1].u = 0; Verts[1].v = 0;
    Verts[2].x = X2 - 0.5f; Verts[2].y = Y2 - 0.5f; Verts[2].rhw = RHW; Verts[2].z = SZ; Verts[2].color = Clr; Verts[2].u = 0; Verts[2].v = 0;
    Verts[3].x = X1 - 0.5f; Verts[3].y = Y2 - 0.5f; Verts[3].rhw = RHW; Verts[3].z = SZ; Verts[3].color = Clr; Verts[3].u = 0; Verts[3].v = 0;

    Device->SetVertexShader( XBOX_FVF_TLVERTEX );
    Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex) );

    Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
    Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
    Device->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );

    unguard;
}

// ============================================================================
// ClearZ
// ============================================================================
void UXboxRenderDevice::ClearZ( FSceneNode* Frame )
{
    guard(UXboxRenderDevice::ClearZ);
    if( Device )
        Device->Clear( 0, NULL, D3DCLEAR_ZBUFFER, 0, 1.0f, 0 );
    unguard;
}

// ============================================================================
// EndFlash — screen flash effect (damage, pickup, etc.)
// Port of D3D7 lines 1278-1307.
// ============================================================================
void UXboxRenderDevice::EndFlash()
{
    guard(UXboxRenderDevice::EndFlash);

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
        Device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
        Device->SetRenderState( D3DRS_SRCBLEND,  D3DBLEND_ONE );
        Device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_SRCALPHA );
        Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2 );
        Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 );
        Device->SetRenderState( D3DRS_ZFUNC, D3DCMP_ALWAYS );

        Device->SetVertexShader( XBOX_FVF_TLVERTEX );
        Device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, 2, Verts, sizeof(FXboxTLVertex) );

        Device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
        Device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
        Device->SetRenderState( D3DRS_ZFUNC, D3DCMP_LESSEQUAL );
        SetBlending( 0 );
    }

    unguard;
}

// ============================================================================
// PushHit / PopHit — not needed on Xbox (no hit testing)
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
    // Not implemented — screenshots not supported on Xbox.
    unguard;
}
