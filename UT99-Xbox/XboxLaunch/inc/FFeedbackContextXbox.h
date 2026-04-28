// FFeedbackContextXbox.h
#pragma once

class FFeedbackContextXbox : public FFeedbackContext
{
public:
    void Serialize( const TCHAR* V, EName Event )
    {
        const char* Ansi = (const char*)appToAnsi(V);
        OutputDebugStringA( Ansi );
        OutputDebugStringA( "\n" );
        GXboxLog.Write( "LOG: %s", Ansi );
    }
    UBOOL YesNof( const TCHAR* Fmt, ... )
    {
        return 1;
    }
    void BeginSlowTask( const TCHAR* Task, UBOOL StatusWindow, UBOOL Cancelable ) {}
    void EndSlowTask() {}
    UBOOL StatusUpdatef( INT Numerator, INT Denominator, const TCHAR* Fmt, ... )
    {
        return 1;
    }
    void SetContext( FContextSupplier* InSupplier ) {}
};
