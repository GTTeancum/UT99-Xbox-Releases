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

#define XBOX_ENABLE_UMX_MUSIC 0

#if XBOX_ENABLE_UMX_MUSIC
extern "C"
{
#include "xmp.h"
void libxmp_xbox_reset_alloc_stats(void);
void libxmp_xbox_get_alloc_stats(unsigned long*, unsigned long*, unsigned long*, unsigned long*, unsigned long*, unsigned long*, unsigned long*);
}
#else
typedef void* xmp_context;
#endif

extern "C" UBOOL XboxMenuWantsEffectSuppression();
extern "C" UBOOL XboxMenuAllowsEffectSound( INT Id );
extern "C" volatile LONG GXboxAudioToneSmokeState = 0;
extern "C" volatile LONG GXboxAudioMusicLoadState = 0;
extern "C" volatile LONG GXboxAudioMusicPacketState = 0;
extern "C" volatile LONG GXboxAudioMusicStreamState = 0;

enum { ToneSmokeStreamPackets = 4, ToneSmokeStreamSamples = 4096, ToneSmokeStreamChannels = 2, ToneSmokeStreamRate = 44100, ToneSmokeStreamBytes = ToneSmokeStreamSamples * ToneSmokeStreamChannels * 2 };
enum { XboxMusicStreamPackets = 3, XboxMusicStreamPacketBytes = 18432 };
#if XBOX_ENABLE_UMX_MUSIC
static BYTE GXboxMusicStreamData[XboxMusicStreamPackets][XboxMusicStreamPacketBytes];
#endif

static DWORD XboxAudioAvailPhysKB()
{
    MEMORYSTATUS Status;
    appMemzero( &Status, sizeof(Status) );
    GlobalMemoryStatus( &Status );
    return Status.dwAvailPhys / 1024;
}

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

static const DSMIXBINS* XboxDefaultMixBinsForChannels( WORD Channels )
{
    return (Channels == 1) ? &DirectSoundDefaultMixBins_Mono : &DirectSoundDefaultMixBins_Stereo;
}

enum { XboxSoundMetaSlots = 1024 };

struct FXboxSoundMeta
{
    USound* Sound;
    DWORD   BaseRate;
    DWORD   Bytes;
};

static FXboxSoundMeta GXboxSoundMeta[XboxSoundMetaSlots];

static INT XboxSoundMetaFindSlot( USound* Sound, UBOOL bAllowEmpty )
{
    if( !Sound )
        return -1;

    DWORD Start = (((DWORD)Sound) >> 4) & (XboxSoundMetaSlots - 1);
    INT FirstEmpty = -1;
    for( INT Probe=0; Probe<XboxSoundMetaSlots; Probe++ )
    {
        INT Index = (Start + Probe) & (XboxSoundMetaSlots - 1);
        if( GXboxSoundMeta[Index].Sound == Sound )
            return Index;
        if( !GXboxSoundMeta[Index].Sound && FirstEmpty < 0 )
            FirstEmpty = Index;
    }
    return bAllowEmpty ? FirstEmpty : -1;
}

static void XboxSoundMetaSet( USound* Sound, DWORD BaseRate, DWORD Bytes )
{
    INT Index = XboxSoundMetaFindSlot( Sound, 1 );
    if( Index >= 0 )
    {
        GXboxSoundMeta[Index].Sound    = Sound;
        GXboxSoundMeta[Index].BaseRate = BaseRate ? BaseRate : 22050;
        GXboxSoundMeta[Index].Bytes    = Bytes;
    }
}

static DWORD XboxSoundMetaGetBaseRate( USound* Sound )
{
    INT Index = XboxSoundMetaFindSlot( Sound, 0 );
    return (Index >= 0 && GXboxSoundMeta[Index].BaseRate) ? GXboxSoundMeta[Index].BaseRate : 22050;
}

static void XboxSoundMetaClear( USound* Sound )
{
    INT Index = XboxSoundMetaFindSlot( Sound, 0 );
    if( Index >= 0 )
        appMemzero( &GXboxSoundMeta[Index], sizeof(GXboxSoundMeta[Index]) );
}

