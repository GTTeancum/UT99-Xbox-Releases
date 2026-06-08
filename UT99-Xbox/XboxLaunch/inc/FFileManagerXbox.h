// FFileManagerXbox.h
// File manager for Xbox. Tracks its own base directory internally.
// SetDefaultDirectory stores the base path, all file operations prepend it.
// ../ sequences resolved by walking up from base directory.
// Works identically on real Xbox hardware and CXBX-R.
#pragma once

#include "FFileManagerGeneric.h"

/*-----------------------------------------------------------------------------
    HANDLE-based archive reader.
-----------------------------------------------------------------------------*/
class FArchiveFileReader : public FArchive
{
public:
    FArchiveFileReader( HANDLE InHandle, FOutputDevice* InError, INT InSize )
    :   Handle( InHandle ), Error( InError ), Size( InSize )
    ,   Pos( 0 ), BufferBase( 0 ), BufferCount( 0 )
    { ArIsLoading = ArIsPersistent = 1; }
    ~FArchiveFileReader() { if( Handle ) Close(); }
    void Precache( INT HintCount )
    {
        guardSlow(FArchiveFileReader::Precache);
        checkSlow(Pos==BufferBase+BufferCount);
        BufferBase = Pos;
        BufferCount = Min( Min( HintCount, (INT)(ARRAY_COUNT(Buffer) - (Pos&(ARRAY_COUNT(Buffer)-1))) ), Size-Pos );
        INT Count=0;
        ReadFile( Handle, Buffer, BufferCount, (DWORD*)&Count, NULL );
        if( Count!=BufferCount )
        { ArIsError = 1; Error->Logf( TEXT("ReadFile failed: Count=%i BufferCount=%i Error=%s"), Count, BufferCount, appGetSystemErrorMessage() ); }
        unguardSlow;
    }
    void Seek( INT InPos )
    {
        check(InPos>=0); check(InPos<=Size);
        if( SetFilePointer( Handle, InPos, 0, FILE_BEGIN )==0xFFFFFFFF )
        { ArIsError = 1; Error->Logf( TEXT("SetFilePointer Failed %i/%i: %i %s"), InPos, Size, Pos, appGetSystemErrorMessage() ); }
        Pos = InPos; BufferBase = Pos; BufferCount = 0;
    }
    INT Tell() { return Pos; }
    INT TotalSize() { return Size; }
    UBOOL Close() { if( Handle ) CloseHandle( Handle ); Handle = NULL; return !ArIsError; }
    void Serialize( void* V, INT Length )
    {
        guardSlow(FArchiveFileReader::Serialize);
        while( Length>0 )
        {
            INT Copy = Min( Length, BufferBase+BufferCount-Pos );
            if( Copy==0 )
            {
                if( Length >= ARRAY_COUNT(Buffer) )
                {
                    INT Count=0;
                    ReadFile( Handle, V, Length, (DWORD*)&Count, NULL );
                    if( Count!=Length )
                    { ArIsError = 1; Error->Logf( TEXT("ReadFile failed: Count=%i Length=%i Error=%s"), Count, Length, appGetSystemErrorMessage() ); }
                    Pos += Length; BufferBase += Length; return;
                }
                Precache( MAXINT );
                Copy = Min( Length, BufferBase+BufferCount-Pos );
                if( Copy<=0 )
                { ArIsError = 1; Error->Logf( TEXT("ReadFile beyond EOF %i+%i/%i"), Pos, Length, Size ); }
                if( ArIsError ) return;
            }
            appMemcpy( V, Buffer+Pos-BufferBase, Copy );
            Pos += Copy; Length -= Copy; V = (BYTE*)V + Copy;
        }
        unguardSlow;
    }
protected:
    HANDLE Handle; FOutputDevice* Error;
    INT Size, Pos, BufferBase, BufferCount;
    BYTE Buffer[8192];
};

/*-----------------------------------------------------------------------------
    HANDLE-based archive writer.
-----------------------------------------------------------------------------*/
class FArchiveFileWriter : public FArchive
{
public:
    FArchiveFileWriter( HANDLE InHandle, FOutputDevice* InError, INT InPos )
    :   Handle( InHandle ), Error( InError ), Pos( InPos ), BufferCount( 0 ) {}
    ~FArchiveFileWriter() { if( Handle ) Close(); Handle = NULL; }
    void Seek( INT InPos )
    {
        Flush();
        if( SetFilePointer( Handle, InPos, 0, FILE_BEGIN )==0xFFFFFFFF )
        { ArIsError = 1; Error->Logf( LocalizeError("SeekFailed",TEXT("Core")) ); }
        Pos = InPos;
    }
    INT Tell() { return Pos; }
    UBOOL Close()
    {
        Flush();
        if( Handle && !CloseHandle(Handle) )
        { ArIsError = 1; Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) ); }
        return !ArIsError;
    }
    void Serialize( void* V, INT Length )
    {
        Pos += Length; INT Copy;
        while( Length > (Copy=ARRAY_COUNT(Buffer)-BufferCount) )
        {
            appMemcpy( Buffer+BufferCount, V, Copy );
            BufferCount += Copy; Length -= Copy; V = (BYTE*)V + Copy;
            Flush();
        }
        if( Length ) { appMemcpy( Buffer+BufferCount, V, Length ); BufferCount += Length; }
    }
    void Flush()
    {
        if( BufferCount )
        {
            INT Result=0;
            if( !WriteFile( Handle, Buffer, BufferCount, (DWORD*)&Result, NULL ) )
            { ArIsError = 1; Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) ); }
        }
        BufferCount = 0;
    }
