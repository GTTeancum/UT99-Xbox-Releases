// FMallocXbox.h
#pragma once

class FMallocXbox : public FMalloc
{
public:
    void* Malloc( DWORD Count, const TCHAR* Tag )
    {
        void* Ptr = malloc( Count );
        if( !Ptr )
            appErrorf( TEXT("FMallocXbox: Out of memory allocating %i bytes"), Count );
        return Ptr;
    }
    void* Realloc( void* Original, DWORD Count, const TCHAR* Tag )
    {
        void* Ptr = realloc( Original, Count );
        if( Count && !Ptr )
            appErrorf( TEXT("FMallocXbox: Out of memory reallocating %i bytes"), Count );
        return Ptr;
    }
    void Free( void* Original )
    {
        free( Original );
    }
    void DumpAllocs()
    {
        OutputDebugStringA( "FMallocXbox: DumpAllocs not implemented\n" );
    }
    void HeapCheck() {}
    void Init() {}
    void Exit() {}
};
