// XboxStubs.cpp
// MSVC CRT intrinsics missing from XDK runtime.

#include <xtl.h>

#pragma float_control(precise, on)

// Import the v1 handler from the XDK CRT.
extern "C" int __cdecl __CxxFrameHandler( void*, void*, void*, void* );

extern "C"
{
	// Produces __ftol2_sse in .obj -- matches what linker wants
	long __cdecl _ftol2_sse( double d )
	{
		volatile double v = d;
		if( v >= 0.0 )
			return (long)(unsigned long)v;
		else
			return -(long)(unsigned long)(-v);
	}

	// Produces __alloca_probe_16 in .obj
	void __cdecl _alloca_probe_16()
	{
		// Xbox stack fully committed -- no probe needed
	}

	// Produces ___CxxFrameHandler3 in .obj
	// VS2005 generates this, but the XDK CRT only has __CxxFrameHandler (v1).
	// The calling convention and parameter layout are identical for the
	// basic C++ exception patterns UT99 uses (throw/catch of TCHAR*).
	// Forward to the XDK's v1 handler.
	int __cdecl __CxxFrameHandler3( void* a, void* b, void* c, void* d )
	{
		return __CxxFrameHandler( a, b, c, d );
	}
}
