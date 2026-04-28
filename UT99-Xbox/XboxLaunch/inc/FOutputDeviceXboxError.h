// FOutputDeviceXboxError.h
#pragma once

class FOutputDeviceXboxError : public FOutputDeviceError
{
public:
    void Serialize( const TCHAR* V, EName Event )
    {
        const char* Ansi = (const char*)appToAnsi(V);
        OutputDebugStringA( "UT99 Error: " );
        OutputDebugStringA( Ansi );
        OutputDebugStringA( "\n" );
        GXboxLog.Write( "ERROR: %s", Ansi );
    }
    void HandleError()
    {
        GXboxLog.Write( "FATAL: HandleError() called — halting" );
        GXboxLog.Close();
        OutputDebugStringA( "UT99: Fatal error - halting\n" );
        XLaunchNewImage( NULL, NULL );
    }
};
