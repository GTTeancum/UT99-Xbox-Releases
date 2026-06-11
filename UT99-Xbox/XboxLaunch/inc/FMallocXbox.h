// FMallocXbox.h
#pragma once

struct FXboxMallocHeader
{
    DWORD Magic;
    DWORD Size;
    DWORD Pad0;
    DWORD Pad1;
};

extern DWORD GXboxMallocLiveBytes;
extern DWORD GXboxMallocPeakBytes;
extern DWORD GXboxMallocTotalBytes;
extern DWORD GXboxMallocLargestBytes;
extern DWORD GXboxMallocLastLargeBytes;
extern char  GXboxMallocLargestTag[64];
extern char  GXboxMallocLastLargeTag[64];

static inline void XboxMallocCopyTag( char* Dest, INT DestCount, const TCHAR* Tag )
{
    if( !Dest || DestCount <= 0 )
        return;
    Dest[0] = 0;
    if( !Tag )
        Tag = TEXT("None");
    INT i = 0;
    for( ; i < DestCount - 1 && Tag[i]; i++ )
        Dest[i] = (char)Tag[i];
    Dest[i] = 0;
}

static inline void XboxMallocNoteAlloc( DWORD Count, const TCHAR* Tag )
{
    GXboxMallocLiveBytes += Count;
    GXboxMallocTotalBytes += Count;
    if( GXboxMallocLiveBytes > GXboxMallocPeakBytes )
        GXboxMallocPeakBytes = GXboxMallocLiveBytes;
    if( Count > GXboxMallocLargestBytes )
    {
        GXboxMallocLargestBytes = Count;
        XboxMallocCopyTag( GXboxMallocLargestTag, ARRAY_COUNT(GXboxMallocLargestTag), Tag );
    }
    if( Count >= 65536 )
    {
        GXboxMallocLastLargeBytes = Count;
        XboxMallocCopyTag( GXboxMallocLastLargeTag, ARRAY_COUNT(GXboxMallocLastLargeTag), Tag );
    }
}

static inline void XboxMallocNoteFree( DWORD Count )
{
    if( GXboxMallocLiveBytes >= Count )
        GXboxMallocLiveBytes -= Count;
    else
        GXboxMallocLiveBytes = 0;
}

class FMallocXbox : public FMalloc
{
public:
    void* Malloc( DWORD Count, const TCHAR* Tag )
    {
        DWORD Bytes = Count + sizeof(FXboxMallocHeader);
        FXboxMallocHeader* Header = (FXboxMallocHeader*)malloc( Bytes );
        if( !Header )
            appErrorf( TEXT("FMallocXbox: Out of memory allocating %i bytes tag=%s liveKB=%i peakKB=%i largestKB=%i largestTag=%s lastLargeKB=%i lastLargeTag=%s"),
                Count, Tag ? Tag : TEXT("None"), GXboxMallocLiveBytes / 1024, GXboxMallocPeakBytes / 1024,
                GXboxMallocLargestBytes / 1024, GXboxMallocLargestTag,
                GXboxMallocLastLargeBytes / 1024, GXboxMallocLastLargeTag );
        Header->Magic = 0x584D414C;
        Header->Size = Count;
        XboxMallocNoteAlloc( Count, Tag );
        return Header + 1;
    }
    void* Realloc( void* Original, DWORD Count, const TCHAR* Tag )
    {
        if( !Original )
            return Count ? Malloc( Count, Tag ) : NULL;
        FXboxMallocHeader* OldHeader = ((FXboxMallocHeader*)Original) - 1;
        DWORD OldSize = OldHeader->Size;
        if( Count == 0 )
        {
            Free( Original );
            return NULL;
        }
        DWORD Bytes = Count + sizeof(FXboxMallocHeader);
        FXboxMallocHeader* Header = (FXboxMallocHeader*)realloc( OldHeader, Bytes );
        if( !Header )
            appErrorf( TEXT("FMallocXbox: Out of memory reallocating %i bytes old=%i tag=%s liveKB=%i peakKB=%i largestKB=%i largestTag=%s lastLargeKB=%i lastLargeTag=%s"),
                Count, OldSize, Tag ? Tag : TEXT("None"), GXboxMallocLiveBytes / 1024, GXboxMallocPeakBytes / 1024,
                GXboxMallocLargestBytes / 1024, GXboxMallocLargestTag,
                GXboxMallocLastLargeBytes / 1024, GXboxMallocLastLargeTag );
        Header->Magic = 0x584D414C;
        Header->Size = Count;
        XboxMallocNoteFree( OldSize );
        XboxMallocNoteAlloc( Count, Tag );
        return Header + 1;
    }
    void Free( void* Original )
    {
        if( !Original )
            return;
        FXboxMallocHeader* Header = ((FXboxMallocHeader*)Original) - 1;
        if( Header->Magic == 0x584D414C )
            XboxMallocNoteFree( Header->Size );
        free( Header );
    }
    void DumpAllocs()
    {
        OutputDebugStringA( "FMallocXbox: DumpAllocs not implemented\n" );
    }
    void HeapCheck() {}
    void Init() {}
    void Exit() {}
};
