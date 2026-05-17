// FOutputDeviceXboxError.h
#pragma once

class FOutputDeviceXboxError : public FOutputDeviceError
{
public:
    void Serialize( const TCHAR* V, EName Event )
    {
        const char* Ansi = (const char*)appToAnsi(V);
        // Build "UT99 Error: <msg>" into a single buffer so CXBX-R captures it
        // as one DEBUG_PRINT line, not three with empty stragglers.
        char buf[1024];
        _snprintf( buf, sizeof(buf), "UT99 Error: %s", Ansi );
        buf[sizeof(buf)-1] = 0;
        OutputDebugStringA( buf );
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
