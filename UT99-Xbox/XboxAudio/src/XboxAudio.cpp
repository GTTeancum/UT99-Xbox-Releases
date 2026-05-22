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

extern "C"
{
#include "xmp.h"
}

extern "C" UBOOL XboxMenuWantsEffectSuppression();
extern "C" UBOOL XboxMenuAllowsEffectSound( INT Id );

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
    INT             MusicVolume;
    INT             SoundVolume;
    xmp_context     MusicContext;
    IDirectSoundBuffer* MusicBuffer;
    UMusic*         CurrentMusic;
    BYTE            CurrentSection;
    BYTE            CurrentCDTrack;
    DWORD           MusicWriteCursor;
    DWORD           MusicBufferBytes;
    UBOOL           MusicPlaying;
    UBOOL           MusicPaused;
    UBOOL           MusicFailed;
    UBOOL           SuppressEffects;

public:
    void StaticConstructor()
    {
        new(GetClass(),TEXT("MusicVolume"), RF_Public) UIntProperty( CPP_PROPERTY(MusicVolume), TEXT("Audio"), CPF_Config );
        new(GetClass(),TEXT("SoundVolume"), RF_Public) UIntProperty( CPP_PROPERTY(SoundVolume), TEXT("Audio"), CPF_Config );

        Viewport         = NULL;
        DirectSound      = NULL;
        RegisteredSounds = 0;
        PlayedSounds     = 0;
        FailedSounds     = 0;
        MusicVolume      = 255;
        SoundVolume      = 255;
        MusicContext     = NULL;
        MusicBuffer      = NULL;
        CurrentMusic     = NULL;
        CurrentSection   = 255;
        CurrentCDTrack   = 255;
        MusicWriteCursor = 0;
        MusicBufferBytes = 0;
        MusicPlaying     = 0;
        MusicPaused      = 0;
        MusicFailed      = 0;
        SuppressEffects  = 0;
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

        MusicVolume = 255;
        SoundVolume = 255;
        LoadConfig();
        MusicVolume = Clamp<INT>( MusicVolume, 0, 255 );
        SoundVolume = Clamp<INT>( SoundVolume, 0, 255 );

        MusicContext = xmp_create_context();
        if( !MusicContext )
            GXboxLog.Write( "XboxAudio: xmp_create_context failed; music disabled" );

        GXboxLog.Write( "XboxAudio: DirectSound initialized musicCtx=%s musicVol=%d soundVol=%d",
            MusicContext ? "OK" : "FAIL", MusicVolume, SoundVolume );
        return 1;

        unguard;
    }

    void Destroy()
    {
        guard(UXboxAudioDevice::Destroy);

        USound::Audio = NULL;
        UMusic::Audio = NULL;
        StopMusic();
        if( MusicContext )
        {
            xmp_free_context( MusicContext );
            MusicContext = NULL;
        }
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
            Ar.Logf( TEXT("XboxAudio registered=%d played=%d failed=%d music=%s section=%d"),
                RegisteredSounds, PlayedSounds, FailedSounds,
                CurrentMusic ? CurrentMusic->GetName() : TEXT("None"),
                CurrentSection );
            return 1;
        }
        if( ParseCommand( &Cmd, TEXT("XAUDIOSTOPFX") ) )
        {
            StopAllEffects();
            GXboxLog.Write( "XboxAudio: stopped active effects" );
            return 1;
        }
        if( ParseCommand( &Cmd, TEXT("XAUDIOSETMENUMODE") ) )
        {
            SuppressEffects = appAtoi(Cmd) ? 1 : 0;
            if( SuppressEffects )
                StopAllEffects();
            GXboxLog.Write( "XboxAudio: menu effect suppression %s", SuppressEffects ? "on" : "off" );
            return 1;
        }
        if( ParseCommand( &Cmd, TEXT("XAUDIOPAUSEMUSIC") ) )
        {
            SetMusicPaused( appAtoi(Cmd) ? 1 : 0 );
            return 1;
        }
        if( ParseCommand( &Cmd, TEXT("XAUDIOSETMUSICVOLUME") ) )
        {
            MusicVolume = Clamp<INT>( appAtoi(Cmd), 0, 255 );
            if( MusicBuffer )
                MusicBuffer->SetVolume( XboxVolumeToDS( (FLOAT)MusicVolume / 255.0f ) );
            SaveConfig();
            GXboxLog.Write( "XboxAudio: music volume=%d", MusicVolume );
            return 1;
        }
        if( ParseCommand( &Cmd, TEXT("XAUDIOSETSOUNDVOLUME") ) )
        {
            SoundVolume = Clamp<INT>( appAtoi(Cmd), 0, 255 );
            SaveConfig();
            GXboxLog.Write( "XboxAudio: sound volume=%d", SoundVolume );
            return 1;
        }
        return 0;
    }

    void Update( FPointRegion Region, FCoords& Listener )
    {
        guard(UXboxAudioDevice::Update);

        if( Viewport && Viewport->Actor )
        {
            if( Viewport->Actor->Song && Viewport->Actor->Transition==MTRAN_None && !CurrentMusic )
                Viewport->Actor->Transition = MTRAN_Instant;

            if( Viewport->Actor->Transition!=MTRAN_None )
            {
                UMusic* NewMusic = Viewport->Actor->Song;
                BYTE NewSection = Viewport->Actor->SongSection;
                BYTE NewCDTrack = Viewport->Actor->CdTrack;
                UBOOL ChangeMusic = CurrentMusic!=NewMusic || CurrentSection!=NewSection;

                GXboxLog.Write( "XboxAudio: music transition old=%s new=%s oldSec=%d newSec=%d cd=%d trans=%d",
                    CurrentMusic ? TCHAR_TO_ANSI(CurrentMusic->GetName()) : "None",
                    NewMusic ? TCHAR_TO_ANSI(NewMusic->GetName()) : "None",
                    CurrentSection, NewSection, NewCDTrack, Viewport->Actor->Transition );

                if( ChangeMusic )
                    StartMusic( NewMusic, NewSection, NewCDTrack );

                Viewport->Actor->Transition = MTRAN_None;
            }
        }

        ServiceMusicStream();

        unguard;
    }

    void RegisterMusic( UMusic* Music )
    {
        if( Music && !Music->Handle )
            Music->Handle = (void*)-1;
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
        if( Music && Music==CurrentMusic )
            StopMusic();
        if( Music )
            Music->Handle = NULL;
    }

    UBOOL PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch )
    {
        guard(UXboxAudioDevice::PlaySound);

        if( !DirectSound || !Sound )
            return 0;

        if( XboxMenuWantsEffectSuppression() && !XboxMenuAllowsEffectSound( Id ) )
        {
            static INT SuppressedLogCount = 0;
            if( SuppressedLogCount < 16 )
            {
                GXboxLog.Write( "XboxAudio: suppressed effect while menu open sound=%s id=%d",
                    Sound ? TCHAR_TO_ANSI(Sound->GetName()) : "None", Id );
                SuppressedLogCount++;
            }
            return 0;
        }

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
        Buffer->SetVolume( XboxVolumeToDS( Clamp( Volume * ((FLOAT)SoundVolume / 255.0f), 0.0f, 1.0f ) ) );
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

private:
    enum
    {
        MusicRate        = 22050,
        MusicChannels    = 2,
        MusicBits        = 16,
        MusicBlockAlign  = 4,
        MusicStreamBytes = 65536,
        MusicSafetyBytes = 8192
    };

    void StartMusic( UMusic* Music, BYTE Section, BYTE CDTrack )
    {
        guard(UXboxAudioDevice::StartMusic);

        StopMusic();

        CurrentMusic   = Music;
        CurrentSection = Section;
        CurrentCDTrack = CDTrack;
        MusicPaused    = 0;
        MusicFailed    = 0;

        if( !DirectSound || !MusicContext || !Music || Section==255 )
        {
            GXboxLog.Write( "XboxAudio: music not started ds=%s ctx=%s music=%s section=%d",
                DirectSound ? "OK" : "NULL",
                MusicContext ? "OK" : "NULL",
                Music ? TCHAR_TO_ANSI(Music->GetName()) : "None",
                Section );
            return;
        }

        Music->Data.Load();
        if( Music->Data.Num() <= 0 )
        {
            GXboxLog.Write( "XboxAudio: music %s has no data", TCHAR_TO_ANSI(Music->GetName()) );
            return;
        }

        Music->Data.Add( 1024 );
        int LoadResult = xmp_load_module_from_memory( MusicContext, &Music->Data(0), Music->Data.Num() );
        if( LoadResult != 0 )
        {
            GXboxLog.Write( "XboxAudio: xmp_load_module_from_memory failed %s result=%d bytes=%d",
                TCHAR_TO_ANSI(Music->GetName()), LoadResult, Music->Data.Num() );
            MusicFailed = 1;
            return;
        }

        int StartResult = xmp_start_player( MusicContext, MusicRate, 0 );
        if( StartResult != 0 )
        {
            GXboxLog.Write( "XboxAudio: xmp_start_player failed %s result=%d", TCHAR_TO_ANSI(Music->GetName()), StartResult );
            xmp_release_module( MusicContext );
            MusicFailed = 1;
            return;
        }

        if( Section != 0 )
            xmp_set_position( MusicContext, Section );

        WAVEFORMATEX wfx;
        appMemzero( &wfx, sizeof(wfx) );
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = MusicChannels;
        wfx.nSamplesPerSec  = MusicRate;
        wfx.wBitsPerSample  = MusicBits;
        wfx.nBlockAlign     = MusicBlockAlign;
        wfx.nAvgBytesPerSec = MusicRate * MusicBlockAlign;

        DSBUFFERDESC Desc;
        appMemzero( &Desc, sizeof(Desc) );
        Desc.dwSize        = sizeof(Desc);
        Desc.dwFlags       = DSBCAPS_CTRLVOLUME;
        Desc.dwBufferBytes = MusicStreamBytes;
        Desc.lpwfxFormat   = &wfx;

        HRESULT hr = DirectSound->CreateSoundBuffer( &Desc, &MusicBuffer, NULL );
        if( FAILED(hr) || !MusicBuffer )
        {
            GXboxLog.Write( "XboxAudio: music CreateSoundBuffer failed %s hr=0x%08X",
                TCHAR_TO_ANSI(Music->GetName()), (DWORD)hr );
            xmp_end_player( MusicContext );
            xmp_release_module( MusicContext );
            MusicFailed = 1;
            return;
        }

        MusicBufferBytes = MusicStreamBytes;
        MusicWriteCursor = 0;
        FillMusicBytes( MusicBufferBytes );
        MusicWriteCursor = 0;
        MusicBuffer->SetCurrentPosition( 0 );
        MusicBuffer->SetVolume( XboxVolumeToDS( (FLOAT)MusicVolume / 255.0f ) );
        hr = MusicBuffer->Play( 0, 0, DSBPLAY_LOOPING );
        if( FAILED(hr) )
        {
            GXboxLog.Write( "XboxAudio: music Play failed %s hr=0x%08X", TCHAR_TO_ANSI(Music->GetName()), (DWORD)hr );
            StopMusic();
            MusicFailed = 1;
            return;
        }

        MusicPlaying = 1;
        RegisterMusic( Music );
        Music->Data.Unload();
        GXboxLog.Write( "XboxAudio: music started %s section=%d rate=%d",
            TCHAR_TO_ANSI(Music->GetName()), Section, MusicRate );

        unguard;
    }

    void StopMusic()
    {
        guard(UXboxAudioDevice::StopMusic);

        if( MusicBuffer )
        {
            MusicBuffer->Stop();
            MusicBuffer->Release();
            MusicBuffer = NULL;
        }
        if( MusicContext && MusicPlaying )
        {
            xmp_end_player( MusicContext );
            xmp_release_module( MusicContext );
        }
        if( CurrentMusic )
            CurrentMusic->Handle = NULL;
        CurrentMusic = NULL;
        CurrentSection = 255;
        CurrentCDTrack = 255;
        MusicWriteCursor = 0;
        MusicBufferBytes = 0;
        MusicPlaying = 0;
        MusicPaused = 0;

        unguard;
    }

    void SetMusicPaused( UBOOL bPaused )
    {
        guard(UXboxAudioDevice::SetMusicPaused);

        bPaused = bPaused ? 1 : 0;
        if( MusicPaused == bPaused )
            return;

        MusicPaused = bPaused;
        if( MusicBuffer && MusicPlaying )
        {
            HRESULT hr = bPaused
                ? MusicBuffer->Stop()
                : MusicBuffer->Play( 0, 0, DSBPLAY_LOOPING );
            if( FAILED(hr) )
            {
                GXboxLog.Write( "XboxAudio: music pause=%d failed hr=0x%08X", MusicPaused, (DWORD)hr );
                MusicFailed = 1;
            }
        }
        GXboxLog.Write( "XboxAudio: music pause=%d music=%s",
            MusicPaused,
            CurrentMusic ? TCHAR_TO_ANSI(CurrentMusic->GetName()) : "None" );

        unguard;
    }

    void FillMusicBytes( DWORD Bytes )
    {
        guard(UXboxAudioDevice::FillMusicBytes);

        if( !MusicBuffer || !MusicContext || (!MusicPlaying && !CurrentMusic) )
            return;

        Bytes &= ~(DWORD)(MusicBlockAlign - 1);
        while( Bytes > 0 )
        {
            DWORD Chunk = Min<DWORD>( Bytes, MusicBufferBytes - MusicWriteCursor );
            VOID* Lock1 = NULL;
            DWORD Size1 = 0;
            VOID* Lock2 = NULL;
            DWORD Size2 = 0;
            HRESULT hr = MusicBuffer->Lock( MusicWriteCursor, Chunk, &Lock1, &Size1, &Lock2, &Size2, 0 );
            if( FAILED(hr) )
            {
                if( !MusicFailed )
                    GXboxLog.Write( "XboxAudio: music buffer Lock failed hr=0x%08X", (DWORD)hr );
                MusicFailed = 1;
                return;
            }

            RenderMusicBlock( Lock1, Size1 );
            if( Lock2 && Size2 )
                RenderMusicBlock( Lock2, Size2 );
            MusicBuffer->Unlock( Lock1, Size1, Lock2, Size2 );

            MusicWriteCursor = (MusicWriteCursor + Chunk) % MusicBufferBytes;
            Bytes -= Chunk;
        }

        unguard;
    }

    void RenderMusicBlock( VOID* Dest, DWORD Size )
    {
        guard(UXboxAudioDevice::RenderMusicBlock);

        if( !Dest || !Size )
            return;

        int Result = xmp_play_buffer( MusicContext, Dest, (int)Size, 1 );
        if( Result < 0 )
        {
            appMemzero( Dest, Size );
            if( !MusicFailed )
                GXboxLog.Write( "XboxAudio: xmp_play_buffer returned %d; writing silence", Result );
            MusicFailed = 1;
        }

        unguard;
    }

    void ServiceMusicStream()
    {
        guard(UXboxAudioDevice::ServiceMusicStream);

        if( !MusicBuffer || !MusicPlaying || MusicPaused || !MusicBufferBytes )
            return;

        DWORD PlayCursor = 0;
        DWORD WriteCursor = 0;
        HRESULT hr = MusicBuffer->GetCurrentPosition( &PlayCursor, &WriteCursor );
        if( FAILED(hr) )
        {
            if( !MusicFailed )
                GXboxLog.Write( "XboxAudio: music GetCurrentPosition failed hr=0x%08X", (DWORD)hr );
            MusicFailed = 1;
            return;
        }

        DWORD Target = (PlayCursor + MusicBufferBytes - MusicSafetyBytes) % MusicBufferBytes;
        DWORD Bytes = (Target + MusicBufferBytes - MusicWriteCursor) % MusicBufferBytes;
        Bytes &= ~(DWORD)(MusicBlockAlign - 1);
        if( Bytes )
            FillMusicBytes( Bytes );

        unguard;
    }

    void StopAllEffects()
    {
        guard(UXboxAudioDevice::StopAllEffects);
        for( TObjectIterator<USound> It; It; ++It )
        {
            if( It->Handle )
                ((IDirectSoundBuffer*)It->Handle)->Stop();
        }
        unguard;
    }

};

IMPLEMENT_CLASS(UXboxAudioDevice);
IMPLEMENT_PACKAGE(XboxAudio);
