// FXboxLogger.h
// Low-level file logger for Xbox HDD.
// Uses raw Win32/Xbox kernel file APIs only — no Unreal dependencies.
// Safe to call before appInit() or any Unreal subsystem.

#pragma once

#include <xtl.h>
#include <stdio.h>
#include <stdarg.h>

extern "C" void XboxDebugMirrorWriteAnsi( const char* Line );
extern "C" void XboxDebugMirrorStart();
extern "C" void XboxDebugMirrorStop();
extern "C" void XboxDebugSetBootPhase( unsigned int Phase );
extern "C" void XboxDebugHeartbeat();

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
        char Buf[1024];
        va_list Args;
        va_start( Args, Fmt );
        int Len = _vsnprintf( Buf, sizeof(Buf)-2, Fmt, Args );
        va_end( Args );

        if( Len < 0 ) Len = sizeof(Buf)-2;
        Buf[Len]   = '\n';
        Buf[Len+1] = '\0';

        // The file was opened with FILE_FLAG_WRITE_THROUGH, which already
        // synchronously commits each WriteFile to disk before returning.
        // A separate FlushFileBuffers() call was redundant and dominated
        // GC time on Xbox (~10 ms per beacon for HDD sync × thousands of
        // beacons during the mark pass).  Trust WRITE_THROUGH and skip it.
        if( IsOpen && FileHandle != INVALID_HANDLE_VALUE )
        {
            DWORD Written;
            WriteFile( FileHandle, Buf, Len+1, &Written, NULL );
        }

        // Echo to debug output WITHOUT the trailing \n.  CXBX-R adds its own
        // newline for each OutputDebugStringA call, so leaving the \n in Buf
        // produces a blank "DEBUG_PRINT:" line after every log entry.
        Buf[Len] = 0;
        OutputDebugStringA( Buf );
        XboxDebugMirrorWriteAnsi( Buf );
    }

    // Call this from critical points (post-LoadMap, pre-GC, etc.) if you want
    // a hard guarantee the OS has flushed everything to disk before whatever
    // the next operation is.  Not needed in the common path — WRITE_THROUGH
    // already keeps the file up-to-date after each Write().
    void Flush()
    {
        if( IsOpen && FileHandle != INVALID_HANDLE_VALUE )
            FlushFileBuffers( FileHandle );
    }
};

// Global logger instance — declared in XboxLaunch.cpp
extern FXboxLogger GXboxLog;
