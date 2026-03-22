// FOutputDeviceXboxError.h
#pragma once

class FOutputDeviceXboxError : public FOutputDeviceError
{
public:
    void Serialize( const TCHAR* V, EName Event )
    {
        OutputDebugStringA( "UT99 Error: " );
        OutputDebugStringA( (const char*)appToAnsi(V) );
        OutputDebugStringA( "\n" );
    }
    void HandleError()
    {
        OutputDebugStringA( "UT99: Fatal error - halting\n" );
#ifdef _DEBUG
        DebugBreak();
#endif
        XLaunchNewImage( NULL, NULL );
    }
};
