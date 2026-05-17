// FFeedbackContextXbox.h
#pragma once

class FFeedbackContextXbox : public FFeedbackContext
{
public:
    void Serialize( const TCHAR* V, EName Event )
    {
        const char* Ansi = (const char*)appToAnsi(V);
        // One OutputDebugStringA call per logical line.  GXboxLog.Write below
        // already echoes the line with a trailing newline via its own
        // OutputDebugStringA, so we don't need a separate "\n" call here —
        // that produced empty "DEBUG_PRINT:" lines in CXBX-R's console.
        OutputDebugStringA( Ansi );
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
