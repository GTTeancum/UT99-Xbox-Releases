// FOutputDeviceXboxError.h
#pragma once

class FOutputDeviceXboxError : public FOutputDeviceError
{
    INT ErrorPos;
    EName ErrorType;

public:
    FOutputDeviceXboxError()
    : ErrorPos(0)
    , ErrorType(NAME_None)
    {}

    void Serialize( const TCHAR* V, EName Event )
    {
        const char* Ansi = (const char*)appToAnsi(V);
#ifdef _DEBUG
        GXboxLog.Write( "ERROR: %s", Ansi );
        OutputDebugStringA( "UT99 Error while debugging" );
        UObject::StaticShutdownAfterError();
        DebugBreak();
#else
        if( !GIsCriticalError )
        {
            GIsCriticalError = 1;
            ErrorType = Event;
            debugf( NAME_Critical, TEXT("appError called:") );
            debugf( NAME_Critical, TEXT("%s"), V );
            GXboxLog.Write( "ERROR: %s", Ansi );

            UObject::StaticShutdownAfterError();
            appStrncpy( GErrorHist, V, ARRAY_COUNT(GErrorHist) );
            appStrncat( GErrorHist, TEXT("\r\n\r\n"), ARRAY_COUNT(GErrorHist) );
            ErrorPos = appStrlen(GErrorHist);
            if( GIsGuarded )
            {
                appStrncat( GErrorHist, LocalizeError("History",TEXT("Core")), ARRAY_COUNT(GErrorHist) );
                appStrncat( GErrorHist, TEXT(": "), ARRAY_COUNT(GErrorHist) );
            }
            else
            {
                HandleError();
            }
        }
        else
        {
            debugf( NAME_Critical, TEXT("Error reentered: %s"), V );
            GXboxLog.Write( "ERROR REENTERED: %s", Ansi );
        }

        if( GIsGuarded )
            throw( 1 );
        else
            appRequestExit( 1 );
#endif
    }

    void HandleError()
    {
        try
        {
            GIsGuarded       = 0;
            GIsRunning       = 0;
            GIsCriticalError = 1;
            GLogHook         = NULL;
            UObject::StaticShutdownAfterError();
            GErrorHist[ErrorType==NAME_FriendlyError ? ErrorPos : ARRAY_COUNT(GErrorHist)-1]=0;
            GXboxLog.Write( "FATAL: %s", TCHAR_TO_ANSI(GErrorHist) );
            OutputDebugStringA( "UT99: Fatal error - halting\n" );
            GXboxLog.Flush();
            if
            (
                GetFileAttributesA( "D:\\XboxSystemLinkSmoke.ini" ) != 0xFFFFFFFF
                || GetFileAttributesA( "D:\\XboxCharacterSoak.ini" ) != 0xFFFFFFFF
                || GetFileAttributesA( "D:\\XboxStartURL.ini" ) != 0xFFFFFFFF
            )
            {
                GXboxLog.Write( "FATAL: diagnostic marker present; holding 120s for RAM-log harvest" );
                GXboxLog.Flush();
                Sleep( 120000 );
            }
            GXboxLog.Close();
            XLaunchNewImage( NULL, NULL );
        }
        catch( ... )
        {}
    }
};