static INT XboxAudioToneSmokeMode()
{
    HANDLE File = CreateFileA( "D:\\XboxAudioToneSmoke.ini", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
        return 0;

    char Buffer[128];
    DWORD Read = 0;
    appMemzero( Buffer, sizeof(Buffer) );
    ReadFile( File, Buffer, sizeof(Buffer)-1, &Read, NULL );
    CloseHandle( File );
    Buffer[sizeof(Buffer)-1] = 0;

    return appStrstr( Buffer, "Enabled=2" ) ? 2 : 1;
}

#if XBOX_ENABLE_UMX_MUSIC
static INT XboxFindEmbeddedTrackerModule( const BYTE* Data, INT Bytes )
{
    if( !Data || Bytes < 4 )
        return -1;

    const INT ScanBytes = Bytes;
    for( INT i=0; i<=ScanBytes-4; i++ )
    {
        if( Data[i+0]=='I' && Data[i+1]=='M' && Data[i+2]=='P' && Data[i+3]=='M' )
            return i;
        if( i <= ScanBytes-17 && appMemcmp( Data+i, "Extended Module: ", 17 ) == 0 )
            return i;
        if( i >= 44 && Data[i+0]=='S' && Data[i+1]=='C' && Data[i+2]=='R' && Data[i+3]=='M' )
            return i - 44;
        if( i >= 1080 )
        {
            if( (Data[i+0]=='M' && Data[i+1]=='.' && Data[i+2]=='K' && Data[i+3]=='.') ||
                (Data[i+0]=='M' && Data[i+1]=='!' && Data[i+2]=='K' && Data[i+3]=='!') ||
                (Data[i+0]=='4' && Data[i+1]=='C' && Data[i+2]=='H' && Data[i+3]=='N') ||
                (Data[i+0]=='6' && Data[i+1]=='C' && Data[i+2]=='H' && Data[i+3]=='N') ||
                (Data[i+0]=='8' && Data[i+1]=='C' && Data[i+2]=='H' && Data[i+3]=='N') )
            {
                return i - 1080;
            }
        }
    }
    return -1;
}

static WORD XboxReadLE16( const BYTE* Data )
{
    return (WORD)(Data[0] | (Data[1] << 8));
}

static DWORD XboxReadLE32( const BYTE* Data )
{
    return (DWORD)Data[0] | ((DWORD)Data[1] << 8) | ((DWORD)Data[2] << 16) | ((DWORD)Data[3] << 24);
}

static INT XboxTrackerModuleBytes( const BYTE* Data, INT Bytes, INT Offset )
{
    if( !Data || Offset < 0 || Offset >= Bytes )
        return 0;

    const BYTE* Module = Data + Offset;
    INT Available = Bytes - Offset;
    INT End = 0;

    if( Available >= 0xC0 && Module[0]=='I' && Module[1]=='M' && Module[2]=='P' && Module[3]=='M' )
    {
        INT Orders  = XboxReadLE16( Module + 0x20 );
        INT Ins     = XboxReadLE16( Module + 0x22 );
        INT Samples = XboxReadLE16( Module + 0x24 );
        INT Pats    = XboxReadLE16( Module + 0x26 );
        INT Table   = 0xC0 + Orders;
        INT Count   = Ins + Samples + Pats;
        if( Orders < 0 || Orders > 4096 || Count < 0 || Count > 4096 || Table + Count * 4 > Available )
            return Available;

        End = Table + Count * 4;
        const BYTE* Ptrs = Module + Table;
        INT Index = 0;
        for( INT i=0; i<Ins; i++, Index++ )
        {
            DWORD Ptr = XboxReadLE32( Ptrs + Index * 4 );
            if( Ptr && Ptr < (DWORD)Available )
                End = Max<INT>( End, Min<INT>( Available, (INT)Ptr + 554 ) );
        }
        for( INT i=0; i<Samples; i++, Index++ )
        {
            DWORD Ptr = XboxReadLE32( Ptrs + Index * 4 );
            if( Ptr && Ptr + 0x50 <= (DWORD)Available )
            {
                DWORD Length = XboxReadLE32( Module + Ptr + 0x30 );
                DWORD DataPtr = XboxReadLE32( Module + Ptr + 0x48 );
                End = Max<INT>( End, (INT)Ptr + 0x50 );
                if( DataPtr && DataPtr < (DWORD)Available )
                    End = Max<INT>( End, Min<INT>( Available, (INT)(DataPtr + Length) ) );
            }
        }
        for( INT i=0; i<Pats; i++, Index++ )
        {
            DWORD Ptr = XboxReadLE32( Ptrs + Index * 4 );
            if( Ptr && Ptr + 8 <= (DWORD)Available )
            {
                INT PackedLen = XboxReadLE16( Module + Ptr );
                End = Max<INT>( End, Min<INT>( Available, (INT)Ptr + 8 + PackedLen ) );
            }
        }
        return Clamp<INT>( End, 0, Available );
    }

    if( Available >= 0x60 && Available >= 48 && Module[44]=='S' && Module[45]=='C' && Module[46]=='R' && Module[47]=='M' )
    {
        INT Orders  = XboxReadLE16( Module + 0x20 );
        INT Ins     = XboxReadLE16( Module + 0x22 );
        INT Pats    = XboxReadLE16( Module + 0x24 );
        INT Table   = 0x60 + Orders + (Orders & 1);
        INT Count   = Ins + Pats;
        if( Orders < 0 || Orders > 4096 || Count < 0 || Count > 4096 || Table + Count * 2 > Available )
            return Available;

        End = Table + Count * 2;
        const BYTE* Ptrs = Module + Table;
        INT Index = 0;
        for( INT i=0; i<Ins; i++, Index++ )
        {
            DWORD Ptr = (DWORD)XboxReadLE16( Ptrs + Index * 2 ) << 4;
            if( Ptr && Ptr + 0x50 <= (DWORD)Available )
            {
                DWORD Length = XboxReadLE32( Module + Ptr + 0x10 );
                DWORD DataPtr = ((DWORD)Module[Ptr + 0x0D] << 20) | ((DWORD)Module[Ptr + 0x0F] << 12) | ((DWORD)Module[Ptr + 0x0E] << 4);
                End = Max<INT>( End, (INT)Ptr + 0x50 );
                if( DataPtr && DataPtr < (DWORD)Available )
                    End = Max<INT>( End, Min<INT>( Available, (INT)(DataPtr + Length) ) );
            }
        }
        for( INT i=0; i<Pats; i++, Index++ )
        {
            DWORD Ptr = (DWORD)XboxReadLE16( Ptrs + Index * 2 ) << 4;
            if( Ptr && Ptr + 2 <= (DWORD)Available )
            {
                INT PackedLen = XboxReadLE16( Module + Ptr );
                End = Max<INT>( End, Min<INT>( Available, (INT)Ptr + 2 + PackedLen ) );
            }
        }
        return Clamp<INT>( End, 0, Available );
    }

    return Available;
}

static BYTE* XboxLoadMusicPackageModule( const TCHAR* MusicName, INT& OutBytes, INT& OutOffset )
{
    OutBytes = 0;
    OutOffset = 0;
    if( !MusicName || !MusicName[0] )
        return NULL;

    char Path[256];
    appSprintf( Path, "D:\\Music\\%s.umx", TCHAR_TO_ANSI(MusicName) );

    HANDLE File = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
        return NULL;

    DWORD FileBytes = GetFileSize( File, NULL );
    if( FileBytes < 4 || FileBytes > 16 * 1024 * 1024 )
    {
        CloseHandle( File );
        return NULL;
    }

    BYTE* Buffer = (BYTE*)appMalloc( FileBytes, TEXT("XboxMusicUMX") );
    if( !Buffer )
    {
        CloseHandle( File );
        return NULL;
    }

    DWORD Read = 0;
    UBOOL Ok = ReadFile( File, Buffer, FileBytes, &Read, NULL ) && Read == FileBytes;
    CloseHandle( File );
    if( !Ok )
    {
        appFree( Buffer );
        return NULL;
    }

    INT Offset = XboxFindEmbeddedTrackerModule( Buffer, (INT)FileBytes );
    if( Offset < 0 || Offset >= (INT)FileBytes )
    {
        appFree( Buffer );
        return NULL;
    }

    OutOffset = Offset;
    OutBytes = XboxTrackerModuleBytes( Buffer, (INT)FileBytes, Offset );
    if( OutBytes <= 0 )
        OutBytes = (INT)FileBytes - Offset;
    return Buffer;
}

struct FXboxXmpFile
{
    HANDLE File;
    DWORD  BaseOffset;
    DWORD  DataBytes;
};

static unsigned long XboxXmpRead( void* Dest, unsigned long Len, unsigned long Count, void* Priv )
{
    FXboxXmpFile* XFile = (FXboxXmpFile*)Priv;
    if( !Dest || !XFile || !XFile->File || Len == 0 || Count == 0 )
        return 0;

    DWORD BytesToRead = (DWORD)(Len * Count);
    DWORD BytesRead = 0;
    if( !ReadFile( XFile->File, Dest, BytesToRead, &BytesRead, NULL ) )
        return 0;
    return BytesRead / Len;
}

static int XboxXmpSeek( void* Priv, long Offset, int Whence )
{
    FXboxXmpFile* XFile = (FXboxXmpFile*)Priv;
    if( !XFile || !XFile->File )
        return -1;

    LONG Target = Offset;
    DWORD Method = FILE_BEGIN;
    if( Whence == SEEK_SET )
        Target = (LONG)(XFile->BaseOffset + Offset);
    else if( Whence == SEEK_CUR )
        Method = FILE_CURRENT;
    else if( Whence == SEEK_END )
        Target = (LONG)(XFile->BaseOffset + XFile->DataBytes + Offset);
    else
        return -1;

    SetLastError( ERROR_SUCCESS );
    DWORD Pos = SetFilePointer( XFile->File, Target, NULL, Method );
    return (Pos == INVALID_SET_FILE_POINTER && GetLastError() != ERROR_SUCCESS) ? -1 : 0;
}

static long XboxXmpTell( void* Priv )
{
    FXboxXmpFile* XFile = (FXboxXmpFile*)Priv;
    if( !XFile || !XFile->File )
        return -1;

    SetLastError( ERROR_SUCCESS );
    DWORD Pos = SetFilePointer( XFile->File, 0, NULL, FILE_CURRENT );
    if( Pos == INVALID_SET_FILE_POINTER && GetLastError() != ERROR_SUCCESS )
        return -1;
    return (Pos < XFile->BaseOffset) ? 0 : (long)(Pos - XFile->BaseOffset);
}

static int XboxXmpClose( void* Priv )
{
    FXboxXmpFile* XFile = (FXboxXmpFile*)Priv;
    if( XFile )
    {
        if( XFile->File )
            CloseHandle( XFile->File );
        appFree( XFile );
    }
    return 0;
}

static int XboxXmpLoadModuleFromXboxFile( xmp_context Context, const char* Path, INT& OutOffset, INT& OutBytes )
{
    OutOffset = 0;
    OutBytes = 0;

    HANDLE File = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
        return -XMP_ERROR_SYSTEM;

    DWORD FileBytes = GetFileSize( File, NULL );
    if( FileBytes < 4 || FileBytes > 16 * 1024 * 1024 )
    {
        CloseHandle( File );
        return -XMP_ERROR_LOAD;
    }

    DWORD ScanBytes = Min<DWORD>( FileBytes, 256 * 1024 );
    BYTE* Scan = (BYTE*)appMalloc( ScanBytes, TEXT("XboxMusicScan") );
    if( !Scan )
    {
        CloseHandle( File );
        return -XMP_ERROR_SYSTEM;
    }
    DWORD Read = 0;
    if( !ReadFile( File, Scan, ScanBytes, &Read, NULL ) || Read != ScanBytes )
    {
        appFree( Scan );
        CloseHandle( File );
        return -XMP_ERROR_SYSTEM;
    }

    INT ModuleOffset = XboxFindEmbeddedTrackerModule( Scan, (INT)ScanBytes );
    appFree( Scan );
    if( ModuleOffset < 0 || (DWORD)ModuleOffset >= FileBytes )
    {
        CloseHandle( File );
        return -XMP_ERROR_FORMAT;
    }

    FXboxXmpFile* XFile = (FXboxXmpFile*)appMalloc( sizeof(FXboxXmpFile), TEXT("XboxXmpFile") );
    if( !XFile )
    {
        CloseHandle( File );
        return -XMP_ERROR_SYSTEM;
    }
    XFile->File       = File;
    XFile->BaseOffset = (DWORD)ModuleOffset;
    XFile->DataBytes  = FileBytes - XFile->BaseOffset;
    OutOffset         = ModuleOffset;
    OutBytes          = (INT)XFile->DataBytes;

    SetFilePointer( File, XFile->BaseOffset, NULL, FILE_BEGIN );

    struct xmp_callbacks Callbacks;
    appMemzero( &Callbacks, sizeof(Callbacks) );
    Callbacks.read_func  = XboxXmpRead;
    Callbacks.seek_func  = XboxXmpSeek;
    Callbacks.tell_func  = XboxXmpTell;
    Callbacks.close_func = XboxXmpClose;
    return xmp_load_module_from_callbacks( Context, XFile, Callbacks );
}

static int XboxXmpLoadWholeXboxFile( xmp_context Context, const char* Path, INT& OutBytes )
{
    OutBytes = 0;

    HANDLE File = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if( File == INVALID_HANDLE_VALUE )
        return -XMP_ERROR_SYSTEM;

    DWORD FileBytes = GetFileSize( File, NULL );
    if( FileBytes < 4 || FileBytes > 16 * 1024 * 1024 )
    {
        CloseHandle( File );
        return -XMP_ERROR_LOAD;
    }

    FXboxXmpFile* XFile = (FXboxXmpFile*)appMalloc( sizeof(FXboxXmpFile), TEXT("XboxXmpFile") );
    if( !XFile )
    {
        CloseHandle( File );
        return -XMP_ERROR_SYSTEM;
    }
    XFile->File       = File;
    XFile->BaseOffset = 0;
    XFile->DataBytes  = FileBytes;
    OutBytes          = (INT)FileBytes;

    SetFilePointer( File, 0, NULL, FILE_BEGIN );

    struct xmp_callbacks Callbacks;
    appMemzero( &Callbacks, sizeof(Callbacks) );
    Callbacks.read_func  = XboxXmpRead;
    Callbacks.seek_func  = XboxXmpSeek;
    Callbacks.tell_func  = XboxXmpTell;
    Callbacks.close_func = XboxXmpClose;
    return xmp_load_module_from_callbacks( Context, XFile, Callbacks );
}
#endif

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
    IDirectSoundStream* MusicStream;
    XFileMediaObject* MusicSource;
    HANDLE          MusicFile;
    XBOXADPCMWAVEFORMAT MusicNativeFormat;
    VOID*           MusicSourceBuffer;
    DWORD           MusicSourceLength;
    DWORD           MusicSourceProgress;
    DWORD           MusicDataOffset;
    DWORD           MusicPacketBytes;
    IDirectSoundBuffer* ToneSmokeBuffer;
    IDirectSoundStream* ToneSmokeStream;
    SHORT*          ToneSmokeStreamData;
    DWORD           ToneSmokePacketStatus[ToneSmokeStreamPackets];
    INT             ToneSmokePacketCursor;
    UMusic*         CurrentMusic;
    BYTE            CurrentSection;
    BYTE            CurrentCDTrack;
    DWORD           MusicPacketStatus[XboxMusicStreamPackets];
    DWORD           MusicPacketsSubmitted;
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
        MusicStream      = NULL;
        MusicSource      = NULL;
        MusicFile        = INVALID_HANDLE_VALUE;
        appMemzero( &MusicNativeFormat, sizeof(MusicNativeFormat) );
        MusicSourceBuffer = NULL;
        MusicSourceLength = 0;
        MusicSourceProgress = 0;
        MusicDataOffset = 0;
        MusicPacketBytes = XboxMusicStreamPacketBytes;
        ToneSmokeBuffer  = NULL;
        ToneSmokeStream  = NULL;
        ToneSmokeStreamData = NULL;
        appMemzero( ToneSmokePacketStatus, sizeof(ToneSmokePacketStatus) );
        ToneSmokePacketCursor = 0;
        CurrentMusic     = NULL;
        CurrentSection   = 255;
        CurrentCDTrack   = 255;
        appMemzero( MusicPacketStatus, sizeof(MusicPacketStatus) );
        MusicPacketsSubmitted = 0;
        MusicPlaying     = 0;
        MusicPaused      = 0;
        MusicFailed      = 0;
        SuppressEffects  = 0;
    }

    UBOOL Init()
    {
        guard(UXboxAudioDevice::Init);

        GXboxAudioToneSmokeState = 10;
        HRESULT hr = DirectSoundCreate( NULL, &DirectSound, NULL );
        if( FAILED(hr) || !DirectSound )
        {
            GXboxAudioToneSmokeState = 901;
            GXboxLog.Write( "XboxAudio: DirectSoundCreate failed hr=0x%08X", (DWORD)hr );
            DirectSound = NULL;
            return 0;
        }
        GXboxAudioToneSmokeState = 20;

        DSEFFECTIMAGELOC EffectLoc;
        appMemzero( &EffectLoc, sizeof(EffectLoc) );
        EffectLoc.dwI3DL2ReverbIndex = 0;
        EffectLoc.dwCrosstalkIndex   = 1;
        hr = XAudioDownloadEffectsImage(
            "D:\\Media\\dsstdfx.bin",
            &EffectLoc,
            XAUDIO_DOWNLOADFX_EXTERNFILE,
            NULL );
        if( FAILED(hr) )
        {
            GXboxAudioToneSmokeState = 906;
            GXboxLog.Write( "XboxAudio: XAudioDownloadEffectsImage failed hr=0x%08X", (DWORD)hr );
        }
        else
        {
            GXboxAudioToneSmokeState = 25;
            GXboxLog.Write( "XboxAudio: standard DSP image loaded" );
        }

        USound::Audio = this;
        UMusic::Audio = this;

        MusicVolume = 255;
        SoundVolume = 255;
        LoadConfig();
        MusicVolume = Clamp<INT>( MusicVolume, 0, 255 );
        SoundVolume = Clamp<INT>( SoundVolume, 0, 255 );

        MusicContext = NULL;
        GXboxAudioToneSmokeState = 30;

        GXboxLog.Write( "XboxAudio: DirectSound initialized musicCtx=native-only musicVol=%d soundVol=%d",
            MusicVolume, SoundVolume );
        StartToneSmokeIfRequested();
        return 1;

        unguard;
    }

    void Destroy()
    {
        guard(UXboxAudioDevice::Destroy);

        USound::Audio = NULL;
        UMusic::Audio = NULL;
        StopMusic();
#if XBOX_ENABLE_UMX_MUSIC
        if( MusicContext )
        {
            xmp_free_context( MusicContext );
            MusicContext = NULL;
        }
#endif
        if( ToneSmokeBuffer )
        {
            ToneSmokeBuffer->Stop();
            ToneSmokeBuffer->Release();
            ToneSmokeBuffer = NULL;
        }
        if( ToneSmokeStream )
        {
            ToneSmokeStream->FlushEx( 0, DSSTREAMFLUSHEX_ASYNC );
            ToneSmokeStream->Release();
            ToneSmokeStream = NULL;
        }
        if( ToneSmokeStreamData )
        {
            appFree( ToneSmokeStreamData );
            ToneSmokeStreamData = NULL;
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

    UBOOL IsFrontendOrMenuMusic()
    {
        if( SuppressEffects )
            return 1;
        if( Viewport && Viewport->Actor && Viewport->Actor->XLevel && Viewport->Actor->XLevel->GetOuter() )
        {
            const TCHAR* LevelPackage = Viewport->Actor->XLevel->GetOuter()->GetName();
            if( appStricmp( LevelPackage, TEXT("CityIntro") ) == 0 )
                return 1;
        }
        return 0;
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
        if( ParseCommand( &Cmd, TEXT("XAUDIOSTARTNATIVE") ) )
        {
            TCHAR MusicName[64];
            appMemzero( MusicName, sizeof(MusicName) );
            if( !ParseToken( Cmd, MusicName, ARRAY_COUNT(MusicName), 0 ) || !MusicName[0] )
            {
                GXboxLog.Write( "XboxAudio: XAUDIOSTARTNATIVE missing music name" );
                return 1;
            }
            BYTE Section = 0;
            TCHAR SectionText[16];
            appMemzero( SectionText, sizeof(SectionText) );
            if( ParseToken( Cmd, SectionText, ARRAY_COUNT(SectionText), 0 ) && SectionText[0] )
                Section = (BYTE)Clamp<INT>( appAtoi(SectionText), 0, 255 );

            StopMusic();
            CurrentSection = Section;
            CurrentCDTrack = 255;
            MusicPaused    = 0;
            MusicFailed    = 0;
            if( !StartNativeMusicByName( TCHAR_TO_ANSI(MusicName), Section, NULL ) )
            {
                GXboxLog.Write( "XboxAudio: native music command missing/failed name=%s section=%d; music silent",
                    TCHAR_TO_ANSI(MusicName), Section );
                GXboxAudioMusicStreamState = 905;
                MusicFailed = 1;
            }
            return 1;
        }
        if( ParseCommand( &Cmd, TEXT("XAUDIOSETMUSICVOLUME") ) )
        {
            MusicVolume = Clamp<INT>( appAtoi(Cmd), 0, 255 );
            if( MusicStream )
                MusicStream->SetVolume( XboxVolumeToDS( (FLOAT)MusicVolume / 255.0f ) );
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
        ServiceToneSmokeStream();
        if( DirectSound )
            DirectSoundDoWork();

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
        if( Sound->Handle )
            return;
        if( Sound->Data.Num() <= 0 )
            return;

        FWaveModInfo WaveInfo;
        if( !WaveInfo.ReadWaveInfo( Sound->Data ) )
        {
            if( FailedSounds < 32 )
                GXboxLog.Write( "XboxAudio: RegisterSound rejected non-wave %s", TCHAR_TO_ANSI(Sound->GetName()) );
            FailedSounds++;
            Sound->Data.Unload();
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
            Sound->Data.Unload();
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
            Sound->Data.Unload();
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
            Sound->Data.Unload();
            return;
        }

        appMemcpy( Lock1, WaveInfo.SampleDataStart, Size1 );
        if( Lock2 && Size2 )
            appMemcpy( Lock2, WaveInfo.SampleDataStart + Size1, Size2 );
        Buffer->Unlock( Lock1, Size1, Lock2, Size2 );

        Sound->Handle = Buffer;
        XboxSoundMetaSet( Sound, wfx.nSamplesPerSec, WaveInfo.SampleDataSize );
        Sound->Data.Unload();
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
            XboxSoundMetaClear( Sound );
            Sound->Data.Unload();
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

        DWORD BaseRate = XboxSoundMetaGetBaseRate( Sound );

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
    void StartToneSmokeIfRequested()
    {
        guard(UXboxAudioDevice::StartToneSmokeIfRequested);

        INT SmokeMode = XboxAudioToneSmokeMode();
        if( !DirectSound || SmokeMode <= 0 )
            return;
        GXboxAudioToneSmokeState = 40;
        if( SmokeMode < 2 )
        {
            GXboxLog.Write( "XboxAudio: tone smoke overlay-only; use Enabled=2 for tone generation" );
            return;
        }

        enum { ToneRate = 22050, ToneSamples = 4096, ToneChannels = 2, ToneBytes = ToneSamples * ToneChannels * 2 };

        WAVEFORMATEX wfx;
        appMemzero( &wfx, sizeof(wfx) );
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = ToneChannels;
        wfx.nSamplesPerSec  = ToneRate;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = ToneChannels * 2;
        wfx.nAvgBytesPerSec = ToneRate * wfx.nBlockAlign;

        DSMIXBINVOLUMEPAIR MixBinVolumes[2];
        MixBinVolumes[0].dwMixBin = DSMIXBIN_FRONT_LEFT;
        MixBinVolumes[0].lVolume  = DSBVOLUME_MAX;
        MixBinVolumes[1].dwMixBin = DSMIXBIN_FRONT_RIGHT;
        MixBinVolumes[1].lVolume  = DSBVOLUME_MAX;
        DSMIXBINS MixBins;
        MixBins.dwMixBinCount = 2;
        MixBins.lpMixBinVolumePairs = MixBinVolumes;

        DSBUFFERDESC Desc;
        appMemzero( &Desc, sizeof(Desc) );
        Desc.dwSize        = sizeof(Desc);
        Desc.dwFlags       = DSBCAPS_CTRLVOLUME | DSBCAPS_LOCDEFER;
        Desc.dwBufferBytes = 0;
        Desc.lpwfxFormat   = &wfx;
        Desc.lpMixBins     = &MixBins;

        HRESULT hr = DirectSoundCreateBuffer( &Desc, &ToneSmokeBuffer );
        if( FAILED(hr) || !ToneSmokeBuffer )
        {
            GXboxAudioToneSmokeState = 903;
            GXboxLog.Write( "XboxAudio: tone smoke CreateSoundBuffer failed hr=0x%08X", (DWORD)hr );
            return;
        }
        GXboxAudioToneSmokeState = 50;

        SHORT* ToneSmokeSamples = (SHORT*)appMalloc( ToneBytes, TEXT("XboxToneSmoke") );
        if( !ToneSmokeSamples )
        {
            GXboxAudioToneSmokeState = 904;
            GXboxLog.Write( "XboxAudio: tone smoke sample alloc failed bytes=%d availKB=%u", ToneBytes, (unsigned)XboxAudioAvailPhysKB() );
            ToneSmokeBuffer->Release();
            ToneSmokeBuffer = NULL;
            return;
        }
        GXboxAudioToneSmokeState = 60;

        SHORT* Samples = ToneSmokeSamples;
        DWORD SampleCount = ToneSamples;
        for( DWORD i=0; i<SampleCount; i++ )
        {
            INT Phase = (INT)((i * 440 * 64) / ToneRate) & 63;
            SHORT Sample = (Phase < 32) ? 12000 : -12000;
            Samples[i * 2 + 0] = Sample;
            Samples[i * 2 + 1] = Sample;
        }

        hr = ToneSmokeBuffer->SetBufferData( ToneSmokeSamples, ToneBytes );
        appFree( ToneSmokeSamples );
        if( FAILED(hr) )
        {
            GXboxAudioToneSmokeState = 907;
            GXboxLog.Write( "XboxAudio: tone smoke SetBufferData failed hr=0x%08X", (DWORD)hr );
            ToneSmokeBuffer->Release();
            ToneSmokeBuffer = NULL;
            return;
        }

        ToneSmokeBuffer->SetMixBins( &MixBins );
        ToneSmokeBuffer->SetVolume( DSBVOLUME_MAX );
        ToneSmokeBuffer->SetCurrentPosition( 0 );
        hr = ToneSmokeBuffer->Play( 0, 0, DSBPLAY_LOOPING );
        DirectSound->CommitDeferredSettings();
        DirectSoundDoWork();
        DWORD Status = 0;
        if( SUCCEEDED(hr) && SUCCEEDED(ToneSmokeBuffer->GetStatus( &Status )) )
            GXboxAudioToneSmokeState = (Status & DSBSTATUS_PLAYING) ? 71 : 72;
        else
            GXboxAudioToneSmokeState = FAILED(hr) ? 905 : 70;
        GXboxLog.Write( "XboxAudio: tone smoke Play hr=0x%08X status=0x%08X bytes=%d", (DWORD)hr, Status, ToneBytes );
        StartToneSmokeStream();

        unguard;
    }

    void StartToneSmokeStream()
    {
        guard(UXboxAudioDevice::StartToneSmokeStream);

        if( !DirectSound || ToneSmokeStream )
            return;

        WAVEFORMATEX wfx;
        appMemzero( &wfx, sizeof(wfx) );
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = ToneSmokeStreamChannels;
        wfx.nSamplesPerSec  = ToneSmokeStreamRate;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = ToneSmokeStreamChannels * 2;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

        DSMIXBINVOLUMEPAIR MixBinVolumes[2];
        MixBinVolumes[0].dwMixBin = DSMIXBIN_FRONT_LEFT;
        MixBinVolumes[0].lVolume  = DSBVOLUME_MAX;
        MixBinVolumes[1].dwMixBin = DSMIXBIN_FRONT_RIGHT;
        MixBinVolumes[1].lVolume  = DSBVOLUME_MAX;
        DSMIXBINS MixBins;
        MixBins.dwMixBinCount = 2;
        MixBins.lpMixBinVolumePairs = MixBinVolumes;

        DSSTREAMDESC Desc;
        appMemzero( &Desc, sizeof(Desc) );
        Desc.dwMaxAttachedPackets = ToneSmokeStreamPackets;
        Desc.lpwfxFormat          = &wfx;
        Desc.lpMixBins            = &MixBins;

        HRESULT hr = DirectSoundCreateStream( &Desc, &ToneSmokeStream );
        if( FAILED(hr) || !ToneSmokeStream )
        {
            GXboxAudioToneSmokeState = 908;
            GXboxLog.Write( "XboxAudio: tone stream CreateStream failed hr=0x%08X", (DWORD)hr );
            return;
        }

        if( !ToneSmokeStreamData )
        {
            ToneSmokeStreamData = (SHORT*)appMalloc( ToneSmokeStreamBytes * ToneSmokeStreamPackets, TEXT("XboxToneSmokeStream") );
            if( !ToneSmokeStreamData )
            {
                GXboxAudioToneSmokeState = 910;
                GXboxLog.Write( "XboxAudio: tone stream sample alloc failed bytes=%d availKB=%u",
                    ToneSmokeStreamBytes * ToneSmokeStreamPackets, (unsigned)XboxAudioAvailPhysKB() );
                ToneSmokeStream->Release();
                ToneSmokeStream = NULL;
                return;
            }
        }

        ToneSmokeStream->SetVolume( DSBVOLUME_MAX );
        ToneSmokeStream->SetMixBins( &MixBins );
        for( INT p=0; p<ToneSmokeStreamPackets; p++ )
            ToneSmokePacketStatus[p] = XMEDIAPACKET_STATUS_SUCCESS;
        ToneSmokePacketCursor = 0;

        for( INT Packet=0; Packet<ToneSmokeStreamPackets; Packet++ )
            SubmitToneSmokePacket( Packet );

        ToneSmokeStream->Pause( DSSTREAMPAUSE_RESUME );
        DirectSoundDoWork();
        GXboxAudioToneSmokeState = 81;
        GXboxLog.Write( "XboxAudio: tone stream started packets=%d bytes=%d", ToneSmokeStreamPackets, ToneSmokeStreamBytes );

        unguard;
    }

    void SubmitToneSmokePacket( INT Packet )
    {
        guard(UXboxAudioDevice::SubmitToneSmokePacket);

        if( !ToneSmokeStream || !ToneSmokeStreamData || Packet < 0 || Packet >= ToneSmokeStreamPackets )
            return;

        SHORT* Samples = ToneSmokeStreamData + Packet * ToneSmokeStreamSamples * ToneSmokeStreamChannels;
        for( INT i=0; i<ToneSmokeStreamSamples; i++ )
        {
            INT GlobalSample = ToneSmokePacketCursor + i;
            INT Phase = (GlobalSample * 440 * 64 / ToneSmokeStreamRate) & 63;
            SHORT Sample = (Phase < 32) ? 12000 : -12000;
            Samples[i * 2 + 0] = Sample;
            Samples[i * 2 + 1] = Sample;
        }
        ToneSmokePacketCursor += ToneSmokeStreamSamples;

        XMEDIAPACKET Xmp;
        appMemzero( &Xmp, sizeof(Xmp) );
        Xmp.pvBuffer  = Samples;
        Xmp.dwMaxSize = ToneSmokeStreamBytes;
        Xmp.pdwStatus = &ToneSmokePacketStatus[Packet];
        ToneSmokePacketStatus[Packet] = XMEDIAPACKET_STATUS_PENDING;
        HRESULT hr = ToneSmokeStream->Process( &Xmp, NULL );
        if( FAILED(hr) )
        {
            ToneSmokePacketStatus[Packet] = XMEDIAPACKET_STATUS_FAILURE;
            GXboxAudioToneSmokeState = 909;
            GXboxLog.Write( "XboxAudio: tone stream packet failed p=%d hr=0x%08X", Packet, (DWORD)hr );
        }

        unguard;
    }

    void ServiceToneSmokeStream()
    {
        guard(UXboxAudioDevice::ServiceToneSmokeStream);

        if( !ToneSmokeStream )
            return;

        INT Submitted = 0;
        for( INT Packet=0; Packet<ToneSmokeStreamPackets; Packet++ )
        {
            if( ToneSmokePacketStatus[Packet] != XMEDIAPACKET_STATUS_PENDING )
            {
                SubmitToneSmokePacket( Packet );
                Submitted++;
            }
        }
        if( Submitted )
            GXboxAudioToneSmokeState = 82;

        unguard;
    }

    enum
    {
        MusicRate        = 22050,
        MusicChannels    = 2,
        MusicBits        = 16,
        MusicBlockAlign  = 4
    };

#if XBOX_ENABLE_UMX_MUSIC
    UBOOL ResetMusicContextForRetry( const char* Reason )
    {
        if( MusicContext )
            xmp_free_context( MusicContext );
        MusicContext = xmp_create_context();
        GXboxLog.Write( "XboxAudio: reset music xmp context after %s result=%s",
            Reason ? Reason : "load-fail", MusicContext ? "OK" : "NULL" );
        return MusicContext != NULL;
    }
#endif

    static DWORD FourCC( const char* Text )
    {
        return ((DWORD)(BYTE)Text[0]) | ((DWORD)(BYTE)Text[1] << 8) | ((DWORD)(BYTE)Text[2] << 16) | ((DWORD)(BYTE)Text[3] << 24);
    }

    UBOOL ReadMusicFileExact( VOID* Dest, DWORD Bytes )
    {
        DWORD Done = 0;
        return MusicFile != INVALID_HANDLE_VALUE && ReadFile( MusicFile, Dest, Bytes, &Done, NULL ) && Done == Bytes;
    }

    UBOOL ReadNativeMusicHeader( const char* Path )
    {
        guard(UXboxAudioDevice::ReadNativeMusicHeader);

        struct FChunk
        {
            DWORD Id;
            DWORD Size;
        };

        DWORD Riff[3];
        if( !ReadMusicFileExact( Riff, sizeof(Riff) ) || Riff[0] != FourCC("RIFF") || Riff[2] != FourCC("WAVE") )
        {
            GXboxLog.Write( "XboxAudio: native music bad RIFF %s", Path );
            return 0;
        }

        UBOOL bHaveFmt = 0;
        UBOOL bHaveData = 0;
        while( !bHaveData )
        {
            FChunk Chunk;
            if( !ReadMusicFileExact( &Chunk, sizeof(Chunk) ) )
                break;

            DWORD Payload = Chunk.Size;
            DWORD ChunkData = SetFilePointer( MusicFile, 0, NULL, FILE_CURRENT );
            if( ChunkData == 0xFFFFFFFF )
                return 0;
            DWORD Next = ChunkData + Payload + (Payload & 1);
            if( Chunk.Id == FourCC("fmt ") )
            {
                BYTE FormatBytes[64];
                if( Payload < sizeof(WAVEFORMATEX) || Payload > sizeof(FormatBytes) )
                {
                    GXboxLog.Write( "XboxAudio: native music bad fmt size %s size=%u", Path, (unsigned)Payload );
                    return 0;
                }
                appMemzero( FormatBytes, sizeof(FormatBytes) );
                if( !ReadMusicFileExact( FormatBytes, Payload ) )
                    return 0;
                appMemzero( &MusicNativeFormat, sizeof(MusicNativeFormat) );
                DWORD CopyBytes = Payload < sizeof(MusicNativeFormat) ? Payload : sizeof(MusicNativeFormat);
                appMemcpy( &MusicNativeFormat, FormatBytes, CopyBytes );
                bHaveFmt = 1;
            }
            else if( Chunk.Id == FourCC("data") )
            {
                MusicDataOffset = ChunkData;
                MusicSourceLength = Payload;
                bHaveData = 1;
            }

            if( SetFilePointer( MusicFile, Next, NULL, FILE_BEGIN ) == 0xFFFFFFFF )
                return 0;
        }

        if( !bHaveFmt || !bHaveData || MusicSourceLength == 0 || MusicNativeFormat.wfx.wFormatTag != WAVE_FORMAT_XBOX_ADPCM )
        {
            GXboxLog.Write( "XboxAudio: native music unsupported %s fmt=%d data=%u",
                Path, MusicNativeFormat.wfx.wFormatTag, (unsigned)MusicSourceLength );
            return 0;
        }

        if( MusicNativeFormat.wSamplesPerBlock != 64 || MusicNativeFormat.wfx.nBlockAlign == 0 )
        {
            GXboxLog.Write( "XboxAudio: native music bad ADPCM block %s block=%u samples=%u",
                Path, (unsigned)MusicNativeFormat.wfx.nBlockAlign, (unsigned)MusicNativeFormat.wSamplesPerBlock );
            return 0;
        }

        MusicPacketBytes = (XboxMusicStreamPacketBytes / MusicNativeFormat.wfx.nBlockAlign) * MusicNativeFormat.wfx.nBlockAlign;
        if( MusicPacketBytes < MusicNativeFormat.wfx.nBlockAlign )
            MusicPacketBytes = MusicNativeFormat.wfx.nBlockAlign;
        SetFilePointer( MusicFile, MusicDataOffset, NULL, FILE_BEGIN );
        MusicSourceProgress = 0;
        return 1;

        unguard;
    }

    UBOOL FillNativeMusicPacket( INT Packet )
    {
        guard(UXboxAudioDevice::FillNativeMusicPacket);

        if( MusicFile == INVALID_HANDLE_VALUE || !MusicSourceBuffer || Packet < 0 || Packet >= XboxMusicStreamPackets )
            return 0;

        BYTE* Dest = (BYTE*)MusicSourceBuffer + Packet * XboxMusicStreamPacketBytes;
        DWORD Total = 0;
        while( Total < MusicPacketBytes )
        {
            DWORD Used = 0;
            DWORD Want = MusicPacketBytes - Total;
            if( MusicSourceLength && MusicSourceProgress + Want > MusicSourceLength )
                Want = MusicSourceLength - MusicSourceProgress;
            if( Want == 0 )
            {
                SetFilePointer( MusicFile, MusicDataOffset, NULL, FILE_BEGIN );
                MusicSourceProgress = 0;
                continue;
            }

            if( !ReadFile( MusicFile, Dest + Total, Want, &Used, NULL ) )
            {
                GXboxLog.Write( "XboxAudio: native music file read failed p=%d err=%u", Packet, (unsigned)GetLastError() );
                return 0;
            }
            if( Used == 0 )
            {
                GXboxLog.Write( "XboxAudio: native music short read p=%d progress=%u len=%u",
                    Packet, (unsigned)MusicSourceProgress, (unsigned)MusicSourceLength );
                return 0;
            }
            Total += Used;
            MusicSourceProgress += Used;

            if( Used < Want || (MusicSourceLength && MusicSourceProgress >= MusicSourceLength) )
            {
                SetFilePointer( MusicFile, MusicDataOffset, NULL, FILE_BEGIN );
                MusicSourceProgress = 0;
            }
        }

        return 1;
        unguard;
    }

    UBOOL StartNativeMusicByName( const char* MusicName, BYTE Section, UMusic* Music )
    {
        guard(UXboxAudioDevice::StartNativeMusicByName);

        if( !MusicName || !MusicName[0] )
            return 0;

        char Path[256];
        appSprintf( Path, "D:\\MusicXbox\\%s.wav", MusicName );

        GXboxLog.Write( "XboxAudio: native music probe %s availKB=%u", Path, (unsigned)XboxAudioAvailPhysKB() );
        MusicFile = CreateFileA( Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
        if( MusicFile == INVALID_HANDLE_VALUE )
        {
            GXboxLog.Write( "XboxAudio: native music missing %s err=%u; music silent", Path, (unsigned)GetLastError() );
            return 0;
        }

        if( !ReadNativeMusicHeader( Path ) )
        {
            CloseHandle( MusicFile );
            MusicFile = INVALID_HANDLE_VALUE;
            return 0;
        }

        DSSTREAMDESC Desc;
        appMemzero( &Desc, sizeof(Desc) );
        Desc.dwMaxAttachedPackets = XboxMusicStreamPackets;
        Desc.lpwfxFormat = (LPWAVEFORMATEX)&MusicNativeFormat;
        Desc.lpMixBins = &DirectSoundDefaultMixBins_Stereo;

        HRESULT hr = DirectSoundCreateStream( &Desc, &MusicStream );
        if( FAILED(hr) || !MusicStream )
        {
            GXboxLog.Write( "XboxAudio: native music CreateStream failed %s hr=0x%08X", Path, (DWORD)hr );
            CloseHandle( MusicFile );
            MusicFile = INVALID_HANDLE_VALUE;
            MusicStream = NULL;
            return 0;
        }

        MusicSourceBuffer = XPhysicalAlloc(
            XboxMusicStreamPacketBytes * XboxMusicStreamPackets,
            MAXULONG_PTR,
            0,
            PAGE_READWRITE | PAGE_NOCACHE );
        if( !MusicSourceBuffer )
        {
            GXboxLog.Write( "XboxAudio: native music packet alloc failed %s bytes=%d availKB=%u",
                Path, XboxMusicStreamPacketBytes * XboxMusicStreamPackets, (unsigned)XboxAudioAvailPhysKB() );
            MusicStream->Release();
            MusicStream = NULL;
            CloseHandle( MusicFile );
            MusicFile = INVALID_HANDLE_VALUE;
            return 0;
        }

        for( INT Packet=0; Packet<XboxMusicStreamPackets; Packet++ )
            MusicPacketStatus[Packet] = XMEDIAPACKET_STATUS_SUCCESS;
        MusicPacketsSubmitted = 0;

        MusicStream->SetVolume( XboxVolumeToDS( (FLOAT)MusicVolume / 255.0f ) );
        for( INT Packet=0; Packet<XboxMusicStreamPackets; Packet++ )
            SubmitMusicPacket( Packet );

        MusicPlaying = 1;
        hr = MusicStream->Pause( DSSTREAMPAUSE_RESUME );
        if( FAILED(hr) )
        {
            GXboxLog.Write( "XboxAudio: native music resume failed %s hr=0x%08X", Path, (DWORD)hr );
            StopMusic();
            GXboxAudioMusicStreamState = 904;
            MusicFailed = 1;
            return 0;
        }

        DirectSound->CommitDeferredSettings();
        DirectSoundDoWork();
        if( Music )
        {
            RegisterMusic( Music );
            if( Music->Data.Num() > 0 )
            {
                INT BulkKB = Music->Data.Num() / 1024;
                Music->Data.Unload();
                GXboxLog.Write( "XboxAudio: native music unloaded UMusic bulk %s bulkKB=%d availKB=%u",
                    TCHAR_TO_ANSI(Music->GetName()), BulkKB, (unsigned)XboxAudioAvailPhysKB() );
            }
        }
        GXboxAudioMusicStreamState = 1;
        GXboxLog.Write( "XboxAudio: native music stream started %s section=%d fmt=0x%04X rate=%u ch=%u block=%u packetBytes=%d packets=%d fileBytes=%u",
            Path, Section, MusicNativeFormat.wfx.wFormatTag, (unsigned)MusicNativeFormat.wfx.nSamplesPerSec,
            (unsigned)MusicNativeFormat.wfx.nChannels, (unsigned)MusicNativeFormat.wfx.nBlockAlign,
            (int)MusicPacketBytes, XboxMusicStreamPackets, (unsigned)MusicSourceLength );
        return 1;

        unguard;
    }

    UBOOL StartNativeMusic( UMusic* Music, BYTE Section )
    {
        guard(UXboxAudioDevice::StartNativeMusic);

        if( !Music || !Music->GetName() || !Music->GetName()[0] )
            return 0;

        return StartNativeMusicByName( TCHAR_TO_ANSI(Music->GetName()), Section, Music );

        unguard;
    }

    void StartMusic( UMusic* Music, BYTE Section, BYTE CDTrack )
    {
        guard(UXboxAudioDevice::StartMusic);

        StopMusic();

        CurrentMusic   = Music;
        CurrentSection = Section;
        CurrentCDTrack = CDTrack;
        MusicPaused    = 0;
        MusicFailed    = 0;

        if( !DirectSound || !Music || Section==255 )
        {
            GXboxLog.Write( "XboxAudio: music not started ds=%s ctx=%s music=%s section=%d",
                DirectSound ? "OK" : "NULL",
                MusicContext ? "OK" : "NULL",
                Music ? TCHAR_TO_ANSI(Music->GetName()) : "None",
                Section );
            return;
        }

        if( StartNativeMusic( Music, Section ) )
            return;

        GXboxLog.Write( "XboxAudio: native music unavailable music=%s section=%d availKB=%u; UMX fallback disabled",
            TCHAR_TO_ANSI(Music->GetName()), Section, (unsigned)XboxAudioAvailPhysKB() );
        GXboxAudioMusicStreamState = 905;
        MusicFailed = 1;
        return;

#if XBOX_ENABLE_UMX_MUSIC
        char MusicPath[256];
        appSprintf( MusicPath, "D:\\Music\\%s.umx", TCHAR_TO_ANSI(Music->GetName()) );

        libxmp_xbox_reset_alloc_stats();
        INT WholeBytes = 0;
        INT CallbackOffset = 0;
        INT CallbackBytes  = 0;
        DWORD AvailBeforeKB = XboxAudioAvailPhysKB();
        int LoadResult = XboxXmpLoadWholeXboxFile( MusicContext, MusicPath, WholeBytes );
        GXboxAudioMusicLoadState = (LoadResult == 0) ? WholeBytes : LoadResult;
        GXboxLog.Write( "XboxAudio: callback loading whole UMX %s path=%s result=%d bytes=%d section=%d availKB=%u memPressure=%d",
            TCHAR_TO_ANSI(Music->GetName()), MusicPath, LoadResult, WholeBytes, Section,
            (unsigned)AvailBeforeKB, AvailBeforeKB < 8192 );

        if( LoadResult != 0 )
        {
            GXboxAudioMusicLoadState = 9100 + Min<INT>( 99, -LoadResult );
            if( !ResetMusicContextForRetry( "whole-umx-fail" ) )
            {
                GXboxAudioMusicStreamState = 901;
                MusicFailed = 1;
                return;
            }
            AvailBeforeKB = XboxAudioAvailPhysKB();
            LoadResult = XboxXmpLoadModuleFromXboxFile( MusicContext, MusicPath, CallbackOffset, CallbackBytes );
            GXboxAudioMusicLoadState = (LoadResult == 0) ? CallbackBytes : LoadResult;
            GXboxLog.Write( "XboxAudio: callback loading embedded music %s path=%s result=%d offset=%d moduleBytes=%d section=%d availKB=%u memPressure=%d",
                TCHAR_TO_ANSI(Music->GetName()), MusicPath, LoadResult, CallbackOffset, CallbackBytes, Section,
                (unsigned)AvailBeforeKB, AvailBeforeKB < 8192 );
        }

        if( LoadResult != 0 )
        {
            GXboxAudioMusicLoadState = 9200 + Min<INT>( 99, -LoadResult );
            if( !ResetMusicContextForRetry( "embedded-callback-fail" ) )
            {
                GXboxAudioMusicStreamState = 901;
                MusicFailed = 1;
                return;
            }
            AvailBeforeKB = XboxAudioAvailPhysKB();
            Music->Data.Load();
            if( Music->Data.Num() > 0 )
            {
                INT LazyOffset = XboxFindEmbeddedTrackerModule( &Music->Data(0), Music->Data.Num() );
                if( LazyOffset < 0 )
                    LazyOffset = 0;
                GXboxLog.Write( "XboxAudio: fallback loading music %s lazyBytes=%d offset=%d moduleBytes=%d section=%d availKB=%u memPressure=%d",
                    TCHAR_TO_ANSI(Music->GetName()), Music->Data.Num(), LazyOffset, Music->Data.Num() - LazyOffset, Section,
                    (unsigned)AvailBeforeKB, AvailBeforeKB < 8192 );
                LoadResult = xmp_load_module_from_memory( MusicContext, &Music->Data(0) + LazyOffset, Music->Data.Num() - LazyOffset );
                GXboxAudioMusicLoadState = (LoadResult == 0) ? (Music->Data.Num() - LazyOffset) : LoadResult;
                GXboxLog.Write( "XboxAudio: fallback memory load result=%d availAfterKB=%u memPressure=%d",
                    LoadResult, (unsigned)XboxAudioAvailPhysKB(), XboxAudioAvailPhysKB() < 8192 );
                Music->Data.Unload();
            }

            if( LoadResult != 0 )
            {
                GXboxAudioMusicLoadState = 9300 + Min<INT>( 99, -LoadResult );
                if( !ResetMusicContextForRetry( "lazy-memory-fail" ) )
                {
                    GXboxAudioMusicStreamState = 901;
                    MusicFailed = 1;
                    return;
                }
                INT PackageBytes = 0;
                INT PackageOffset = 0;
                AvailBeforeKB = XboxAudioAvailPhysKB();
                BYTE* PackageData = XboxLoadMusicPackageModule( Music->GetName(), PackageBytes, PackageOffset );
                if( PackageData )
                {
                    GXboxLog.Write( "XboxAudio: fallback loading whole music package %s bytes=%d section=%d availKB=%u memPressure=%d",
                        TCHAR_TO_ANSI(Music->GetName()), PackageBytes + PackageOffset, Section,
                        (unsigned)AvailBeforeKB, AvailBeforeKB < 8192 );
                    LoadResult = xmp_load_module_from_memory( MusicContext, PackageData, PackageBytes + PackageOffset );
                    if( LoadResult != 0 )
                    {
                        GXboxAudioMusicLoadState = 9400 + Min<INT>( 99, -LoadResult );
                        if( !ResetMusicContextForRetry( "whole-package-memory-fail" ) )
                        {
                            appFree( PackageData );
                            GXboxAudioMusicStreamState = 901;
                            MusicFailed = 1;
                            return;
                        }
                        AvailBeforeKB = XboxAudioAvailPhysKB();
                        GXboxLog.Write( "XboxAudio: fallback loading embedded music %s umxOffset=%d moduleBytes=%d section=%d availKB=%u memPressure=%d",
                            TCHAR_TO_ANSI(Music->GetName()), PackageOffset, PackageBytes, Section,
                            (unsigned)AvailBeforeKB, AvailBeforeKB < 8192 );
                        LoadResult = xmp_load_module_from_memory( MusicContext, PackageData + PackageOffset, PackageBytes );
                        if( LoadResult != 0 )
                            GXboxAudioMusicLoadState = 9500 + Min<INT>( 99, -LoadResult );
                    }
                    GXboxLog.Write( "XboxAudio: fallback package load result=%d availAfterKB=%u memPressure=%d",
                        LoadResult, (unsigned)XboxAudioAvailPhysKB(), XboxAudioAvailPhysKB() < 8192 );
                    if( LoadResult == 0 )
                        GXboxAudioMusicLoadState = PackageBytes;
                    appFree( PackageData );
                }
                else if( Music->Data.Num() <= 0 )
                {
                    GXboxLog.Write( "XboxAudio: music %s has no data availKB=%u memPressure=%d",
                        TCHAR_TO_ANSI(Music->GetName()), (unsigned)XboxAudioAvailPhysKB(), XboxAudioAvailPhysKB() < 8192 );
                }
            }

        }

        if( LoadResult != 0 )
        {
            DWORD AvailFailKB = XboxAudioAvailPhysKB();
            unsigned long XmpLive = 0;
            unsigned long XmpPeak = 0;
            unsigned long XmpTotal = 0;
            unsigned long XmpLargest = 0;
            unsigned long XmpFails = 0;
            unsigned long XmpLastFail = 0;
            unsigned long XmpLastFailAvailKB = 0;
            libxmp_xbox_get_alloc_stats( &XmpLive, &XmpPeak, &XmpTotal, &XmpLargest, &XmpFails, &XmpLastFail, &XmpLastFailAvailKB );
            GXboxLog.Write( "XboxAudio: xmp_load_module failed %s result=%d availKB=%u memPressure=%d xmpLiveKB=%u xmpPeakKB=%u xmpTotalKB=%u xmpLargestKB=%u xmpFails=%u xmpLastFailKB=%u xmpLastFailAvailKB=%u",
                TCHAR_TO_ANSI(Music->GetName()), LoadResult, (unsigned)AvailFailKB, AvailFailKB < 8192,
                (unsigned)(XmpLive / 1024), (unsigned)(XmpPeak / 1024), (unsigned)(XmpTotal / 1024), (unsigned)(XmpLargest / 1024),
                (unsigned)XmpFails, (unsigned)(XmpLastFail / 1024), (unsigned)XmpLastFailAvailKB );
            GXboxAudioMusicStreamState = 901;
            MusicFailed = 1;
            return;
        }

        int StartResult = xmp_start_player( MusicContext, MusicRate, 0 );
        if( StartResult != 0 )
        {
            GXboxLog.Write( "XboxAudio: xmp_start_player failed %s result=%d", TCHAR_TO_ANSI(Music->GetName()), StartResult );
            xmp_release_module( MusicContext );
            GXboxAudioMusicStreamState = 902;
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

        DSSTREAMDESC Desc;
        appMemzero( &Desc, sizeof(Desc) );
        Desc.dwMaxAttachedPackets = XboxMusicStreamPackets;
        Desc.lpwfxFormat          = &wfx;
        Desc.lpMixBins            = &DirectSoundDefaultMixBins_Stereo;

        HRESULT hr = DirectSoundCreateStream( &Desc, &MusicStream );
        if( FAILED(hr) || !MusicStream )
        {
            GXboxLog.Write( "XboxAudio: music CreateStream failed %s hr=0x%08X",
                TCHAR_TO_ANSI(Music->GetName()), (DWORD)hr );
            xmp_end_player( MusicContext );
            xmp_release_module( MusicContext );
            GXboxAudioMusicStreamState = 903;
            MusicFailed = 1;
            return;
        }

        for( INT Packet=0; Packet<XboxMusicStreamPackets; Packet++ )
            MusicPacketStatus[Packet] = XMEDIAPACKET_STATUS_SUCCESS;
        MusicPacketsSubmitted = 0;

        MusicStream->SetVolume( XboxVolumeToDS( (FLOAT)MusicVolume / 255.0f ) );
        for( INT Packet=0; Packet<XboxMusicStreamPackets; Packet++ )
            SubmitMusicPacket( Packet );
        MusicPlaying = 1;
        hr = MusicStream->Pause( DSSTREAMPAUSE_RESUME );
        if( FAILED(hr) )
        {
            GXboxLog.Write( "XboxAudio: music stream resume failed %s hr=0x%08X", TCHAR_TO_ANSI(Music->GetName()), (DWORD)hr );
            StopMusic();
            GXboxAudioMusicStreamState = 904;
            MusicFailed = 1;
            return;
        }

        DirectSound->CommitDeferredSettings();
        DirectSoundDoWork();
        RegisterMusic( Music );
        GXboxAudioMusicStreamState = 1;
        GXboxLog.Write( "XboxAudio: music stream started %s section=%d rate=%d packetBytes=%d packets=%d",
            TCHAR_TO_ANSI(Music->GetName()), Section, MusicRate, XboxMusicStreamPacketBytes, XboxMusicStreamPackets );
#endif

        unguard;
    }

    void StopMusic()
    {
        guard(UXboxAudioDevice::StopMusic);

        UBOOL bTrackerMusic = ( MusicFile == INVALID_HANDLE_VALUE );
        if( MusicStream )
        {
            MusicStream->FlushEx( 0, DSSTREAMFLUSHEX_ASYNC );
            MusicStream->Release();
            MusicStream = NULL;
        }
        if( MusicSource )
        {
            MusicSource->Release();
            MusicSource = NULL;
        }
        if( MusicFile != INVALID_HANDLE_VALUE )
        {
            CloseHandle( MusicFile );
            MusicFile = INVALID_HANDLE_VALUE;
        }
        if( MusicSourceBuffer )
        {
            XPhysicalFree( MusicSourceBuffer );
            MusicSourceBuffer = NULL;
        }
        MusicSourceLength = 0;
        MusicSourceProgress = 0;
        MusicDataOffset = 0;
        MusicPacketBytes = XboxMusicStreamPacketBytes;
#if XBOX_ENABLE_UMX_MUSIC
        if( bTrackerMusic && MusicContext && MusicPlaying )
        {
            xmp_end_player( MusicContext );
            xmp_release_module( MusicContext );
        }
#endif
        if( CurrentMusic )
            CurrentMusic->Handle = NULL;
        CurrentMusic = NULL;
        CurrentSection = 255;
        CurrentCDTrack = 255;
        appMemzero( MusicPacketStatus, sizeof(MusicPacketStatus) );
        MusicPacketsSubmitted = 0;
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
        if( MusicStream && MusicPlaying )
        {
            HRESULT hr = MusicStream->Pause( bPaused ? DSSTREAMPAUSE_PAUSE : DSSTREAMPAUSE_RESUME );
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

    void SubmitMusicPacket( INT Packet )
    {
        guard(UXboxAudioDevice::SubmitMusicPacket);

        if( !MusicStream || Packet < 0 || Packet >= XboxMusicStreamPackets )
            return;

        VOID* PacketData = NULL;
        DWORD SubmitBytes = XboxMusicStreamPacketBytes;
        if( MusicFile != INVALID_HANDLE_VALUE )
        {
            if( !FillNativeMusicPacket( Packet ) )
            {
                MusicFailed = 1;
                return;
            }
            PacketData = (BYTE*)MusicSourceBuffer + Packet * XboxMusicStreamPacketBytes;
            SubmitBytes = MusicPacketBytes;
        }
        else
        {
#if XBOX_ENABLE_UMX_MUSIC
            if( !MusicContext )
                return;
            PacketData = GXboxMusicStreamData[Packet];
            RenderMusicBlock( GXboxMusicStreamData[Packet], XboxMusicStreamPacketBytes );
#else
            MusicFailed = 1;
            return;
#endif
        }

        XMEDIAPACKET Xmp;
        appMemzero( &Xmp, sizeof(Xmp) );
        Xmp.pvBuffer  = PacketData;
        Xmp.dwMaxSize = SubmitBytes;
        Xmp.pdwStatus = &MusicPacketStatus[Packet];
        MusicPacketStatus[Packet] = XMEDIAPACKET_STATUS_PENDING;

        HRESULT hr = MusicStream->Process( &Xmp, NULL );
        if( FAILED(hr) )
        {
            MusicPacketStatus[Packet] = XMEDIAPACKET_STATUS_FAILURE;
            if( !MusicFailed )
                GXboxLog.Write( "XboxAudio: music stream packet failed p=%d hr=0x%08X", Packet, (DWORD)hr );
            MusicFailed = 1;
            return;
        }
        MusicPacketsSubmitted++;
        GXboxAudioMusicPacketState = (LONG)MusicPacketsSubmitted;

        unguard;
    }

#if XBOX_ENABLE_UMX_MUSIC
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
#endif

    void ServiceMusicStream()
    {
        guard(UXboxAudioDevice::ServiceMusicStream);

        if( !MusicStream || !MusicPlaying || MusicPaused )
            return;

        for( INT Packet=0; Packet<XboxMusicStreamPackets; Packet++ )
        {
            if( MusicPacketStatus[Packet] != XMEDIAPACKET_STATUS_PENDING )
                SubmitMusicPacket( Packet );
        }
        if( MusicPacketsSubmitted > 0 )
            GXboxAudioMusicStreamState = 2;

        DirectSoundDoWork();

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