protected:
    HANDLE Handle; FOutputDevice* Error;
    INT Pos, BufferCount;
    BYTE Buffer[4096];
};

/*-----------------------------------------------------------------------------
    Xbox file manager.
-----------------------------------------------------------------------------*/
class FFileManagerXbox : public FFileManagerGeneric
{
    TCHAR BaseDir[1024];

public:
    FFileManagerXbox()
    {
        BaseDir[0] = 0;
    }

    FString NormalizePath( const TCHAR* InPath )
    {
        TCHAR Src[1024];
        TCHAR Out[1024];
        appStrncpy( Src, InPath, ARRAY_COUNT(Src) );
        Src[ARRAY_COUNT(Src)-1] = 0;

        for( INT i=0; Src[i]; i++ )
            if( Src[i] == '/' )
                Src[i] = '\\';

        Out[0] = 0;
        INT Pos = 0;
        if( Src[0] && Src[1] == ':' )
        {
            Out[0] = Src[0];
            Out[1] = ':';
            Out[2] = '\\';
            Out[3] = 0;
            Pos = 2;
            while( Src[Pos] == '\\' )
                Pos++;
        }
        else if( Src[0] == '\\' )
        {
            Out[0] = '\\';
            Out[1] = 0;
            Pos = 1;
            while( Src[Pos] == '\\' )
                Pos++;
        }

        while( Src[Pos] )
        {
            while( Src[Pos] == '\\' )
                Pos++;
            if( !Src[Pos] )
                break;

            TCHAR Segment[256];
            INT SegLen = 0;
            while( Src[Pos] && Src[Pos] != '\\' && SegLen < ARRAY_COUNT(Segment)-1 )
                Segment[SegLen++] = Src[Pos++];
            Segment[SegLen] = 0;

            if( appStrcmp( Segment, TEXT(".") ) == 0 )
                continue;

            if( appStrcmp( Segment, TEXT("..") ) == 0 )
            {
                INT Len = appStrlen( Out );
                while( Len > 0 && Out[Len-1] == '\\' )
                    Out[--Len] = 0;

                INT MinLen = (Out[0] && Out[1] == ':') ? 3 : ((Out[0] == '\\') ? 1 : 0);
                if( Len > MinLen )
                {
                    while( Len > MinLen && Out[Len-1] != '\\' )
                        Out[--Len] = 0;
                    while( Len > MinLen && Out[Len-1] == '\\' )
                        Out[--Len] = 0;
                }
                if( MinLen == 3 )
                {
                    Out[1] = ':';
                    Out[2] = '\\';
                    Out[3] = 0;
                }
                continue;
            }

            INT OutLen = appStrlen( Out );
            if( OutLen > 0 && Out[OutLen-1] != '\\' )
                appStrcat( Out, TEXT("\\") );
            appStrcat( Out, Segment );
        }

        return FString( Out );
    }

    FString ResolvePath( const TCHAR* Filename )
    {
        if( Filename[0] && Filename[1]==':' )
            return NormalizePath( Filename );

        FString Result( BaseDir );

        while( Filename[0]=='.' && Filename[1]=='.' &&
               (Filename[2]=='\\' || Filename[2]=='/') )
        {
            Filename += 3;
            INT Len = Result.Len();
            if( Len > 0 && ( (*Result)[Len-1]=='\\' || (*Result)[Len-1]=='/' ) )
                Result = Result.Left( Len - 1 );
            INT Slash = Max( Result.InStr( TEXT("\\"), 1 ), Result.InStr( TEXT("/"), 1 ) );
            if( Slash >= 0 )
                Result = Result.Left( Slash + 1 );
        }

        while( Filename[0]=='.' && (Filename[1]=='\\' || Filename[1]=='/') )
            Filename += 2;

        FString Final = NormalizePath( *(Result + Filename) );

        // Diagnostic: log first 5000 resolutions (bumped from 30 so we can see
        // file ops happening late in boot — e.g. during GetPackageLinker for
        // UTMenu when it tries to open UTMenu.u for the first time).
        static INT LogCount = 0;
        if( LogCount++ < 0 )
        {
            char buf[512];
            _snprintf( buf, sizeof(buf), "RESOLVE: [%s] -> [%s]",
                TCHAR_TO_ANSI(Filename), TCHAR_TO_ANSI(*Final) );
            // GXboxLog.Write already echoes via OutputDebugStringA;
            // a separate OutputDebugStringA(buf) here would duplicate
            // every FileManager line in CXBX-R's console.
            GXboxLog.Write( "%s", buf );
        }

        return Final;
    }

