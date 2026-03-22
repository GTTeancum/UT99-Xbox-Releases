// FFileManagerXbox.h
// FFileManager for Xbox HDD. Remaps paths to E:\UT99\.
#pragma once

#include "FFileManagerGeneric.h"

class FFileManagerXbox : public FFileManagerGeneric
{
public:
    static const TCHAR* GetRootPath()
    {
        return TEXT("E:\\UT99\\");
    }

    FString MapPath( const TCHAR* Filename )
    {
        // Strip leading ..\ sequences
        while( Filename[0]=='.' && Filename[1]=='.' &&
               (Filename[2]=='\\' || Filename[2]=='/') )
            Filename += 3;
        // Already absolute
        if( Filename[1]==':' )
            return FString(Filename);
        return FString(GetRootPath()) + Filename;
    }

    // Required pure virtuals from FFileManager
    FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags=0, FOutputDevice* Error=GLog )
    {
        return GFileManager->CreateFileReader( *MapPath(Filename), Flags, Error );
    }
    FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags=0, FOutputDevice* Error=GNull )
    {
        return GFileManager->CreateFileWriter( *MapPath(Filename), Flags, Error );
    }
    UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
    {
        return GFileManager->Delete( *MapPath(Filename), RequireExists, EvenReadOnly );
    }
    SQWORD GetGlobalTime( const TCHAR* Filename )
    {
        return 0;
    }
    UBOOL SetGlobalTime( const TCHAR* Filename )
    {
        return 1;
    }
    TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
    {
        return GFileManager->FindFiles( *MapPath(Filename), Files, Directories );
    }
    UBOOL SetDefaultDirectory( const TCHAR* Filename )
    {
        return 1;
    }
    FString GetDefaultDirectory()
    {
        return FString( GetRootPath() );
    }
    INT FileSize( const TCHAR* Filename )
    {
        return GFileManager->FileSize( *MapPath(Filename) );
    }
    UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1,
                UBOOL EvenReadOnly=0, UBOOL Attributes=0 )
    {
        return GFileManager->Move( *MapPath(Dest), *MapPath(Src), Replace, EvenReadOnly, Attributes );
    }
    UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
    {
        return GFileManager->MakeDirectory( *MapPath(Path), Tree );
    }
    UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
    {
        return GFileManager->DeleteDirectory( *MapPath(Path), RequireExists, Tree );
    }
};
