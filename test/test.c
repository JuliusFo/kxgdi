///////////////////////////////////////////////////////////////////////////////
//
// test.c - smoke test for kxgdi.dll (runs on Windows 7 and later).
//
// Checks:
//   1. kxgdi.dll loads and exports D3DKMTOpenAdapterFromLuid.
//   2. Forwarded exports resolve to the same address as in gdi32.dll.
//   3. For every display adapter, both emulation strategies and the exported
//      function find the adapter by its LUID. On Windows 8+ the result is
//      compared with the native gdi32!D3DKMTOpenAdapterFromLuid.
//   4. An unknown LUID is rejected with STATUS_INVALID_PARAMETER.
//
// Exit code = number of failed checks.
//
// Option /ci: on machines without a desktop display adapter (headless build
// servers), skip the adapter checks instead of failing.
//
///////////////////////////////////////////////////////////////////////////////

// Pull in the implementation so the static strategy functions can be tested
// individually. (The DLL itself is loaded separately below.)
#include "../src/kxgdi.c"

__declspec(dllimport) HANDLE	WINAPI GetStdHandle(DWORD StdHandle);
__declspec(dllimport) BOOL		WINAPI WriteFile(HANDLE File, const void *Buffer,
	DWORD BytesToWrite, DWORD *BytesWritten, PVOID Overlapped);
__declspec(dllimport) void		WINAPI ExitProcess(UINT ExitCode);
__declspec(dllimport) PWSTR		WINAPI GetCommandLineW(void);

typedef NTSTATUS (WINAPI *PFN_OPENFROMLUID)(D3DKMT_OPENADAPTERFROMLUID *Args);

static HANDLE StdOut;
static UINT Failures;

static void Print(LOG_BUFFER *Log)
{
	char Ascii[LOG_BUFFER_CCH];
	DWORD Written;
	UINT Index;

	// All messages are ASCII. WriteFile works for consoles, pipes and files.
	LogAppend(Log, L"\r\n");

	for (Index = 0; Index < Log->Length; ++Index) {
		Ascii[Index] = (Log->Text[Index] < 0x80) ? (char) Log->Text[Index] : '?';
	}

	WriteFile(StdOut, Ascii, Log->Length, &Written, NULL);
	Log->Length = 0;
	Log->Text[0] = 0;
}

// TRUE if the command line contains Arg (case-insensitive, ASCII only).
static BOOL HasArgument(PCWSTR Arg)
{
	PCWSTR Cmd = GetCommandLineW();
	UINT i, j;

	for (i = 0; Cmd[i]; ++i) {
		for (j = 0; Arg[j]; ++j) {
			WCHAR a = Cmd[i + j], b = Arg[j];
			if (a >= L'A' && a <= L'Z') a += L'a' - L'A';
			if (b >= L'A' && b <= L'Z') b += L'a' - L'A';
			if (a != b) break;
		}
		if (!Arg[j]) return TRUE;
	}

	return FALSE;
}

static void Check(BOOL Condition, PCWSTR What)
{
	LOG_BUFFER Log;

	Log.Length = 0;
	LogAppend(&Log, Condition ? L"  [ OK ] " : L"  [FAIL] ");
	LogAppend(&Log, What);
	Print(&Log);

	if (!Condition) {
		++Failures;
	}
}

static void TestLuid(LUID Luid, PFN_OPENFROMLUID Exported, PFN_OPENFROMLUID Native, BOOL ExpectFound)
{
	D3DKMT_HANDLE Adapter;
	D3DKMT_OPENADAPTERFROMLUID Args;
	NTSTATUS Status;
	BOOL Found;
	LOG_BUFFER Log;

	Log.Length = 0;
	LogAppend(&Log, ExpectFound ? L"Adapter LUID " : L"Unknown LUID ");
	LogAppendLuid(&Log, Luid);
	Print(&Log);

	if (ExpectFound) {
		Adapter = 0;
		Found = OpenAdapterByLuidFromDeviceInterfaces(Luid, &Adapter);
		Check(Found && Adapter != 0, L"strategy 1 (device interfaces) finds adapter");
		if (Found) CloseKmtAdapter(Adapter);

		Adapter = 0;
		Found = OpenAdapterByLuidFromGdiDisplayNames(Luid, &Adapter);
		// Strategy 2 only sees adapters with a desktop display; for those it must work.
		Check(Found && Adapter != 0, L"strategy 2 (GDI display names) finds adapter");
		if (Found) CloseKmtAdapter(Adapter);
	}

	Args.AdapterLuid = Luid;
	Args.hAdapter = 0;
	Status = Exported(&Args);

	if (ExpectFound) {
		Check(Status == STATUS_SUCCESS && Args.hAdapter != 0,
			L"kxgdi!D3DKMTOpenAdapterFromLuid returns STATUS_SUCCESS + handle");
		if (NT_SUCCESS(Status)) CloseKmtAdapter(Args.hAdapter);
	} else {
		Check(Status == STATUS_INVALID_PARAMETER && Args.hAdapter == 0,
			L"kxgdi!D3DKMTOpenAdapterFromLuid returns STATUS_INVALID_PARAMETER");
	}

	if (Native) {
		NTSTATUS NativeStatus;
		D3DKMT_OPENADAPTERFROMLUID NativeArgs;

		NativeArgs.AdapterLuid = Luid;
		NativeArgs.hAdapter = 0;
		NativeStatus = Native(&NativeArgs);
		if (NT_SUCCESS(NativeStatus)) CloseKmtAdapter(NativeArgs.hAdapter);

		Log.Length = 0;
		LogAppend(&Log, L"         native gdi32 status ");
		LogAppendHex(&Log, (DWORD) NativeStatus);
		LogAppend(&Log, L", kxgdi status ");
		LogAppendHex(&Log, (DWORD) Status);
		Print(&Log);

		Check(NT_SUCCESS(NativeStatus) == NT_SUCCESS(Status),
			L"success/failure matches native Windows 8+ implementation");
	}
}