    UBOOL SetDefaultDirectory( const TCHAR* Filename )
    {
        if( !Filename )
            return 0;
        appStrncpy( BaseDir, Filename, ARRAY_COUNT(BaseDir) );
        INT Len = appStrlen( BaseDir );
        if( Len > 0 && BaseDir[Len-1] != '\\' && BaseDir[Len-1] != '/' )
        {
            if( Len < ARRAY_COUNT(BaseDir) - 2 )
            {
                BaseDir[Len] = '\\';
                BaseDir[Len+1] = 0;
            }
        }
        // Diagnostic
        {
            char buf[512];
            _snprintf( buf, sizeof(buf), "SetDefaultDirectory: BaseDir=[%s]",
                TCHAR_TO_ANSI(BaseDir) );
            // GXboxLog.Write already echoes via OutputDebugStringA;
            // a separate OutputDebugStringA(buf) here would duplicate
            // every FileManager line in CXBX-R's console.
            GXboxLog.Write( "%s", buf );
        }
        return 1;
    }

    FString GetDefaultDirectory()
    {
        return FString( BaseDir );
    }

    FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags=0, FOutputDevice* Error=GLog )
    {
        guard(FFileManagerXbox::CreateFileReader);
        FString Path = ResolvePath( Filename );
        HANDLE Handle = CreateFileA(
            TCHAR_TO_ANSI(*Path), GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
        );
        if( Handle == INVALID_HANDLE_VALUE )
        {
            GXboxLog.Write( "CreateFileReader: FAILED [%s] error=%lu", TCHAR_TO_ANSI(*Path), GetLastError() );
            if( Flags & FILEREAD_NoFail )
                appErrorf( TEXT("Failed to read file: %s"), *Path );
            return NULL;
        }
        INT Size = GetFileSize(Handle, NULL);
        return new(TEXT("XboxFileReader"))FArchiveFileReader(Handle, Error, Size);
        unguard;
    }

    FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags=0, FOutputDevice* Error=GNull )
    {
        guard(FFileManagerXbox::CreateFileWriter);
        FString Path = ResolvePath( Filename );
        if( Flags & FILEWRITE_EvenIfReadOnly )
            SetFileAttributesA( TCHAR_TO_ANSI(*Path), FILE_ATTRIBUTE_NORMAL );
        DWORD Access   = GENERIC_WRITE;
        DWORD WinFlags = (Flags & FILEWRITE_AllowRead) ? FILE_SHARE_READ : 0;
        DWORD Create   = (Flags & FILEWRITE_Append) ? OPEN_ALWAYS : (Flags & FILEWRITE_NoReplaceExisting) ? CREATE_NEW : CREATE_ALWAYS;
        HANDLE Handle = CreateFileA(
            TCHAR_TO_ANSI(*Path), Access, WinFlags,
            NULL, Create, FILE_ATTRIBUTE_NORMAL, NULL
        );
        INT Pos = 0;
        if( Handle == INVALID_HANDLE_VALUE )
        {
            if( Flags & FILEWRITE_NoFail )
                appErrorf( TEXT("Failed to create file: %s"), *Path );
            return NULL;
        }
        if( Flags & FILEWRITE_Append )
            Pos = SetFilePointer( Handle, 0, 0, FILE_END );
        return new(TEXT("XboxFileWriter"))FArchiveFileWriter(Handle, Error, Pos);
        unguard;
    }

    UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
    {
        guard(FFileManagerXbox::Delete);
        FString Path = ResolvePath( Filename );
        if( EvenReadOnly )
            SetFileAttributesA( TCHAR_TO_ANSI(*Path), FILE_ATTRIBUTE_NORMAL );
        return DeleteFileA( TCHAR_TO_ANSI(*Path) ) != 0 || (!RequireExists && GetLastError() == ERROR_FILE_NOT_FOUND);
        unguard;
    }

    SQWORD GetGlobalTime( const TCHAR* Filename ) { return 0; }
    UBOOL SetGlobalTime( const TCHAR* Filename ) { return 1; }

    TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
    {
        guard(FFileManagerXbox::FindFiles);
        TArray<FString> Result;
        FString Path = ResolvePath( Filename );

        // Diagnostic
        static INT FFCount = 0;
        if( FFCount++ < 0 )
        {
            char buf[512];
            _snprintf( buf, sizeof(buf), "FindFiles: [%s] -> [%s]",
                TCHAR_TO_ANSI(Filename), TCHAR_TO_ANSI(*Path) );
            // GXboxLog.Write already echoes via OutputDebugStringA;
            // a separate OutputDebugStringA(buf) here would duplicate
            // every FileManager line in CXBX-R's console.
            GXboxLog.Write( "%s", buf );
        }

        WIN32_FIND_DATAA Data;
        HANDLE Handle = FindFirstFileA( TCHAR_TO_ANSI(*Path), &Data );
        if( Handle != INVALID_HANDLE_VALUE )
        {
            do
            {
                if( appStricmp( ANSI_TO_TCHAR(Data.cFileName), TEXT(".") )
                &&  appStricmp( ANSI_TO_TCHAR(Data.cFileName), TEXT("..") )
                &&  ((Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? Directories : Files) )
                    new(Result) FString( ANSI_TO_TCHAR(Data.cFileName) );
            }
            while( FindNextFileA( Handle, &Data ) );
            FindClose( Handle );
        }

        // Diagnostic
        if( FFCount <= 0 )
        {
            char buf[512];
            _snprintf( buf, sizeof(buf), "FindFiles: found %d results", Result.Num() );
            // GXboxLog.Write already echoes via OutputDebugStringA;
            // a separate OutputDebugStringA(buf) here would duplicate
            // every FileManager line in CXBX-R's console.
            GXboxLog.Write( "%s", buf );
        }

        return Result;
        unguard;
    }

    INT FileSize( const TCHAR* Filename )
    {
        guard(FFileManagerXbox::FileSize);
        FString Path = ResolvePath( Filename );

        // Diagnostic: log first 5000 file size checks (bumped from 40 to cover
        // late-boot file ops like UTMenu package lookup).
        static INT FSCount = 0;
        if( FSCount++ < 0 )
        {
            char buf[512];
            HANDLE hTest = CreateFileA(
                TCHAR_TO_ANSI(*Path), GENERIC_READ, FILE_SHARE_READ,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
            );
            _snprintf( buf, sizeof(buf), "FileSize: [%s] -> [%s] = %s",
                TCHAR_TO_ANSI(Filename), TCHAR_TO_ANSI(*Path),
                hTest != INVALID_HANDLE_VALUE ? "FOUND" : "NOT FOUND" );
            // GXboxLog.Write already echoes via OutputDebugStringA;
            // a separate OutputDebugStringA(buf) here would duplicate
            // every FileManager line in CXBX-R's console.
            GXboxLog.Write( "%s", buf );
            if( hTest != INVALID_HANDLE_VALUE )
            {
                DWORD sz = GetFileSize( hTest, NULL );
                CloseHandle( hTest );
                GXboxLog.Write( "FileSize: size=%lu for [%s]", sz, TCHAR_TO_ANSI(*Path) );
                return sz;
            }
            return -1;
        }

        HANDLE Handle = CreateFileA(
            TCHAR_TO_ANSI(*Path), GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
        );
        if( Handle == INVALID_HANDLE_VALUE )
            return -1;
        DWORD Result = GetFileSize( Handle, NULL );
        CloseHandle( Handle );
        return Result;
        unguard;
    }

    UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1,
                UBOOL EvenReadOnly=0, UBOOL Attributes=0 )
    {
        guard(FFileManagerXbox::Move);
        FString PDest = ResolvePath( Dest );
        FString PSrc  = ResolvePath( Src );
        Delete( *PDest, 0, 1 );
        return MoveFileA( TCHAR_TO_ANSI(*PSrc), TCHAR_TO_ANSI(*PDest) ) != 0;
        unguard;
    }

    UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
    {
        guard(FFileManagerXbox::MakeDirectory);
        if( Tree )
            return FFileManagerGeneric::MakeDirectory( Path, Tree );
        FString P = ResolvePath( Path );
        return CreateDirectoryA( TCHAR_TO_ANSI(*P), NULL ) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
        unguard;
    }

    UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
    {
        guard(FFileManagerXbox::DeleteDirectory);
        if( Tree )
            return FFileManagerGeneric::DeleteDirectory( Path, RequireExists, Tree );
        FString P = ResolvePath( Path );
        return RemoveDirectoryA( TCHAR_TO_ANSI(*P) ) != 0 || (!RequireExists && GetLastError() == ERROR_FILE_NOT_FOUND);
        unguard;
    }
};
