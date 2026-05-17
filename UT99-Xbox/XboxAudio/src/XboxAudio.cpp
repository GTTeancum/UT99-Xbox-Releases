// XboxAudio.cpp - DirectSound-backed Xbox audio subsystem.

#include <xtl.h>
#ifdef Top
#undef Top
#endif
#ifdef MAKEFOURCC
#undef MAKEFOURCC
#endif

#pragma warning(disable: 4005)
#undef  DLL_EXPORT
#define DLL_EXPORT
#undef  DLL_IMPORT
#define DLL_IMPORT
#undef  CORE_API
#define CORE_API
#undef  ENGINE_API
#define ENGINE_API
#pragma warning(disable: 4996 4244 4267 4305 4800 4018)
#pragma conform(forScope, off)

#undef  GPackage
#define GPackage GPackage_XboxAudio
#ifndef IMPLEMENT_PACKAGE_XBOX
#define IMPLEMENT_PACKAGE_XBOX 1
#endif

#include "Engine.h"
#include "FXboxLogger.h"

/*-----------------------------------------------------------------------------
    UXboxAudioDevice
-----------------------------------------------------------------------------*/

static LONG XboxVolumeToDS( FLOAT Volume )
{
    if( Volume <= 0.0f )
        return DSBVOLUME_MIN;
    if( Volume >= 1.0f )
        return DSBVOLUME_MAX;

    // First pass: conservative linear-to-dB-ish falloff. Good enough for
    // effects playback, avoids dragging CRT math/log dependencies into XDK.
    return (LONG)(DSBVOLUME_MIN * (1.0f - Volume));
}

class UXboxAudioDevice : public UAudioSubsystem
{
    DECLARE_CLASS(UXboxAudioDevice, UAudioSubsystem, CLASS_Config)
    NO_DEFAULT_CONSTRUCTOR(UXboxAudioDevice)

    UViewport*      Viewport;
    IDirectSound*   DirectSound;
    INT             RegisteredSounds;
    INT             PlayedSounds;
    INT             FailedSounds;

public:
    void StaticConstructor()
    {
        Viewport         = NULL;
        DirectSound      = NULL;
        RegisteredSounds = 0;
        PlayedSounds     = 0;
        FailedSounds     = 0;
    }

    UBOOL Init()
    {
        guard(UXboxAudioDevice::Init);

        HRESULT hr = DirectSoundCreate( NULL, &DirectSound, NULL );
        if( FAILED(hr) || !DirectSound )
        {
            GXboxLog.Write( "XboxAudio: DirectSoundCreate failed hr=0x%08X", (DWORD)hr );
            DirectSound = NULL;
            return 0;
        }

        USound::Audio = this;
        UMusic::Audio = this;

        GXboxLog.Write( "XboxAudio: DirectSound initialized" );
        return 1;

        unguard;
    }

    void Destroy()
    {
        guard(UXboxAudioDevice::Destroy);

        USound::Audio = NULL;
        UMusic::Audio = NULL;
        if( DirectSound )
        {
            DirectSound->Release();
            DirectSound = NULL;
        }

        GXboxLog.Write( "XboxAudio: Destroy registered=%d played=%d failed=%d",
            RegisteredSounds, PlayedSounds, FailedSounds );
        Super::Destroy();

        unguard;
    }

    void SetViewport( UViewport* InViewport )
    {
        Viewport = InViewport;
    }

    UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar )
    {
        if( ParseCommand( &Cmd, TEXT("XAUDIOSTAT") ) )
        {
            Ar.Logf( TEXT("XboxAudio registered=%d played=%d failed=%d"), RegisteredSounds, PlayedSounds, FailedSounds );
            return 1;
        }
        return 0;
    }

    void Update( FPointRegion Region, FCoords& Listener )
    {
    }

    void RegisterMusic( UMusic* Music )
    {
    }

    void RegisterSound( USound* Sound )
    {
        guard(UXboxAudioDevice::RegisterSound);

        if( !DirectSound || !Sound || Sound->Handle )
            return;

        Sound->Data.Load();
        if( Sound->Data.Num() <= 0 )
            return;

        FWaveModInfo WaveInfo;
        if( !WaveInfo.ReadWaveInfo( Sound->Data ) )
        {
            if( FailedSounds < 32 )
                GXboxLog.Write( "XboxAudio: RegisterSound rejected non-wave %s", TCHAR_TO_ANSI(Sound->GetName()) );
            FailedSounds++;
            return;
        }

        WAVEFORMATEX wfx;
        appMemzero( &wfx, sizeof(wfx) );
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = (WORD)*WaveInfo.pChannels;
        wfx.nSamplesPerSec  = (DWORD)*WaveInfo.pSamplesPerSec;
        wfx.wBitsPerSample  = (WORD)*WaveInfo.pBitsPerSample;
        wfx.nBlockAlign     = (WORD)*WaveInfo.pBlockAlign;
        wfx.nAvgBytesPerSec = (DWORD)*WaveInfo.pAvgBytesPerSec;

        if( wfx.nChannels < 1 || wfx.nChannels > 2 || (wfx.wBitsPerSample != 8 && wfx.wBitsPerSample != 16) || WaveInfo.SampleDataSize <= 0 )
        {
            if( FailedSounds < 32 )
                GXboxLog.Write( "XboxAudio: unsupported wave %s ch=%d bits=%d bytes=%d",
                    TCHAR_TO_ANSI(Sound->GetName()), wfx.nChannels, wfx.wBitsPerSample, WaveInfo.SampleDataSize );
            FailedSounds++;
            return;
        }

        DSBUFFERDESC Desc;
        appMemzero( &Desc, sizeof(Desc) );
        Desc.dwSize        = sizeof(Desc);
        Desc.dwFlags       = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY;
        Desc.dwBufferBytes = (DWORD)WaveInfo.SampleDataSize;
        Desc.lpwfxFormat   = &wfx;

        IDirectSoundBuffer* Buffer = NULL;
        HRESULT hr = DirectSound->CreateSoundBuffer( &Desc, &Buffer, NULL );
        if( FAILED(hr) || !Buffer )
        {
            if( FailedSounds < 32 )
                GXboxLog.Write( "XboxAudio: CreateSoundBuffer failed %s hr=0x%08X bytes=%d rate=%d",
                    TCHAR_TO_ANSI(Sound->GetName()), (DWORD)hr, WaveInfo.SampleDataSize, wfx.nSamplesPerSec );
            FailedSounds++;
            return;
        }

        VOID* Lock1 = NULL;
        DWORD Size1 = 0;
        VOID* Lock2 = NULL;
        DWORD Size2 = 0;
        hr = Buffer->Lock( 0, (DWORD)WaveInfo.SampleDataSize, &Lock1, &Size1, &Lock2, &Size2, 0 );
        if( FAILED(hr) )
        {
            Buffer->Release();
            if( FailedSounds < 32 )
                GXboxLog.Write( "XboxAudio: Buffer Lock failed %s hr=0x%08X", TCHAR_TO_ANSI(Sound->GetName()), (DWORD)hr );
            FailedSounds++;
            return;
        }

        appMemcpy( Lock1, WaveInfo.SampleDataStart, Size1 );
        if( Lock2 && Size2 )
            appMemcpy( Lock2, WaveInfo.SampleDataStart + Size1, Size2 );
        Buffer->Unlock( Lock1, Size1, Lock2, Size2 );

        Sound->Handle = Buffer;
        RegisteredSounds++;
        if( RegisteredSounds <= 24 )
            GXboxLog.Write( "XboxAudio: registered #%d %s ch=%d bits=%d rate=%d bytes=%d",
                RegisteredSounds, TCHAR_TO_ANSI(Sound->GetName()), wfx.nChannels, wfx.wBitsPerSample,
                wfx.nSamplesPerSec, WaveInfo.SampleDataSize );

        unguard;
    }

    void UnregisterSound( USound* Sound )
    {
        guard(UXboxAudioDevice::UnregisterSound);

        if( Sound && Sound->Handle )
        {
            IDirectSoundBuffer* Buffer = (IDirectSoundBuffer*)Sound->Handle;
            Buffer->Stop();
            Buffer->Release();
            Sound->Handle = NULL;
        }

        unguard;
    }

    void UnregisterMusic( UMusic* Music )
    {
    }

    UBOOL PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch )
    {
        guard(UXboxAudioDevice::PlaySound);

        if( !DirectSound || !Sound )
            return 0;

        if( !Sound->Handle )
            RegisterSound( Sound );

        IDirectSoundBuffer* Buffer = (IDirectSoundBuffer*)Sound->Handle;
        if( !Buffer )
            return 0;

        Sound->Data.Load();
        FWaveModInfo WaveInfo;
        DWORD BaseRate = 22050;
        if( WaveInfo.ReadWaveInfo( Sound->Data ) && *WaveInfo.pSamplesPerSec )
            BaseRate = (DWORD)*WaveInfo.pSamplesPerSec;

        FLOAT ClampedPitch = Clamp( Pitch, 0.25f, 4.0f );
        Buffer->Stop();
        Buffer->SetCurrentPosition( 0 );
        Buffer->SetVolume( XboxVolumeToDS( Clamp( Volume, 0.0f, 1.0f ) ) );
        Buffer->SetFrequency( Max<DWORD>( 100, (DWORD)(BaseRate * ClampedPitch) ) );

        HRESULT hr = Buffer->Play( 0, 0, 0 );
        if( FAILED(hr) )
        {
            if( FailedSounds < 64 )
                GXboxLog.Write( "XboxAudio: Play failed %s hr=0x%08X", TCHAR_TO_ANSI(Sound->GetName()), (DWORD)hr );
            FailedSounds++;
            return 0;
        }

        PlayedSounds++;
        if( PlayedSounds <= 32 )
            GXboxLog.Write( "XboxAudio: play #%d %s vol=%.2f pitch=%.2f",
                PlayedSounds, TCHAR_TO_ANSI(Sound->GetName()), Volume, Pitch );
        return 1;

        unguard;
    }

    void NoteDestroy( AActor* Actor )
    {
    }

    UBOOL GetLowQualitySetting()
    {
        return 1;
    }

    UViewport* GetViewport()
    {
        return Viewport;
    }

    void RenderAudioGeometry( FSceneNode* Frame )
    {
    }

    void PostRender( FSceneNode* Frame )
    {
    }
};

IMPLEMENT_CLASS(UXboxAudioDevice);
IMPLEMENT_PACKAGE(XboxAudio);
