///////////////////////////////////////////////////////////////////////////////
//
// ldrtest.c - end-to-end test of the standalone loader (kxgdildr.dll).
//
// Must be started with kxgdildr.dll registered as verifier provider for
// kxgdi-ldrtest.exe (test\Test-Loader.ps1 does that). Like Path of Exile, this
// program statically imports D3DKMTOpenAdapterFromLuid from gdi32.dll: on
// Windows 7 it does not even start unless the loader works.
//
// Checks:
//   1. kxgdildr.dll and kxgdi.dll are loaded.
//   2. The program's own import resolves to kxgdi.dll.
//   3. A statically linked DLL and a dynamically loaded DLL with the same
//      import also resolve to kxgdi.dll. (Windows 7 only; newer Windows
//      versions resolve DLL imports at a different time and have the
//      function natively anyway.)
//   4. Calling the import with an unknown LUID returns STATUS_INVALID_PARAMETER.
//
// Exit code = number of failed checks.
//
///////////////////////////////////////////////////////////////////////////////

#include "../src/kxtypes.h"
#include "testutil.h"

typedef struct _LUID {
	DWORD	LowPart;
	LONG	HighPart;
} LUID;

typedef struct _D3DKMT_OPENADAPTERFROMLUID {
	LUID	AdapterLuid;
	UINT	hAdapter;
} D3DKMT_OPENADAPTERFROMLUID;

__declspec(dllimport) NTSTATUS WINAPI D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID *Args);
__declspec(dllimport) PVOID KxTestGetImport(void);		// kxgdi-ldrtest-static.dll

typedef PVOID (*PFN_GETIMPORT)(void);
typedef PCWSTR (*PFN_GETLOG)(void);

void WINAPI TestMain(void)
{
	HMODULE Loader, KxGdi, Gdi32, Dynamic;
	PVOID KxGdiExport = NULL;
	PVOID Gdi32Export = NULL;
	PVOID Imported;
	ULONG Major, Minor, Build;
	BOOL IsWindows7;
	D3DKMT_OPENADAPTERFROMLUID Args;
	NTSTATUS Status;

	RtlGetNtVersionNumbers(&Major, &Minor, &Build);
	IsWindows7 = (Major == 6 && Minor == 1);

	TestWrite("kxgdi loader test\r\n");

	Loader = GetModuleHandleW(L"kxgdildr.dll");
	TestCheck(Loader != NULL, "kxgdildr.dll is loaded (verifier provider active)");

	if (Loader) {
		PFN_GETLOG GetLog = (PFN_GETLOG) GetProcAddress(Loader, "KxGdiLdrGetLog");
		if (GetLog) {
			TestWrite("--- loader log ---\r\n");
			TestWriteW(GetLog());
			TestWrite("------------------\r\n");
		}
	} else {
		TestWrite("         Is the IFEO entry for kxgdi-ldrtest.exe set? (run Test-Loader.ps1 as admin)\r\n");
	}

	KxGdi = GetModuleHandleW(L"kxgdi.dll");
	TestCheck(KxGdi != NULL, "kxgdi.dll is loaded");
	if (KxGdi) KxGdiExport = GetProcAddress(KxGdi, "D3DKMTOpenAdapterFromLuid");

	Gdi32 = GetModuleHandleW(L"gdi32.dll");
	if (Gdi32) Gdi32Export = GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromLuid");

	TestWrite("         kxgdi export ");
	TestWriteHex((ULONG_PTR) KxGdiExport, 16);
	TestWrite(", native gdi32 export ");
	TestWriteHex((ULONG_PTR) Gdi32Export, 16);
	TestWrite("\r\n");

	Imported = (PVOID) &D3DKMTOpenAdapterFromLuid;
	TestCheck(KxGdiExport != NULL && Imported == KxGdiExport,
		"program import D3DKMTOpenAdapterFromLuid resolves to kxgdi.dll");

	Imported = KxTestGetImport();
	TestCheckOrInfo(KxGdiExport != NULL && Imported == KxGdiExport, IsWindows7,
		"static DLL import resolves to kxgdi.dll");

	Dynamic = LoadLibraryW(L"kxgdi-ldrtest-dynamic.dll");
	TestCheck(Dynamic != NULL, "kxgdi-ldrtest-dynamic.dll loads");
	if (Dynamic) {
		PFN_GETIMPORT GetImport = (PFN_GETIMPORT) GetProcAddress(Dynamic, "KxTestGetImport");
		Imported = GetImport ? GetImport() : NULL;
		TestCheckOrInfo(KxGdiExport != NULL && Imported == KxGdiExport, IsWindows7,
			"dynamic DLL import resolves to kxgdi.dll");
	}

	Args.AdapterLuid.LowPart = 0xDEADBEEF;
	Args.AdapterLuid.HighPart = 0x7FFF0000;
	Args.hAdapter = 0;
	Status = D3DKMTOpenAdapterFromLuid(&Args);
	TestCheck(Status == STATUS_INVALID_PARAMETER,
		"calling the import with an unknown LUID returns STATUS_INVALID_PARAMETER");

	TestFinish();
}
