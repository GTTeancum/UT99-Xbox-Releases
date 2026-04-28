// FXboxLogger.h
// Low-level file logger for Xbox HDD.
// Uses raw Win32/Xbox kernel file APIs only — no Unreal dependencies.
// Safe to call before appInit() or any Unreal subsystem.

#pragma once

#include <xtl.h>
#include <stdio.h>
#include <stdarg.h>

class FXboxLogger
{
    HANDLE FileHandle;
    BOOL   IsOpen;
public:
    FXboxLogger() : FileHandle(INVALID_HANDLE_VALUE), IsOpen(FALSE) {}
    ~FXboxLogger() { Close(); }

    BOOL Open( const char* Path )
    {
        FileHandle = CreateFileA(
            Path,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            NULL
        );
        IsOpen = ( FileHandle != INVALID_HANDLE_VALUE );
        return IsOpen;
    }

    // Try multiple paths — Xbox drive letter mapping can be tricky
    BOOL OpenWithFallbacks()
    {
        // Try D: first (XBE launch directory on retail Xbox)
        if( Open( "D:\\ut99.log" ) )
            return TRUE;
        // Try T: (title persistent storage)
        if( Open( "T:\\ut99.log" ) )
            return TRUE;
        // Try E:\UT99\ (our intended location)
        if( Open( "E:\\UT99\\ut99.log" ) )
            return TRUE;
        // Try E: root
        if( Open( "E:\\ut99.log" ) )
            return TRUE;
        // Try relative to XBE
        if( Open( "ut99.log" ) )
            return TRUE;
        return FALSE;
    }

    void Close()
    {
        if( IsOpen && FileHandle != INVALID_HANDLE_VALUE )
        {
            CloseHandle( FileHandle );
            FileHandle = INVALID_HANDLE_VALUE;
            IsOpen = FALSE;
        }
    }

    void Write( const char* Fmt, ... )
    {
        if( !IsOpen )
            return;

        char Buf[1024];
        va_list Args;
        va_start( Args, Fmt );
        int Len = _vsnprintf( Buf, sizeof(Buf)-2, Fmt, Args );
        va_end( Args );

        if( Len < 0 ) Len = sizeof(Buf)-2;
        Buf[Len]   = '\n';
        Buf[Len+1] = '\0';

        DWORD Written;
        WriteFile( FileHandle, Buf, Len+1, &Written, NULL );
        FlushFileBuffers( FileHandle );

        // Also echo to debug output in case anything is listening
        OutputDebugStringA( Buf );
        OutputDebugStringA( "\n" );
    }
};

// Global logger instance — declared in XboxLaunch.cpp
extern FXboxLogger GXboxLog;