void WINAPI TestMain(void)
{
	HMODULE KxGdi;
	HMODULE Gdi32;
	HMODULE User32;
	PFN_OPENFROMLUID Exported;
	PFN_OPENFROMLUID Native;
	PFN_ENUMDISPLAYDEVICESW pEnumDisplayDevicesW;
	LUID Seen[16];
	UINT SeenCount;
	DWORD Index;
	LUID Bogus;
	LOG_BUFFER Log;

	StdOut = GetStdHandle((DWORD) -11); // STD_OUTPUT_HANDLE
	Failures = 0;
	Log.Length = 0;

	LogAppend(&Log, L"kxgdi smoke test");
	Print(&Log);

	KxGdi = LoadLibraryW(L"kxgdi.dll");
	Check(KxGdi != NULL, L"kxgdi.dll loads");
	if (!KxGdi) ExitProcess(Failures);

	Gdi32 = LoadLibraryW(L"gdi32.dll");
	Exported = (PFN_OPENFROMLUID) GetProcAddress(KxGdi, "D3DKMTOpenAdapterFromLuid");
	Native = (PFN_OPENFROMLUID) GetProcAddress(Gdi32, "D3DKMTOpenAdapterFromLuid");

	Check(Exported != NULL, L"kxgdi exports D3DKMTOpenAdapterFromLuid");
	if (!Exported) ExitProcess(Failures);

	Check((PVOID) Exported != (PVOID) Native,
		L"exported D3DKMTOpenAdapterFromLuid is kxgdi's own implementation");

	{
		static const char *Forwarded[] = {
			"CreateCompatibleDC", "BitBlt", "SelectObject", "D3DKMTOpenAdapterFromHdc",
			"D3DKMTCloseAdapter", "GetDeviceCaps", "CreateFontIndirectW"
		};
		UINT i;
		BOOL AllMatch = TRUE;

		for (i = 0; i < sizeof(Forwarded) / sizeof(Forwarded[0]); ++i) {
			PVOID A = GetProcAddress(KxGdi, Forwarded[i]);
			PVOID B = GetProcAddress(Gdi32, Forwarded[i]);
			if (A == NULL || A != B) AllMatch = FALSE;
		}

		Check(AllMatch, L"forwarded exports resolve to the gdi32.dll functions");
	}

	Log.Length = 0;
	LogAppend(&Log, Native ? L"Native D3DKMTOpenAdapterFromLuid present (Windows 8+): comparing results"
	                       : L"No native D3DKMTOpenAdapterFromLuid (Windows 7): testing emulation only");
	Print(&Log);

	//
	// Collect LUIDs of all desktop display adapters via a Windows 7 API.
	//

	User32 = LoadLibraryW(L"user32.dll");
	pEnumDisplayDevicesW = (PFN_ENUMDISPLAYDEVICESW) GetProcAddress(User32, "EnumDisplayDevicesW");
	SeenCount = 0;

	for (Index = 0; pEnumDisplayDevicesW && SeenCount < 16; ++Index) {
		DISPLAY_DEVICEW Dev;
		D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
		UINT c, s;
		BOOL Duplicate;

		memset(&Dev, 0, sizeof(Dev));
		Dev.cb = sizeof(Dev);
		if (!pEnumDisplayDevicesW(NULL, Index, &Dev, 0)) break;

		memset(&Open, 0, sizeof(Open));
		for (c = 0; c < 31 && Dev.DeviceName[c]; ++c) Open.DeviceName[c] = Dev.DeviceName[c];
		if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&Open))) continue;
		CloseKmtAdapter(Open.hAdapter);

		Duplicate = FALSE;
		for (s = 0; s < SeenCount; ++s) {
			if (LuidEqual(Seen[s], Open.AdapterLuid)) Duplicate = TRUE;
		}

		if (!Duplicate) Seen[SeenCount++] = Open.AdapterLuid;
	}

	if (SeenCount == 0 && HasArgument(L"/ci")) {
		// Headless build machines may have no desktop display adapter.
		Log.Length = 0;
		LogAppend(&Log, L"  [SKIP] no display adapter found via GDI (/ci): adapter checks skipped");
		Print(&Log);
	} else {
		Check(SeenCount > 0, L"at least one display adapter found via GDI");
	}

	for (Index = 0; Index < SeenCount; ++Index) {
		TestLuid(Seen[Index], Exported, Native, TRUE);
	}

	Bogus.LowPart = 0xDEADBEEF;
	Bogus.HighPart = 0x7FFF0000;
	TestLuid(Bogus, Exported, Native, FALSE);

	Log.Length = 0;
	LogAppend(&Log, Failures ? L"RESULT: FAILED, failed checks: " : L"RESULT: all checks passed ");
	LogAppendHex(&Log, Failures);
	Print(&Log);

	ExitProcess(Failures);
}
