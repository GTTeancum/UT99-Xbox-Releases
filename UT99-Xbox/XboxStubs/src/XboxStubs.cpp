// XboxStubs.cpp
// MSVC CRT intrinsics missing from XDK runtime.
//
// Name decoration: extern "C" __cdecl prepends one underscore.
// To produce symbol __ftol2_sse in the .obj, write _ftol2_sse in source.
// To produce __alloca_probe_16, write _alloca_probe_16 in source.
// To produce ___CxxFrameHandler3, write __CxxFrameHandler3 in source.

#include <xtl.h>

#pragma float_control(precise, on)

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
	int __cdecl __CxxFrameHandler3( void* a, void* b, void* c, void* d )
	{
		DebugBreak();
		return 0;
	}
}
