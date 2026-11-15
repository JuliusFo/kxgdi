///////////////////////////////////////////////////////////////////////////////
//
// ldrtest-dll.c - helper DLL for the loader test.
//
// Built twice: kxgdi-ldrtest-static.dll (static import of the test program)
// and kxgdi-ldrtest-dynamic.dll (loaded with LoadLibrary). Like a game DLL,
// it imports D3DKMTOpenAdapterFromLuid from gdi32.dll, which fails on
// Windows 7 unless kxgdildr renamed the import to kxgdi.dll.
//
///////////////////////////////////////////////////////////////////////////////

#include "../src/kxtypes.h"

typedef struct _D3DKMT_OPENADAPTERFROMLUID D3DKMT_OPENADAPTERFROMLUID;

__declspec(dllimport) NTSTATUS WINAPI D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID *Args);

// Returns the address the loader resolved for D3DKMTOpenAdapterFromLuid.
__declspec(dllexport) PVOID KxTestGetImport(void)
{
	return (PVOID) &D3DKMTOpenAdapterFromLuid;
}

BOOL WINAPI DllMain(PVOID DllBase, ULONG Reason, PVOID Reserved)
{
	(void) DllBase; (void) Reason; (void) Reserved;
	return TRUE;
}
